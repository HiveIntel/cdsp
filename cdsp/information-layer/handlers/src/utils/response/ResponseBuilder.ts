/**
 * Utility functions for constructing response messages.
 * Provides factory functions for creating typed message responses (status, error, data content).
 */

import {
  DataContentMessage,
  ErrorMessage,
  StatusMessage,
} from "../../../../router/utils/NewMessage";
import {
  buildDataStructureFlat,
  buildDataStructureAsTree,
  getSchemaOrThrow,
  normalizeRequestedPath,
} from "../formatting/DataStructureFormatter";
import { replaceUnderscoresWithDots } from "../../../utils/transformations";

/**
 * Get current timestamp in seconds and nanoseconds format.
 */
function getCurrentTimestamp(): { seconds: number; nanos: number } {
  const now = new Date();
  const seconds = Math.floor(now.getTime() / 1000);
  const nanos = (now.getTime() % 1000) * 1e6;
  return { seconds, nanos: nanos };
}

/**
 * Create a status message.
 *
 * @param code HTTP-based status code
 * @param statusMessage A descriptive message
 * @param requestId The ID of the corresponding request
 * @returns A status message
 */
export function createStatusMessage(
  code: number,
  statusMessage: string,
  requestId: string,
): StatusMessage {
  return {
    type: "status",
    code: code,
    message: statusMessage,
    requestId: requestId,
    timestamp: getCurrentTimestamp(),
  };
}

/**
 * Create an error message.
 *
 * @param code HTTP-based status code
 * @param message A short message
 * @param reason A descriptive reason for the error
 * @param requestId The ID of the corresponding request
 * @returns An error message
 */
export function createErrorMessage(
  code: number,
  message: string,
  reason: string,
  requestId: string,
): ErrorMessage {
  return {
    type: "error",
    code: code,
    message: message,
    reason: reason,
    requestId: requestId,
  };
}

/**
 * Create a data content message.
 *
 * @param instance The ID of the element in the tree
 * @param dataPoints The data points with values
 * @param root "absolute" or "relative" paths
 * @param format "nested" or "flat" data structure
 * @param path The path associated with the data points
 * @param metadata Available metadata for the data points
 * @param requestId The ID of the corresponding request
 * @returns A data content message
 */
export function createDataContentMessage(
  instance: string,
  dataPoints: Array<{ name: string; value: any }>,
  root: "absolute" | "relative",
  format: "nested" | "flat",
  path: string,
  metadata?: Array<{ name: string; value: any }>,
  requestId?: string,
): DataContentMessage {
  // Ensure nodes are valid
  if (dataPoints.length === 0) {
    throw new Error("Nodes array cannot be empty.");
  }

  // Process root first: determine the base path for each datapoint
  // Then process format: structure the output as flat or nested
  const schema = getSchemaOrThrow(dataPoints);
  const requestedPath = normalizeRequestedPath(
    replaceUnderscoresWithDots(path),
    schema,
  );

  return {
    type: "data",
    instance: instance,
    schema: schema,
    data:
      format === "nested"
        ? buildDataStructureAsTree(dataPoints, root, requestedPath, schema)
        : buildDataStructureFlat(dataPoints, root, requestedPath, schema),
    requestId: requestId,
    ...(metadata &&
      Object.keys(metadata).length && {
        metadata: buildDataStructureFlat(metadata, root, requestedPath, schema),
      }),
  };
}
