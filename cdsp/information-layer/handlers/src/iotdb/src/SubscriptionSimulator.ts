import {
  logMessage,
  logError,
  logErrorStr,
  LogMessageType,
} from "../../../../utils/logger";
import { databaseConfig } from "../config/database-params";
import { IoTDBSession } from "./IoTDBSession"; // Ensure IoTDBSession is included in the build
import { WebSocketWithId } from "../../../../utils/database-params";
import {
  DataContentMessage,
  ErrorMessage,
  STATUS_ERRORS,
  STATUS_SUCCESS,
  StatusMessage,
  SubscribeMessageType,
  UnsubscribeMessageType,
} from "../../../../router/utils/NewMessage";
import { toResponseFormat } from "../../../utils/transformations";

export type Subscription = {
  vin: string;
  dataPoints: Set<string>;
  requestId: string;
  path: string;
  root: "absolute" | "relative";
  format: "nested" | "flat";
};

export type WebsocketToSubscriptionsMap = Map<WebSocketWithId, Subscription[]>;

// Define the singleton instance at the module level
let subscriptionSimulatorInstance: SubscriptionSimulator | null = null;

// Function to get or initialize the singleton instance
export function getSubscriptionSimulator(
  session: IoTDBSession,
  sendMessageToClient: (
    ws: WebSocketWithId,
    message: StatusMessage | DataContentMessage | ErrorMessage,
  ) => void,
  createDataContentMessage: (
    instance: string,
    dataPoints: Array<{ name: string; value: unknown }>,
    root: "absolute" | "relative",
    format: "nested" | "flat",
    path: string,
    metadata?: Array<{ name: string; value: unknown }>, // FIXME: HandleBase contains optional metadata
    requestId?: string,
  ) => DataContentMessage,
  createStatusMessage: (
    code: number,
    statusMessage: string,
    requestId: string,
  ) => StatusMessage,
  createErrorMessage: (
    code: number,
    message: string,
    reason: string,
    requestId: string,
  ) => ErrorMessage,
  sendAlreadySubscribedErrorMsg: (
    ws: WebSocketWithId,
    vin: string,
    newDataPoints: string[],
    requestId: string,
  ) => void,
): SubscriptionSimulator {
  if (!subscriptionSimulatorInstance) {
    subscriptionSimulatorInstance = new SubscriptionSimulator(
      session,
      sendMessageToClient,
      createDataContentMessage,
      createStatusMessage,
      createErrorMessage,
      sendAlreadySubscribedErrorMsg,
    );
    logMessage("SubscriptionSimulator instance created.", LogMessageType.INFO);
  }
  return subscriptionSimulatorInstance;
}

export class SubscriptionSimulator {
  private intervalId: NodeJS.Timeout | null = null;
  private session: IoTDBSession;
  private timeIntervalLowerLimit: number | undefined = undefined;
  private websocketToSubscriptionsMap: WebsocketToSubscriptionsMap = new Map();
  private readonly sendMessageToClient: (
    ws: WebSocketWithId,
    message: StatusMessage | DataContentMessage | ErrorMessage,
  ) => void;
  private readonly createDataContentMessage: (
    instance: string,
    dataPoints: Array<{ name: string; value: unknown }>,
    root: "absolute" | "relative",
    format: "nested" | "flat",
    path: string,
    metadata?: Array<{ name: string; value: unknown }>, // FIXME: HandleBase contains optional metadata
    requestId?: string,
  ) => DataContentMessage;
  private readonly createStatusMessage: (
    code: number,
    statusMessage: string,
    requestId: string,
  ) => StatusMessage;
  private readonly createErrorMessage: (
    code: number,
    message: string,
    reason: string,
    requestId: string,
  ) => ErrorMessage;
  private readonly sendAlreadySubscribedErrorMsg: (
    ws: WebSocketWithId,
    vin: string,
    newDataPoints: string[],
    requestId: string,
  ) => void;

  constructor(
    session: IoTDBSession,
    sendMessageToClient: (
      ws: WebSocketWithId,
      message: StatusMessage | DataContentMessage | ErrorMessage,
    ) => void,
    createDataContentMessage: (
      instance: string,
      dataPoints: Array<{ name: string; value: unknown }>,
      root: "absolute" | "relative",
      format: "nested" | "flat",
      path: string,
      metadata?: Array<{ name: string; value: unknown }>, // FIXME: HandleBase contains optional metadata
      requestId?: string,
    ) => DataContentMessage,
    createStatusMessage: (
      code: number,
      statusMessage: string,
      requestId: string,
    ) => StatusMessage,
    createErrorMessage: (
      code: number,
      message: string,
      reason: string,
      requestId: string,
    ) => ErrorMessage,
    sendAlreadySubscribedErrorMsg: (
      ws: WebSocketWithId,
      vin: string,
      newDataPoints: string[],
      requestId: string,
    ) => void,
  ) {
    this.session = session;
    this.notifyDatabaseChanges = this.notifyDatabaseChanges.bind(this);
    this.sendMessageToClient = sendMessageToClient;
    this.createDataContentMessage = createDataContentMessage;
    this.createStatusMessage = createStatusMessage;
    this.createErrorMessage = createErrorMessage;
    this.sendAlreadySubscribedErrorMsg = sendAlreadySubscribedErrorMsg;

    // Register the cleanup function
    process.on("exit", () => this.cleanup()); // Called when the process exits normally
    process.on("SIGINT", () => {
      this.cleanup();
      process.exit(0); // Ensure the process exits
    });
  }

  private cleanup(): void {
    logMessage("Cleaning up SubscriptionSimulator...");

    if (this.intervalId !== null) {
      clearInterval(this.intervalId);
      this.intervalId = null;
    }
    this.timeIntervalLowerLimit = undefined;
  }

  /**
   * Creates a new subscription for the given websocket client for provided data points and VIN (message.instance)
   *
   * An associated client is represented by a websocket.
   * If the subscription exists, an error message is sent.
   * If a subscription was created and there is no timer yet, periodic database listener is enabled.
   * @param message subscribe message
   * @param wsOfNewSubscription websocket representing a client to be associated with the new subscription
   * @param newDataPoints list of data points the client wants to subscribe to
   * @returns void
   */
  async subscribe(
    message: SubscribeMessageType,
    wsOfNewSubscription: WebSocketWithId,
    newDataPoints: string[], // FIXME: Check if the data points are extracted from the path as array before calling this method
  ): Promise<void> {
    const subscriptions: Subscription[] =
      this.websocketToSubscriptionsMap.get(wsOfNewSubscription) ?? [];
    let subscription = subscriptions.find(
      (value) => value.requestId === message.requestId,
    );

    if (!subscription) {
      subscription = {
        vin: message.instance,
        dataPoints: new Set(),
        requestId: message.requestId,
        path: message.path,
        root: message.root,
        format: message.format,
      };
      subscriptions.push(subscription); // Add to the array
    } else {
      // Replace existing subscription for the same requestId
      subscription.vin = message.instance;
      subscription.path = message.path;
      subscription.root = message.root;
      subscription.format = message.format;
      subscription.dataPoints = new Set();
    }

    // FIXME: Check if this is required, seems that subscription.dataPoints is always empty here
    const alreadySubscribed = newDataPoints.every((dataPoint) =>
      subscription.dataPoints.has(dataPoint),
    );

    if (alreadySubscribed) {
      this.sendAlreadySubscribedErrorMsg(
        wsOfNewSubscription,
        message.instance,
        newDataPoints,
        message.requestId,
      );
      return;
    }

    // Update subscribed data points
    newDataPoints.forEach((dataPoint) =>
      subscription.dataPoints.add(dataPoint),
    );
    this.websocketToSubscriptionsMap.set(wsOfNewSubscription, subscriptions);

    this.startSubscriptionListenerIfNeeded();
    this.sendMessageToClient(
      wsOfNewSubscription,
      this.createStatusMessage(
        STATUS_SUCCESS.OK,
        `Successfully subscribed to '${message.instance}' [${toResponseFormat(newDataPoints)}].`,
        message.requestId,
      ),
    );

    logMessage(
      "client id " +
        wsOfNewSubscription.id +
        " successfully subscribed to " +
        message.instance,
      LogMessageType.INFO,
    );
    this.logSubscriptions();
  }

  private startSubscriptionListenerIfNeeded() {
    if (this.intervalId === null && databaseConfig) {
      this.intervalId = setInterval(
        this.notifyDatabaseChanges,
        databaseConfig.pollIntervalLenInSec * 1000,
      );
      this.intervalId.unref(); // Ensure timer doesn't keep the process alive
      this.timeIntervalLowerLimit = Date.now();
      logMessage(
        `started timer: interval=${databaseConfig.pollIntervalLenInSec}s, websockets=${this.websocketToSubscriptionsMap.size}, subscriptions=${Array.from(this.websocketToSubscriptionsMap.values()).reduce((n, subs) => n + subs.length, 0)}`,
        LogMessageType.INFO,
      );
    }
  }

  /**
   * Unsubscribes the client from all subscriptions that match the given VIN and exact datapoints.
   * All subscriptions with the given VIN and containing exactly the same datapoints are removed.
   * If no subscriptions are left for the client, it is removed.
   * If no clients are left, the periodic database listener is disabled.
   * @param message unsubscribe message
   * @param wsOfSubscriptionToBeDeleted websocket to be removed from subscription
   * @param dataPointsToUnsub list of data point strings to unsubscribe the client from
   * @returns void
   */
  unsubscribe(
    message: UnsubscribeMessageType,
    wsOfSubscriptionToBeDeleted: WebSocketWithId,
    dataPointsToUnsub: string[],
  ): void {
    const subscriptions = this.websocketToSubscriptionsMap.get(
      wsOfSubscriptionToBeDeleted,
    );

    if (!subscriptions) {
      this.sendMessageToClient(
        wsOfSubscriptionToBeDeleted,
        this.createErrorMessage(
          STATUS_ERRORS.NOT_FOUND,
          `Subscription not found`,
          `Cannot unsubscribe. No subscription for instance '${message.instance}'`,
          message.requestId,
        ),
      );
      return;
    }

    // Find all subscriptions matching the VIN and exact datapoints
    const subscriptionsToRemove = subscriptions.filter((sub) => {
      // Check if VIN matches
      if (sub.vin !== message.instance) {
        return false;
      }

      // Check if subscription has exactly the same datapoints (same count and same items)
      if (sub.dataPoints.size !== dataPointsToUnsub.length) {
        return false;
      }

      // Check if all datapoints to unsubscribe are in the subscription
      return dataPointsToUnsub.every((dp) => sub.dataPoints.has(dp));
    });

    // If no matching subscriptions found, return error
    if (subscriptionsToRemove.length === 0) {
      this.sendMessageToClient(
        wsOfSubscriptionToBeDeleted,
        this.createErrorMessage(
          STATUS_ERRORS.NOT_FOUND,
          `Subscription not found`,
          `Cannot unsubscribe. No subscription for instance '${message.instance}' with the provided data points.`,
          message.requestId,
        ),
      );
      return;
    }

    // Remove the matching subscriptions
    const updatedSubscriptions = subscriptions.filter(
      (sub) => !subscriptionsToRemove.includes(sub),
    );

    if (updatedSubscriptions.length > 0) {
      this.websocketToSubscriptionsMap.set(
        wsOfSubscriptionToBeDeleted,
        updatedSubscriptions,
      );
    } else {
      // Remove the websocket from the map if no subscriptions are left
      this.websocketToSubscriptionsMap.delete(wsOfSubscriptionToBeDeleted);
    }

    this.removeTimerIfNoSubscription();
    this.logSubscriptions();

    this.sendMessageToClient(
      wsOfSubscriptionToBeDeleted,
      this.createStatusMessage(
        STATUS_SUCCESS.OK,
        "Successfully unsubscribed",
        message.requestId,
      ),
    );
  }

  /**
   * Removes all subscriptions for a client.
   *
   * Checks all subscriptions and removes the given client from each of them.
   * If a subscription is left with no client, the entire subscription is removed.
   * If no subscription is left, the timer is stopped.
   * @param ws websocket representing a client to be removed from all subscriptions
   */
  unsubscribeClient(ws: WebSocketWithId): void {
    this.websocketToSubscriptionsMap.delete(ws);
    this.removeTimerIfNoSubscription();
    logMessage(
      "client id " + ws.id + " unsubscribed from all subscriptions",
      LogMessageType.INFO,
    );
    this.logSubscriptions();
  }

  /**
   * Removes the timer if there is no subscription left.
   */
  private removeTimerIfNoSubscription() {
    if (
      this.intervalId !== null &&
      this.websocketToSubscriptionsMap.size === 0
    ) {
      clearInterval(this.intervalId);
      this.intervalId = null;
      this.timeIntervalLowerLimit = undefined;
      logMessage("stopped timer: no active subscriptions", LogMessageType.INFO);
    }
  }

  /**
   * Logs all subscriptions with their associated clients on the console.
   * Clients are represented by the ID of the websocket.
   */
  private logSubscriptions(): void {
    let logString = "subscriptions:\n";
    for (const [
      ws,
      subscriptions,
    ] of this.websocketToSubscriptionsMap.entries()) {
      logString += `WS id: ${ws.id} is subscribed to: \n`;
      subscriptions.forEach(
        (sub) =>
          (logString += `  ${sub.vin} => [${Array.from(sub.dataPoints).join(", ")}] \n`),
      );
    }
    logMessage(logString, LogMessageType.INFO);
  }

  /**
   * Notifies subscribed clients about changes on data points during the last time interval.
   *
   * Checks for each subscription if there has been a change on the related data point in the last time interval.
   * For each change, it sends an update message for each client subscribed for this data point.
   *
   * @returns void
   */
  private async notifyDatabaseChanges(): Promise<void> {
    if (!this.timeIntervalLowerLimit) {
      logErrorStr(
        "time of last check is undefined. Cannot go for notifications.",
      );
      return;
    }

    const timeIntervalUpperLimit = Date.now();
    for (const [
      ws,
      subscriptions,
    ] of this.websocketToSubscriptionsMap.entries()) {
      await Promise.all(
        subscriptions.map(async (subscription) => {
          const dataContentMessage = await this.checkForChanges(
            subscription,
            timeIntervalUpperLimit,
          );
          if (dataContentMessage) {
            this.sendMessageToClient(ws, dataContentMessage);
          }
        }),
      );
    }
    this.timeIntervalLowerLimit = timeIntervalUpperLimit;
  }

  /**
   * Checks for changes for the given vin in the last time interval.
   * For each change it creates an update message for each client subscribed for this data point
   * and returns the update message.
   *
   * @param subscription subscription containing the vin and data points to check for changes
   * @param timeIntervalUpperLimit the upper limit of the time interval to be checked
   * @returns generated update message
   */
  private async checkForChanges(
    subscription: Subscription,
    timeIntervalUpperLimit: number,
  ): Promise<DataContentMessage | undefined> {
    try {
      const lower = this.timeIntervalLowerLimit;
      if (lower === undefined) return undefined;

      const result = await this.session.getDataPointsInWindow(
        [...subscription.dataPoints],
        subscription.vin,
        lower,
        timeIntervalUpperLimit,
      );

      if (!result.success || result.dataPoints.length === 0) {
        return undefined;
      }

      logMessage(
        `Processed ${result.dataPoints.length} changes for id ${subscription.vin}`,
      );

      return this.createDataContentMessage(
        subscription.vin,
        result.dataPoints,
        subscription.root,
        subscription.format,
        subscription.path,
        result.metadata,
        subscription.requestId,
      );
    } catch (error: unknown) {
      logError("Unknown error", error);
      return undefined;
    }
  }
}
