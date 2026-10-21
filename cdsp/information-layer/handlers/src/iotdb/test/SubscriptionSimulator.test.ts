import { SubscriptionSimulator } from "../src/SubscriptionSimulator";
import { WebSocketWithId } from "../../../../utils/database-params";
import {
  DataContentMessage,
  ErrorMessage,
  StatusMessage,
  SubscribeMessageType,
  UnsubscribeMessageType,
} from "../../../../router/utils/NewMessage";
import { IoTDBSession } from "../src/IoTDBSession";

jest.mock("../config/database-params", () => ({
  ...jest.requireActual("../config/database-params"),
  databaseConfig: {
    iotdbHost: "mock-host",
    iotdbPort: 5678,
    iotdbUser: "mock-user",
    iotdbPassword: "mock-password",
    fetchSize: 2000,
    timeZoneId: "UTC",
    pollIntervalLenInSec: 15, // Mocked interval for the test
  },
}));

// Mock the module where transformSessionDataSet is defined
jest.mock("../utils/database-helper", () => ({
  transformSessionDataSet: jest.fn(), // Ensure transformSessionDataSet is a mock function
}));

describe("SubscriptionSimulator", () => {
  let simulator: SubscriptionSimulator;
  let mockWebSocket: WebSocketWithId;
  let mockSubscribeMessageVIN1: SubscribeMessageType;
  let mockSubscribeMessageVIN2: SubscribeMessageType;
  let mockUnsubscribeMessageVIN1: UnsubscribeMessageType;
  let sendMessageToClientMock: jest.Mock;
  let sendAlreadySubscribedErrorMsg: jest.Mock;
  let mockNewSession: jest.Mocked<IoTDBSession>;

  beforeEach(() => {
    const createStatusMessageMock = jest.fn<
      StatusMessage, // Return type
      [number, string, string] // Parameters: code, message, requestId
    >((code, statusMessage, requestId) => ({
      type: "status",
      code: code,
      message: statusMessage,
      requestId: requestId,
      timestamp: { seconds: 1715253322, nanos: 123000000 }, // Mocked timestamp
    }));

    const createErrorMessageMock = jest.fn<
      ErrorMessage, // Return type
      [number, string, string, string] // Parameters: code, message, reason, requestId
    >((code, statusMessage, reason, requestId) => ({
      type: "error",
      code: code,
      message: statusMessage,
      reason: reason,
      requestId: requestId,
    }));

    mockNewSession = {
      open: jest.fn().mockResolvedValue(undefined),
      close: jest.fn().mockResolvedValue(undefined),
      isOpen: jest.fn().mockReturnValue(true),
      getDataPoints: jest.fn(),
      getDataPointsInWindow: jest.fn().mockResolvedValue({
        success: true,
        dataPoints: [],
        metadata: [],
      }),
      setDataPoints: jest.fn(),
      createDatabaseIfNeeded: jest.fn().mockResolvedValue(undefined),
      subscribeDataPoints: jest.fn(),
      unsubscribeDataPoints: jest.fn(),
    } as unknown as jest.Mocked<IoTDBSession>;

    sendAlreadySubscribedErrorMsg = jest.fn();
    sendMessageToClientMock = jest.fn();
    simulator = new SubscriptionSimulator(
      mockNewSession,
      sendMessageToClientMock,
      jest.fn(),
      createStatusMessageMock,
      createErrorMessageMock,
      sendAlreadySubscribedErrorMsg,
    );

    mockWebSocket = { id: "ws1" } as WebSocketWithId;
    mockSubscribeMessageVIN1 = {
      instance: "VIN_1",
      requestId: "requestId1",
      path: "Vehicle_CurrentLocation",
      root: "relative",
      format: "flat",
      type: "subscribe",
    } as SubscribeMessageType;

    mockSubscribeMessageVIN2 = {
      instance: "VIN_2",
      requestId: "requestId2",
      path: "Vehicle_CurrentLocation",
      root: "relative",
      format: "flat",
      type: "subscribe",
    } as SubscribeMessageType;

    mockUnsubscribeMessageVIN1 = {
      instance: "VIN_1",
      requestId: "requestId3",
    } as UnsubscribeMessageType;

    const subscription = {
      vin: "VIN_1",
      dataPoints: new Set<string>(["datapoint1", "datapoint2"]),
      requestId: "requestId1",
      path: "Vehicle_CurrentLocation",
      root: "relative" as const,
      format: "flat" as const,
    };
    simulator["websocketToSubscriptionsMap"].set(mockWebSocket, [subscription]);
  });

  afterEach(() => {
    jest.clearAllMocks();
    if (simulator["intervalId"]) {
      clearInterval(simulator["intervalId"]);
      simulator["intervalId"] = null; // Reset intervalId
    }
    jest.clearAllTimers(); // Clear any remaining active timers
    process.removeAllListeners("exit");
    process.removeAllListeners("SIGINT");
  });

  describe("subscribe", () => {
    test("does not send already-subscribed error on first subscribe", () => {
      void simulator.subscribe(mockSubscribeMessageVIN1, mockWebSocket, [
        "datapoint1",
      ]);

      expect(sendAlreadySubscribedErrorMsg).not.toHaveBeenCalled();
    });

    test("creates a new subscription if client is not subscribed to provided VIN", () => {
      void simulator.subscribe(mockSubscribeMessageVIN2, mockWebSocket, [
        "datapoint1",
        "datapoint2",
      ]);

      expect(simulator["websocketToSubscriptionsMap"].size).toBe(1);
      const subscriptions =
        simulator["websocketToSubscriptionsMap"].get(mockWebSocket);
      expect(subscriptions).toBeDefined();
      expect(subscriptions?.length).toBe(2);
      expect(
        subscriptions?.find(
          (subscription) =>
            subscription.vin === mockSubscribeMessageVIN2.instance,
        )?.dataPoints.size,
      ).toBe(2);
    });

    test("replaces existing subscription when requestId matches", () => {
      const replacementMessage = {
        ...mockSubscribeMessageVIN1,
        instance: "VIN_1",
        requestId: "requestId1",
        path: "Vehicle_CurrentLocation_Latitude",
        root: "absolute",
        format: "nested",
      } as SubscribeMessageType;

      // Subscribe with the replacement message
      void simulator.subscribe(replacementMessage, mockWebSocket, [
        "datapointNew",
      ]);

      const subscriptions =
        simulator["websocketToSubscriptionsMap"].get(mockWebSocket);
      if (!subscriptions) {
        throw new Error("Subscriptions should be defined");
      }
      const replaced = subscriptions.find(
        (sub) => sub.requestId === "requestId1",
      );

      expect(replaced).toBeDefined();
      expect(replaced?.vin).toBe("VIN_1");
      expect(replaced?.path).toBe("Vehicle_CurrentLocation_Latitude");
      expect(replaced?.root).toBe("absolute");
      expect(replaced?.format).toBe("nested");
      expect(replaced?.dataPoints.size).toBe(1);
      expect(replaced?.dataPoints.has("datapointNew")).toBeTruthy();
    });

    test("updates subscription datapoints when requestId matches", () => {
      void simulator.subscribe(mockSubscribeMessageVIN1, mockWebSocket, [
        "datapointNew",
        "datapoint2",
      ]);

      expect(simulator["websocketToSubscriptionsMap"].size).toBe(1);
      const subscriptions =
        simulator["websocketToSubscriptionsMap"].get(mockWebSocket);
      expect(subscriptions).toBeDefined();
      expect(subscriptions?.length).toBe(1);
      expect(
        subscriptions?.find(
          (subscription) =>
            subscription.vin === mockSubscribeMessageVIN1.instance,
        )?.dataPoints.size,
      ).toBe(2);
    });

    test("starts a periodic database listener if no intervalId is set", () => {
      simulator["intervalId"] = null;

      void simulator.subscribe(mockSubscribeMessageVIN2, mockWebSocket, [
        "datapoint1",
        "datapoint2",
      ]);

      expect(simulator["intervalId"]).not.toBeNull();
    });
  });

  describe("unsubscribe", () => {
    test("remove subscription with exact matching datapoints", () => {
      const subscriptions =
        simulator["websocketToSubscriptionsMap"].get(mockWebSocket);
      if (!subscriptions) {
        throw new Error("Subscriptions should be defined");
      }
      subscriptions.push({
        vin: "VIN_1",
        dataPoints: new Set<string>(["datapoint3", "datapoint4"]),
        requestId: "requestId2",
        path: "Vehicle_Speed",
        root: "relative" as const,
        format: "flat" as const,
      });

      // Unsubscribe with datapoints that match the first mocked subscription
      simulator.unsubscribe(mockUnsubscribeMessageVIN1, mockWebSocket, [
        "datapoint1",
        "datapoint2",
      ]);

      expect(simulator["websocketToSubscriptionsMap"].size).toBe(1);
      const remainingSubscriptions =
        simulator["websocketToSubscriptionsMap"].get(mockWebSocket);
      expect(remainingSubscriptions).toBeDefined();
      expect(remainingSubscriptions?.length).toBe(1);
      expect(
        remainingSubscriptions?.find((sub) => sub.requestId === "requestId2"),
      ).toBeDefined();
      expect(
        remainingSubscriptions?.find((sub) => sub.requestId === "requestId1"),
      ).toBeUndefined();
    });

    test("remove multiple subscriptions if they have exact matching datapoints", () => {
      const subscriptions =
        simulator["websocketToSubscriptionsMap"].get(mockWebSocket);
      if (!subscriptions) {
        throw new Error("Subscriptions should be defined");
      }
      subscriptions.push({
        vin: "VIN_1",
        dataPoints: new Set<string>(["datapoint1", "datapoint2"]),
        requestId: "requestId3",
        path: "Vehicle_CurrentLocation",
        root: "relative" as const,
        format: "flat" as const,
      });

      simulator.unsubscribe(mockUnsubscribeMessageVIN1, mockWebSocket, [
        "datapoint1",
        "datapoint2",
      ]);

      expect(simulator["websocketToSubscriptionsMap"].size).toBe(0);
    });

    test("calls sendMessageToClient if unsubscribe datapoints do not match exactly", () => {
      simulator.unsubscribe(mockUnsubscribeMessageVIN1, mockWebSocket, [
        "datapointUnknown",
      ]);

      expect(sendMessageToClientMock).toHaveBeenCalledWith(
        mockWebSocket,
        expect.objectContaining({
          type: "error",
          code: 404,
          requestId: "requestId3",
          message: expect.stringContaining("Subscription not found"),
          reason: expect.stringContaining(
            "Cannot unsubscribe. No subscription for instance 'VIN_1' with the provided data points.",
          ),
        }),
      );
    });

    test("calls sendMessageToClient if unsubscribe with partial datapoints", () => {
      simulator.unsubscribe(mockUnsubscribeMessageVIN1, mockWebSocket, [
        "datapoint1",
      ]);

      expect(sendMessageToClientMock).toHaveBeenCalledWith(
        mockWebSocket,
        expect.objectContaining({
          type: "error",
          code: 404,
          requestId: "requestId3",
          message: expect.stringContaining("Subscription not found"),
          reason: expect.stringContaining(
            "Cannot unsubscribe. No subscription for instance 'VIN_1' with the provided data points.",
          ),
        }),
      );
    });

    test("stops the timer if there is no subscription left", () => {
      void simulator.subscribe(mockSubscribeMessageVIN1, mockWebSocket, [
        "datapoint1",
        "datapoint2",
      ]);
      expect(simulator["intervalId"]).not.toBeNull();

      simulator.unsubscribe(mockUnsubscribeMessageVIN1, mockWebSocket, [
        "datapoint1",
        "datapoint2",
      ]);

      expect(simulator["intervalId"]).toBeNull();
    });

    test("does not stop timer if other subscriptions exist", () => {
      void simulator.subscribe(mockSubscribeMessageVIN2, mockWebSocket, [
        "datapoint3",
      ]);
      const intervalId = simulator["intervalId"];
      expect(intervalId).not.toBeNull();

      simulator.unsubscribe(mockUnsubscribeMessageVIN1, mockWebSocket, [
        "datapoint1",
        "datapoint2",
      ]);

      expect(simulator["intervalId"]).toBe(intervalId);
      const subscriptions =
        simulator["websocketToSubscriptionsMap"].get(mockWebSocket);
      if (!subscriptions) {
        throw new Error("Subscriptions should be defined");
      }
      expect(subscriptions.length).toBe(1);
      expect(subscriptions.find((sub) => sub.vin === "VIN_2")).toBeDefined();
    });

    test("returns error when unsubscribing from non-existent VIN", () => {
      const nonExistentVINMessage = {
        instance: "VIN_NONEXISTENT",
        requestId: "requestId5",
      } as UnsubscribeMessageType;

      simulator.unsubscribe(nonExistentVINMessage, mockWebSocket, [
        "datapoint1",
      ]);

      expect(sendMessageToClientMock).toHaveBeenCalledWith(
        mockWebSocket,
        expect.objectContaining({
          type: "error",
          code: 404,
          requestId: "requestId5",
          message: expect.stringContaining("Subscription not found"),
          reason: expect.stringContaining(
            "Cannot unsubscribe. No subscription for instance 'VIN_NONEXISTENT' with the provided data points.",
          ),
        }),
      );
    });

    test("returns error when unsubscribing from non-existent websocket", () => {
      const nonExistentWebSocket = { id: "ws_unknown" } as WebSocketWithId;

      simulator.unsubscribe(mockUnsubscribeMessageVIN1, nonExistentWebSocket, [
        "datapoint1",
      ]);

      expect(sendMessageToClientMock).toHaveBeenCalledWith(
        nonExistentWebSocket,
        expect.objectContaining({
          type: "error",
          code: 404,
          requestId: "requestId3",
          message: expect.stringContaining("Subscription not found"),
          reason: expect.stringContaining(
            "Cannot unsubscribe. No subscription for instance 'VIN_1'",
          ),
        }),
      );
    });
  });

  describe("notifyDatabaseChanges", () => {
    test("timeIntervalUpperLimit and timeIntervalLowerLimit form a series", async () => {
      simulator["timeIntervalLowerLimit"] = Date.now();

      // Wait a little to ensure a time difference in the next call
      await new Promise((resolve) => setTimeout(resolve, 10));

      // Spy on `checkForChanges`
      const timeIntervals: { lower: number; upper: number }[] = [];

      (
        jest.spyOn(simulator as any, "checkForChanges") as jest.Mock
      ).mockImplementation((subscription: any, upperLimit: number) => {
        const lowerLimit = simulator["timeIntervalLowerLimit"];
        if (lowerLimit !== undefined) {
          timeIntervals.push({
            lower: lowerLimit, // Current lower limit
            upper: upperLimit, // Passed upper limit
          });
        }
        return Promise.resolve(undefined); // Simulate checkForChanges returning no updates
      });

      // First call
      await (simulator as any).notifyDatabaseChanges();

      // Wait a little to ensure a time difference in the next call
      await new Promise((resolve) => setTimeout(resolve, 10));

      // Second call
      await (simulator as any).notifyDatabaseChanges();

      // Assert that checkForChanges was called twice and time intervals are correct
      expect((simulator as any).checkForChanges).toHaveBeenCalledTimes(2);

      // Expect two interval records (one for each call)
      expect(timeIntervals.length).toBe(2);

      const { lower: lower1, upper: upper1 } = timeIntervals[0];
      const { lower: lower2, upper: upper2 } = timeIntervals[1];

      // Assertions
      expect(upper1).toBeGreaterThan(lower1); // First interval: upper > lower
      expect(upper2).toBeGreaterThan(lower2); // Second interval: upper > lower
      expect(lower2).toBe(upper1); // Second interval's lower is the first interval's upper
    });

    test("calls sendMessageToClient if there is a matching subscription and update", async () => {
      simulator["timeIntervalLowerLimit"] = Date.now();

      // Mock checkForChanges to return an update message
      jest.spyOn(simulator as any, "checkForChanges").mockResolvedValueOnce({
        type: "data",
        instance: "VIN_1",
        schema: "Vehicle",
        data: [
          { "CurrentLocation.Latitude": 55, "CurrentLocation.Longitude": 66 },
        ],
      } as DataContentMessage);

      // Call notifyDatabaseChanges
      await (simulator as any).notifyDatabaseChanges();

      // Assert that sendMessageToClient was called with the matching websocket and update message
      expect(sendMessageToClientMock).toHaveBeenCalledWith(
        { id: "ws1" }, // WebSocket in the subscription
        expect.objectContaining({
          instance: "VIN_1",
          schema: "Vehicle",
          type: "data",
          data: [
            {
              "CurrentLocation.Latitude": 55,
              "CurrentLocation.Longitude": 66,
            },
          ],
        }),
      );
    });
  });
});
