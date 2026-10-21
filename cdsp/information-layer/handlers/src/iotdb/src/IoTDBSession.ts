import { Session, SessionDataSet, TreeTablet } from "@iotdb/client";
import { databaseConfig, databaseParams } from "../config/database-params";
import { logError, logMessage, LogMessageType } from "../../../../utils/logger";
import { QueryResult } from "../../HandlerBase";
import { METADATA_SUFFIX } from "../utils/iotdb-constants";
import {
  replaceDotsWithUnderscore,
  replaceUnderscoresWithDots,
} from "../../../utils/transformations";
import {
  SubscribeMessageType,
  UnsubscribeMessageType,
} from "../../../../router/utils/NewMessage";
import { WebSocketWithId } from "../../../../utils/database-params";

const METADATA_SUFFIX_DISPLAY = ".Metadata";

function isValidDatapoint(value: unknown): boolean {
  if (value === null || value === undefined) return false;
  if (typeof value === "number") return !isNaN(value);
  if (typeof value === "string") return true;
  return false;
}

function isValidMetadata(value: unknown): boolean {
  return value !== null && typeof value === "object";
}

function deepMerge(
  target: Record<string, unknown>,
  source: Record<string, unknown>,
): Record<string, unknown> {
  for (const key in source) {
    const srcVal = source[key];
    if (srcVal && typeof srcVal === "object" && !Array.isArray(srcVal)) {
      target[key] = deepMerge(
        (target[key] ?? {}) as Record<string, unknown>,
        srcVal as Record<string, unknown>,
      );
    } else {
      target[key] = srcVal;
    }
  }
  return target;
}

function buildReceivedTimestampMetadata(
  timestampMs: number,
): Record<string, unknown> {
  return {
    timestamps: {
      received: {
        seconds: Math.floor(timestampMs / 1000),
        nanos: (timestampMs % 1000) * 1e6,
      },
    },
  };
}

/**
 * IoTDBSession encapsulates all direct interactions with the @iotdb/client library,
 * providing a clean API for the IoTDBHandler to perform database operations without
 * needing to manage connection details, query construction, or result parsing.
 *
 * It includes methods for opening/closing the session, ensuring database existence,
 * querying data points (latest and within a time window), writing data points with
 * metadata, and handling subscriptions. The session manages its own connection state
 * and applies consistent transformations to maintain compatibility with the handler's
 * expected data formats and metadata conventions.
 *
 * This separation of concerns allows the IoTDBHandler to focus on message handling
 * and response construction, while the IoTDBSession handles all database-specific
 * logic and interactions, making future maintenance and potential migration to native
 * subscription handling easier.
 */
export class IoTDBSession {
  private readonly session: Session;
  private sessionOpen = false;

  constructor() {
    if (!databaseConfig) {
      throw new Error("Invalid database configuration.");
    }
    this.session = new Session({
      host: databaseConfig.iotdbHost,
      port: databaseConfig.iotdbPort,
      username: databaseConfig.iotdbUser,
      password: databaseConfig.iotdbPassword,
    });
  }

  public async open(): Promise<void> {
    await this.session.open();
    try {
      await this.session.executeNonQueryStatement("SET SQL_DIALECT=TREE");
      logMessage("IoTDB Session: SQL dialect set to TREE", LogMessageType.INFO);
    } catch (error: unknown) {
      // Non-blocking: dialect defaults to TREE on server side anyway.
      logMessage(
        "IoTDB Session: could not set SQL dialect explicitly; using server default" +
          (error instanceof Error ? `: ${error.message}` : ""),
        LogMessageType.WARNING,
      );
    }
    this.sessionOpen = true;
    logMessage("IoTDB Session: session opened", LogMessageType.INFO);
  }

  public async close(): Promise<void> {
    if (this.sessionOpen) {
      await this.session.close();
      this.sessionOpen = false;
    }
  }

  public isOpen(): boolean {
    return this.sessionOpen;
  }

  public async createDatabaseIfNeeded(databaseName: string): Promise<void> {
    if (!this.sessionOpen) {
      await this.open();
    }

    const sql = `CREATE DATABASE ${databaseName};`;
    try {
      await this.session.executeNonQueryStatement(sql);
      logMessage(
        `IoTDB Session: ensured database exists (${databaseName})`,
        LogMessageType.INFO,
      );
    } catch (error: unknown) {
      const errMsg = error instanceof Error ? error.message : String(error);

      const alreadyExists =
        /already exists/i.test(errMsg) ||
        /database.*exist/i.test(errMsg) ||
        /root\..*already/i.test(errMsg);

      if (alreadyExists) {
        logMessage(
          `IoTDB Session: database already exists (${databaseName})`,
          LogMessageType.INFO,
        );
        return;
      }

      // Keep previous tolerant behavior: log but do not throw hard here
      logError("IoTDB Session: createDatabaseIfNeeded failed", error);
    }
  }

  //** GET DATA POINTS */

  /**
   * Queries the latest value (and any stored metadata) for each given data point
   * using the new @iotdb/client, and returns results in the canonical QueryResult shape.
   *
   * Applies the same column-name normalisation and received-timestamp injection as
   * the legacy path so downstream response building remains identical.
   */
  public async getDataPoints(
    dataPoints: string[],
    vin: string,
  ): Promise<QueryResult> {
    const { databaseName, dataPointId } = databaseParams["VSS"];
    const latestDataPoints: Record<string, unknown> = {};
    const latestMetadata: Record<string, unknown> = {};
    // TODO(perf): batch all datapoints into a single SELECT to reduce round-trips.
    const safeVin = vin.replace(/'/g, "''");

    try {
      for (const dataPoint of dataPoints) {
        const metadataPoint = `${dataPoint}${METADATA_SUFFIX}`;
        const sql =
          `SELECT ${dataPoint},${metadataPoint}` +
          ` FROM ${databaseName}` +
          ` WHERE ${dataPointId} = '${safeVin}'` +
          `   AND ${dataPoint} IS NOT NULL` +
          ` ORDER BY time DESC LIMIT 1`;

        const dataSet = await this.session.executeQueryStatement(sql);
        await this.drainDataSet(
          dataSet,
          databaseName,
          latestDataPoints,
          latestMetadata,
        );
        await dataSet.close();
      }

      return {
        success: true,
        dataPoints: Object.entries(latestDataPoints).map(([name, value]) => ({
          name,
          value,
        })),
        metadata: Object.entries(latestMetadata).map(([name, value]) => ({
          name,
          value,
        })),
      };
    } catch (error: unknown) {
      logError("IoTDB Session: getDataPoints failed", error);
      const errMsg =
        error instanceof Error ? error.message : "Unknown database error";
      return { success: false, error: errMsg };
    }
  }

  /**
   * Queries all values for each given data point within the specified time window,
   * along with any stored metadata, using the new @iotdb/client. Returns results
   * in the canonical QueryResult shape, applying the same normalisation and
   * timestamp injection as the legacy path.
   */
  public async getDataPointsInWindow(
    dataPoints: string[],
    vin: string,
    lowerExclusive: number,
    upperInclusive: number,
  ): Promise<QueryResult> {
    const { databaseName, dataPointId } = databaseParams["VSS"];
    const latestDataPoints: Record<string, unknown> = {};
    const latestMetadata: Record<string, unknown> = {};
    const safeVin = vin.replace(/'/g, "''");

    try {
      if (!this.sessionOpen) {
        await this.open();
      }

      for (const dataPoint of dataPoints) {
        const metadataPoint = `${dataPoint}${METADATA_SUFFIX}`;
        const sql =
          `SELECT ${dataPoint},${metadataPoint}` +
          ` FROM ${databaseName}` +
          ` WHERE ${dataPointId} = '${safeVin}'` +
          `   AND Time > ${lowerExclusive}` +
          `   AND Time <= ${upperInclusive}` +
          ` ORDER BY Time ASC`;

        const dataSet = await this.session.executeQueryStatement(sql);
        await this.drainDataSet(
          dataSet,
          databaseName,
          latestDataPoints,
          latestMetadata,
        );
        await dataSet.close();
      }

      return {
        success: true,
        dataPoints: Object.entries(latestDataPoints).map(([name, value]) => ({
          name,
          value,
        })),
        metadata: Object.entries(latestMetadata).map(([name, value]) => ({
          name,
          value,
        })),
      };
    } catch (error: unknown) {
      logError("IoTDB Session: Get DataPointsInWindow failed", error);
      const errMsg =
        error instanceof Error ? error.message : "Unknown database error";
      return { success: false, error: errMsg };
    }
  }

  private async drainDataSet(
    dataSet: SessionDataSet,
    databaseName: string,
    latestDataPoints: Record<string, unknown>,
    latestMetadata: Record<string, unknown>,
  ): Promise<void> {
    const columnNames = dataSet.getColumnNames();
    while (await dataSet.hasNext()) {
      const row = dataSet.next();
      const timestampMs = row.getTimestamp();
      for (const colName of columnNames) {
        const rawValue = row.getValue(colName);
        if (rawValue === null || rawValue === undefined) continue;

        const dotKey = replaceUnderscoresWithDots(colName);
        const dbPrefix = `${databaseName}.`;
        const shortKey = dotKey.startsWith(dbPrefix)
          ? dotKey.slice(dbPrefix.length)
          : dotKey;

        if (shortKey.endsWith(METADATA_SUFFIX_DISPLAY)) {
          this.applyMetadataColumn(
            shortKey,
            rawValue,
            timestampMs,
            latestMetadata,
          );
        } else if (isValidDatapoint(rawValue)) {
          latestDataPoints[shortKey] = rawValue;
          this.applyDefaultMetadataColumn(
            shortKey,
            timestampMs,
            latestMetadata,
          );
        }
      }
    }
  }

  private applyMetadataColumn(
    shortKey: string,
    rawValue: unknown,
    timestampMs: number,
    latestMetadata: Record<string, unknown>,
  ): void {
    const dpKey = shortKey.slice(0, -METADATA_SUFFIX_DISPLAY.length);
    try {
      const parsed: unknown =
        typeof rawValue === "string" ? JSON.parse(rawValue) : rawValue;
      if (isValidMetadata(parsed)) {
        latestMetadata[dpKey] = deepMerge(
          parsed as Record<string, unknown>,
          buildReceivedTimestampMetadata(timestampMs),
        );
      }
    } catch {
      // skip malformed metadata JSON
    }
  }

  private applyDefaultMetadataColumn(
    shortKey: string,
    timestampMs: number,
    latestMetadata: Record<string, unknown>,
  ): void {
    if (!latestMetadata[shortKey]) {
      latestMetadata[shortKey] = buildReceivedTimestampMetadata(timestampMs);
    }
  }

  //** SET DATA POINTS */

  /**
   * Writes the given data points to the database with the new @iotdb/client,
   * including any metadata as separate columns. Expects measurements and
   * dataTypes to be in the same order as values, and that all three arrays
   * are the same length.
   *
   * Returns success/failure and lets caller handle response building.
   */
  public async setDataPoints(
    deviceId: string,
    measurements: string[],
    dataTypes: string[],
    values: unknown[],
  ): Promise<{ success: boolean; status?: unknown; error?: string }> {
    try {
      if (!this.sessionOpen) {
        await this.open();
      }

      if (
        measurements.length !== dataTypes.length ||
        measurements.length !== values.length
      ) {
        return {
          success: false,
          error:
            "Invalid write payload: measurements, dataTypes, and values must have the same length",
        };
      }

      // Keep naming parity with legacy path (dot notation -> underscore)
      const transformedMeasurements = measurements.map((measurement) =>
        replaceDotsWithUnderscore(measurement),
      );

      // Convert type names to IoTDB numeric TSDataType codes
      const ioTDBTypes = dataTypes.map((dt) => this.getDataTypeCode(dt));

      // Build one-row tablet write (recommended typed API in @iotdb/client)
      // We import TreeTablet from @iotdb/client
      const tablet = new TreeTablet(
        deviceId,
        transformedMeasurements,
        ioTDBTypes,
      );

      type IoTDBValue = string | number | boolean | null | Date | Buffer;
      tablet.addRow(Date.now(), values as IoTDBValue[]);

      await this.session.insertTablet(tablet);

      logMessage("IoTDB Session: setDataPoints succeeded", LogMessageType.INFO);

      return { success: true, status: { code: 200 } };
    } catch (error: unknown) {
      logError("IoTDB Session: setDataPoints failed", error);
      const errMsg =
        error instanceof Error ? error.message : "Unknown database error";
      return { success: false, error: errMsg };
    }
  }

  /**
   * Note: this method is copy-pasted from @iotdb/client Session class
   * because the TSDataType parsing logic is not exported
   * and we want to maintain the same type handling in our
   * adapter as IoTDB does internally for consistency.
   * Helper method to convert data type string to numeric code
   */
  private getDataTypeCode(typeStr: string): number {
    const type = String(typeStr).toUpperCase();
    if (type.includes("BOOLEAN")) return 0;
    if (type.includes("UINT8")) return 1; // map to INT32
    if (type.includes("INT8")) return 1; // map to INT32
    if (type.includes("UINT16")) return 1; // map to INT32
    if (type.includes("INT16")) return 1; // map to INT32
    if (type.includes("UINT32")) return 1; // map to INT32
    if (type.includes("INT32")) return 1;
    if (type.includes("INT64")) return 2;
    if (type.includes("FLOAT")) return 3;
    if (type.includes("DOUBLE")) return 4;
    if (type.includes("TEXT")) return 5;
    if (type.includes("TIMESTAMP")) return 8;
    if (type.includes("DATE")) return 9;
    if (type.includes("BLOB")) return 10;
    if (type.includes("STRING")) return 11;

    // fallback: unsupported app type -> STRING
    logMessage(
      `Unsupported IoTDB data type '${typeStr}', falling back to STRING`,
      LogMessageType.WARNING,
    );
    return 11;
  }

  //** SUBSCRIBE / UNSUBSCRIBE DATA POINTS */

  /**
   * Bridge method for new-path subscription flow.
   * For now, it delegates to the provided subscription function (simulator-backed),
   * while keeping the handler decoupled from execution details.
   *
   * This establishes the IoTDBSession API boundary so native subscription
   * logic can be moved here later without changing handler routing code.
   */
  public async subscribeDataPoints(
    message: SubscribeMessageType,
    ws: WebSocketWithId,
    dataPoints: string[],
    subscribeFn: (
      message: SubscribeMessageType,
      ws: WebSocketWithId,
      dataPoints: string[],
    ) => Promise<void>,
  ): Promise<void> {
    await subscribeFn(message, ws, dataPoints);
  }

  /**
   * Bridge method for new-path unsubscription flow.
   * For now, it delegates to the provided unsubscription function (simulator-backed),
   * while preserving the same contract and cleanup semantics.
   *
   * This keeps unsubscribe lifecycle behavior behind the IoTDBSession boundary
   * and prepares migration to native unsubscribe handling later.
   */
  public async unsubscribeDataPoints(
    message: UnsubscribeMessageType,
    ws: WebSocketWithId,
    dataPoints: string[],
    unsubscribeFn: (
      message: UnsubscribeMessageType,
      ws: WebSocketWithId,
      dataPoints: string[],
    ) => void,
  ): Promise<void> {
    unsubscribeFn(message, ws, dataPoints);
  }
}
