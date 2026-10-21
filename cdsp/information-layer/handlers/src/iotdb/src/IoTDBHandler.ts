import {
  getSubscriptionSimulator,
  SubscriptionSimulator,
} from "./SubscriptionSimulator";
import {
  SupportedMessageDataTypes,
  METADATA_SUFFIX,
} from "../utils/iotdb-constants";
import { HandlerBase, QueryResult } from "../../HandlerBase";
import {
  createDataPointsSchema,
  SupportedDataPoints,
} from "../config/iotdb-config";
import { databaseConfig, databaseParams } from "../config/database-params";
import { COLORS, logErrorStr, logWithColor } from "../../../../utils/logger";
import { WebSocketWithId } from "../../../../utils/database-params";
import {
  DataContentMessage,
  ErrorMessage,
  NewMessage,
  SetMessageType,
  STATUS_ERRORS,
  STATUS_SUCCESS,
  StatusMessage,
  SubscribeMessageType,
  UnsubscribeMessageType,
  GetMessageType,
} from "../../../../router/utils/NewMessage";
import {
  removeSuffixFromString,
  replaceDotsWithUnderscore,
  replaceUnderscoresWithDots,
} from "../../../utils/transformations";
import { IoTDBSession } from "./IoTDBSession";
import { logMessage, LogMessageType } from "../../../../utils/logger";

export class IoTDBHandler extends HandlerBase {
  private subscriptionSimulator: SubscriptionSimulator;
  private dataPointsSchema: SupportedDataPoints = {};
  private session: IoTDBSession;

  constructor(
    sendMessage: (
      ws: WebSocketWithId,
      message: StatusMessage | DataContentMessage | ErrorMessage,
    ) => void,
  ) {
    super(sendMessage);
    if (!databaseConfig) {
      throw new Error("Invalid database configuration.");
    }
    this.session = new IoTDBSession();
    this.subscriptionSimulator = getSubscriptionSimulator(
      this.session,
      this.sendMessageToClient.bind(this),
      this.createDataContentMessage,
      this.createStatusMessage,
      this.createErrorMessage,
      this.sendAlreadySubscribedErrorMsg.bind(this),
    );
  }

  async authenticateAndConnect(): Promise<void> {
    await this.session.open();
    const supportedDataPoint: SupportedDataPoints =
      this.getSupportedDataPoints() as SupportedDataPoints;
    this.dataPointsSchema = createDataPointsSchema(supportedDataPoint);
    await this.createDatabaseIfNeeded();
  }

  async createDatabaseIfNeeded(): Promise<void> {
    await this.session.createDatabaseIfNeeded(
      databaseParams["VSS"].databaseName,
    );
  }

  protected async get(
    message: GetMessageType,
    ws: WebSocketWithId,
  ): Promise<void> {
    return this.getData(message, ws);
  }

  protected async set(
    message: SetMessageType,
    ws: WebSocketWithId,
  ): Promise<void> {
    return this.setData(message, ws);
  }

  protected async subscribe(
    message: SubscribeMessageType,
    ws: WebSocketWithId,
  ): Promise<void> {
    return this.subscribeData(message, ws);
  }

  protected async unsubscribe(
    message: UnsubscribeMessageType,
    ws: WebSocketWithId,
  ): Promise<void> {
    return this.unsubscribeData(message, ws);
  }

  async unsubscribeClient(ws: WebSocketWithId): Promise<void> {
    this.subscriptionSimulator.unsubscribeClient(ws);
    await this.session.close();
  }

  public async getData(
    message: GetMessageType,
    ws: WebSocketWithId,
  ): Promise<void> {
    if (!this.session.isOpen()) {
      try {
        await this.session.open();
      } catch (err) {
        logMessage(
          `IoTDBSession open failed, cannot serve client get: ${err}`,
          LogMessageType.DEBUG,
        );
        this.sendGetResponseToClient(
          { success: false, error: "Session unavailable" },
          message.instance,
          [],
          ws,
          message.requestId,
          message.path,
          message.root,
          message.format,
        );
        return;
      }
    }

    const requestedDataPoints = this.getKnownDatapointsByPrefix(message.path);

    if (requestedDataPoints.length === 0) {
      this.sendRequestedDataPointsNotFoundErrorMsg(
        ws,
        message.path,
        message.requestId,
      );
      return;
    }

    const queryResult = await this.session.getDataPoints(
      requestedDataPoints,
      message.instance,
    );

    this.sendGetResponseToClient(
      queryResult,
      message.instance,
      requestedDataPoints,
      ws,
      message.requestId,
      message.path,
      message.root,
      message.format,
    );
  }

  public async setData(
    message: SetMessageType,
    ws: WebSocketWithId,
  ): Promise<void> {
    if (this.areNodesValid(message, ws)) {
      let statusMessage: StatusMessage | ErrorMessage;
      try {
        const data = {
          ...this.extractNodesFromMessageWithVinAsNode(message),
          ...this.extractNodesFromMetadata(message),
        };
        const measurements: string[] = [];
        const dataTypes: string[] = [];
        const values: unknown[] = [];

        for (const [key, value] of Object.entries(data)) {
          measurements.push(key);
          dataTypes.push(this.getDataType(key));
          values.push(value);
        }

        const deviceId = databaseParams["VSS"].databaseName;
        const writeResult = await this.session.setDataPoints(
          deviceId,
          measurements,
          dataTypes,
          values,
        );

        if (!writeResult.success) {
          throw new Error(writeResult.error ?? "Unknown database error");
        }

        logWithColor(
          `Record inserted to device ${deviceId},
      status code: `.concat(
            JSON.stringify(writeResult.status ?? { code: 200 }),
          ),
          COLORS.GREY,
        );

        statusMessage = this.createStatusMessage(
          STATUS_SUCCESS.OK,
          "Successfully wrote data to database.",
          message.requestId,
        );
      } catch (error: unknown) {
        const errMsg = error instanceof Error ? error.message : "Unknown error";
        statusMessage = this.createErrorMessage(
          STATUS_ERRORS.SERVICE_UNAVAILABLE,
          `Database service unavailable`,
          `Failed writing data. ${errMsg}`,
          message.requestId,
        );
      }
      this.sendMessageToClient(ws, statusMessage);
    }
  }

  public async subscribeData(
    message: SubscribeMessageType,
    ws: WebSocketWithId,
  ): Promise<void> {
    const newDataPoints = this.getKnownDatapointsByPrefix(message.path);

    if (newDataPoints.length === 0) {
      this.sendRequestedDataPointsNotFoundErrorMsg(
        ws,
        message.path,
        message.requestId,
      );
      return;
    }

    await this.session.subscribeDataPoints(
      message,
      ws,
      newDataPoints,
      this.subscriptionSimulator.subscribe.bind(this.subscriptionSimulator),
    );
  }

  public async unsubscribeData(
    message: UnsubscribeMessageType,
    ws: WebSocketWithId,
  ): Promise<void> {
    const dataPointsToUnsub = this.getKnownDatapointsByPrefix(message.path);

    if (dataPointsToUnsub.length === 0) {
      this.sendRequestedDataPointsNotFoundErrorMsg(
        ws,
        message.path,
        message.requestId,
      );
      return;
    }

    await this.session.unsubscribeDataPoints(
      message,
      ws,
      dataPointsToUnsub,
      this.subscriptionSimulator.unsubscribe.bind(this.subscriptionSimulator),
    );
  }

  private extractNodesFromMetadata(
    message: SetMessageType,
  ): Record<string, unknown> {
    if (!message.metadata) return {};

    return Object.fromEntries(
      Object.entries(message.metadata).map(([key, value]) => [
        message.path + this.formatKey(key) + METADATA_SUFFIX,
        JSON.stringify(value),
      ]),
    );
  }

  private formatKey = (key: string) =>
    key ? "_" + replaceDotsWithUnderscore(key) : "";

  private getDataType(dataPointName: string) {
    if (dataPointName.endsWith(METADATA_SUFFIX)) {
      const dataPoint = removeSuffixFromString(dataPointName, METADATA_SUFFIX);
      if (
        Object.prototype.hasOwnProperty.call(this.dataPointsSchema, dataPoint)
      ) {
        return SupportedMessageDataTypes.string;
      } else {
        throw new Error(
          `Invalid metadata provided, datapoint ${replaceUnderscoresWithDots(dataPoint)} not defined.`,
        );
      }
    }
    return this.dataPointsSchema[dataPointName];
  }

  /**
   * Validates the nodes in a message against the schema of a media element.
   *
   * @param message - The message object containing details for the request.
   * @param ws - The WebSocket object for communication.
   * @returns - Returns true if all nodes are valid against the schema, otherwise false.
   */
  private areNodesValid(message: NewMessage, ws: WebSocketWithId): boolean {
    const errorReason = this.validateNodesAgainstSchema(
      message,
      this.dataPointsSchema,
    );

    if (errorReason) {
      logErrorStr(
        `Error validating message nodes against schema: ${errorReason}`,
      );
      const errorMessage = this.createErrorMessage(
        STATUS_ERRORS.NOT_FOUND,
        "Nodes not found in schema",
        errorReason,
        message.requestId,
      );
      this.sendMessageToClient(ws, errorMessage);
      return false;
    }
    return true;
  }

  async getDataPointsFromDB(
    dataPoints: string[],
    vin: string,
  ): Promise<QueryResult> {
    return this.session.getDataPoints(dataPoints, vin);
  }

  getKnownDatapointsByPrefix(datapointPrefix: string) {
    const allKnownDataPoints: string[] = Object.keys(this.dataPointsSchema);
    return allKnownDataPoints
      .filter((field) => field !== databaseParams["VSS"].dataPointId) // remove VIN from the list, so only real data points are left
      .filter(
        (value) =>
          value === datapointPrefix ||
          (value.startsWith(datapointPrefix) &&
            value[datapointPrefix.length] === "_"),
      );
  }
}
