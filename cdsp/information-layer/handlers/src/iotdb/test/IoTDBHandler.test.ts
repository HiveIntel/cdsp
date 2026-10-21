import { IoTDBHandler } from "../src/IoTDBHandler";
import { WebSocketWithId } from "../../../../utils/database-params";
import {
  NewMessageType,
  SetMessageType,
  STATUS_SUCCESS,
  GetMessageType,
  SubscribeMessageType,
  UnsubscribeMessageType,
  STATUS_ERRORS,
} from "../../../../router/utils/NewMessage";
import { SupportedMessageDataTypes } from "../utils/iotdb-constants";

jest.mock("../src/SubscriptionSimulator", () => ({
  getSubscriptionSimulator: jest.fn(() => ({
    subscribe: jest.fn().mockResolvedValue(undefined),
    unsubscribe: jest.fn(),
    unsubscribeClient: jest.fn(),
  })),
}));
jest.mock("../src/IoTDBSession");
jest.mock("../utils/database-helper", () => ({
  transformSessionDataSet: jest.fn(() => [
    [{ name: "datapoint", value: 1 }],
    [{ name: "metadata", value: 2 }],
  ]),
}));
jest.mock("../config/database-params", () => ({
  databaseConfig: {},
  databaseParams: {
    VSS: {
      databaseName: "test_db",
      dataPointId: "Vehicle_VehicleIdentification_VIN",
    },
  },
}));

describe("IoTDBHandler", () => {
  let handler: IoTDBHandler;
  let mockWebSocket: jest.Mocked<WebSocketWithId>;
  let mockSendMessage: jest.Mock;

  const buildGetMessage = (): GetMessageType =>
    ({
      type: NewMessageType.Get,
      instance: "TEST_VIN",
      path: "Vehicle",
      requestId: "req-get-1",
      root: "absolute",
      format: "flat",
    }) as GetMessageType;

  const buildSetMessage = (): SetMessageType =>
    ({
      type: NewMessageType.Set,
      instance: "TEST_VIN",
      path: "Vehicle",
      requestId: "req-set-id",
      data: { Speed: 60 },
      metadata: { Speed: { unit: "km/h" } },
    }) as unknown as SetMessageType;

  const buildSubscribeMessage = (): SubscribeMessageType =>
    ({
      type: NewMessageType.Subscribe,
      instance: "TEST_VIN",
      path: "Vehicle",
      requestId: "req-sub-1",
    }) as unknown as SubscribeMessageType;

  const buildUnsubscribeMessage = (): UnsubscribeMessageType =>
    ({
      type: NewMessageType.Unsubscribe,
      instance: "TEST_VIN",
      path: "Vehicle",
      requestId: "req-unsub-1",
    }) as unknown as UnsubscribeMessageType;

  beforeEach(() => {
    jest.clearAllMocks();

    mockSendMessage = jest.fn();
    handler = new IoTDBHandler(mockSendMessage);

    mockWebSocket = { id: "test-socket" } as jest.Mocked<WebSocketWithId>;
  });

  describe("lifecycle", () => {
    test("should authenticate and connect to IoTDB", async () => {
      // Act
      await handler.authenticateAndConnect();

      // Assert
      expect((handler as any).session.open).toHaveBeenCalled();
    });

    test("unsubscribeClient should cleanup simulator and close both sessions", async () => {
      // Arrange
      const unsubscribeClientSpy = jest.spyOn(
        (handler as any).subscriptionSimulator,
        "unsubscribeClient",
      );
      const closeSpy = jest
        .spyOn((handler as any).session, "close")
        .mockResolvedValue(undefined);

      // Act
      await (handler as any).unsubscribeClient(mockWebSocket);

      // Assert
      expect(unsubscribeClientSpy).toHaveBeenCalledWith(mockWebSocket);
      expect(closeSpy).toHaveBeenCalledTimes(1);
    });
  });

  describe("get flow", () => {
    test("should delegate getDataPointsFromDB to session", async () => {
      const mockDataPoints = ["Temperature", "Speed"];
      const vin = "TEST_VIN";
      const queryResult = { success: true, dataPoints: [], metadata: [] };

      const getDataPointsSpy = jest
        .spyOn((handler as any).session, "getDataPoints")
        .mockResolvedValue(queryResult);

      const result = await handler.getDataPointsFromDB(mockDataPoints, vin);

      expect(getDataPointsSpy).toHaveBeenCalledWith(mockDataPoints, vin);
      expect(result).toEqual(queryResult);
    });

    test("getData should send datapoints-not-found when no datapoints are known", async () => {
      // Arrange
      jest
        .spyOn(handler as any, "getKnownDatapointsByPrefix")
        .mockReturnValue([]);

      const notFoundSpy = jest.spyOn(
        handler as any,
        "sendRequestedDataPointsNotFoundErrorMsg",
      );

      const msg = buildGetMessage();

      // Act
      await (handler as any).getData(msg, mockWebSocket);

      // Assert
      expect(notFoundSpy).toHaveBeenCalledWith(
        mockWebSocket,
        msg.path,
        msg.requestId,
      );
    });

    test("getData should send failure response when new session open fails", async () => {
      // Arrange
      const msg = buildGetMessage();

      jest
        .spyOn(handler as any, "getKnownDatapointsByPrefix")
        .mockReturnValue(["Vehicle.Speed"]);

      (handler as any).session = {
        isOpen: jest.fn().mockReturnValue(false),
        open: jest.fn().mockRejectedValue(new Error("open failed")),
        getDataPoints: jest.fn(),
        close: jest.fn(),
      };

      const sendGetSpy = jest.spyOn(handler as any, "sendGetResponseToClient");

      // Act
      await (handler as any).getData(msg, mockWebSocket);

      // Assert
      expect(sendGetSpy).toHaveBeenCalledWith(
        { success: false, error: "Session unavailable" },
        msg.instance,
        [],
        mockWebSocket,
        msg.requestId,
        msg.path,
        msg.root,
        msg.format,
      );
    });

    test("getData should forward successful query result via sendGetResponseToClient", async () => {
      // Arrange
      const msg = buildGetMessage();
      const knownDatapoints = ["Vehicle.Speed"];
      const queryResult = {
        success: true,
        dataPoints: [{ name: "Vehicle.Speed", value: 88 }],
        metadata: [{ name: "timestamp", value: 123456789 }],
      };

      jest
        .spyOn(handler as any, "getKnownDatapointsByPrefix")
        .mockReturnValue(knownDatapoints);

      (handler as any).session = {
        isOpen: jest.fn().mockReturnValue(true),
        open: jest.fn(),
        getDataPoints: jest.fn().mockResolvedValue(queryResult),
        close: jest.fn(),
      };

      const sendGetSpy = jest.spyOn(handler as any, "sendGetResponseToClient");

      // Act
      await (handler as any).getData(msg, mockWebSocket);

      // Assert
      expect((handler as any).session.getDataPoints).toHaveBeenCalledWith(
        knownDatapoints,
        msg.instance,
      );

      expect(sendGetSpy).toHaveBeenCalledWith(
        queryResult,
        msg.instance,
        knownDatapoints,
        mockWebSocket,
        msg.requestId,
        msg.path,
        msg.root,
        msg.format,
      );
    });

    test("getData should forward error query result via sendGetResponseToClient", async () => {
      // Arrange
      const msg = buildGetMessage();
      const knownDatapoints = ["Vehicle.Speed"];
      const queryResult = { success: false, error: "db error" };

      jest
        .spyOn(handler as any, "getKnownDatapointsByPrefix")
        .mockReturnValue(knownDatapoints);

      (handler as any).session = {
        isOpen: jest.fn().mockReturnValue(true),
        open: jest.fn(),
        getDataPoints: jest.fn().mockResolvedValue(queryResult),
        close: jest.fn(),
      };

      const sendGetSpy = jest.spyOn(handler as any, "sendGetResponseToClient");

      // Act
      await (handler as any).getData(msg, mockWebSocket);

      // Assert
      expect(sendGetSpy).toHaveBeenCalledWith(
        queryResult,
        msg.instance,
        knownDatapoints,
        mockWebSocket,
        msg.requestId,
        msg.path,
        msg.root,
        msg.format,
      );
    });
  });

  describe("set flow", () => {
    test("setData should send STATUS_SUCCESS.OK on successful write", async () => {
      // Arrange
      jest.spyOn(handler as any, "areNodesValid").mockReturnValue(true);

      jest
        .spyOn(handler as any, "extractNodesFromMessageWithVinAsNode")
        .mockReturnValue({
          Vehicle_Speed: 60,
          Vehicle_VehicleIdentification_VIN: "TEST_VIN",
        });

      jest.spyOn(handler as any, "extractNodesFromMetadata").mockReturnValue({
        Vehicle_Speed_Metadata: JSON.stringify({ unit: "km/h" }),
      });

      jest
        .spyOn(handler as any, "getDataType")
        .mockImplementation((key: unknown) => {
          if (String(key).endsWith("_Metadata")) {
            return SupportedMessageDataTypes.string;
          } else if (String(key).includes("VIN")) {
            return SupportedMessageDataTypes.string;
          } else {
            return SupportedMessageDataTypes.int16;
          }
        });

      (handler as any).session = {
        isOpen: jest.fn().mockReturnValue(true),
        open: jest.fn(),
        close: jest.fn(),
        setDataPoints: jest
          .fn()
          .mockResolvedValue({ success: true, status: { code: 200 } }),
      };

      // Act
      await (handler as any).setData(buildSetMessage(), mockWebSocket);

      // Assert
      expect((handler as any).session.setDataPoints).toHaveBeenCalled();
      expect(mockSendMessage).toHaveBeenCalledWith(
        mockWebSocket,
        expect.objectContaining({ code: STATUS_SUCCESS.OK }),
      );
    });

    test("setData should send SERVICE_UNAVAILABLE on write failure", async () => {
      // Arrange
      const msg = {
        ...buildSetMessage(),
        requestId: "req-set-fail",
        metadata: {},
      };

      jest.spyOn(handler as any, "areNodesValid").mockReturnValue(true);

      jest
        .spyOn(handler as any, "extractNodesFromMessageWithVinAsNode")
        .mockReturnValue({
          Vehicle_Speed: 60,
          Vehicle_VehicleIdentification_VIN: "TEST_VIN",
        });

      jest
        .spyOn(handler as any, "extractNodesFromMetadata")
        .mockReturnValue({});

      jest
        .spyOn(handler as any, "getDataType")
        .mockImplementation((key: unknown) => {
          if (String(key).includes("VIN")) {
            return SupportedMessageDataTypes.string;
          } else {
            return SupportedMessageDataTypes.int16;
          }
        });

      (handler as any).session = {
        isOpen: jest.fn().mockReturnValue(true),
        open: jest.fn(),
        close: jest.fn(),
        setDataPoints: jest
          .fn()
          .mockResolvedValue({ success: false, error: "write failed" }),
      };

      // Act
      await (handler as any).setData(msg, mockWebSocket);

      // Assert
      expect((handler as any).session.setDataPoints).toHaveBeenCalled();
      expect(mockSendMessage).toHaveBeenCalledWith(
        mockWebSocket,
        expect.objectContaining({ code: STATUS_ERRORS.SERVICE_UNAVAILABLE }),
      );
    });
  });

  describe("subscribe/unsubscribe data behavior", () => {
    test("subscribeData should delegate to session when datapoints are known", async () => {
      // Arrange
      const msg = buildSubscribeMessage();
      const knownDatapoints = ["Vehicle_Speed"];

      jest
        .spyOn(handler as any, "getKnownDatapointsByPrefix")
        .mockReturnValue(knownDatapoints);

      const subscribeSpy = jest
        .spyOn((handler as any).session, "subscribeDataPoints")
        .mockResolvedValue(undefined);

      // Act
      await (handler as any).subscribeData(msg, mockWebSocket);

      // Assert
      expect(subscribeSpy).toHaveBeenCalledWith(
        msg,
        mockWebSocket,
        knownDatapoints,
        expect.any(Function),
      );
    });

    test("subscribeData should send datapoints-not-found when no datapoints are known", async () => {
      // Arrange
      const msg = buildSubscribeMessage();

      jest
        .spyOn(handler as any, "getKnownDatapointsByPrefix")
        .mockReturnValue([]);

      const notFoundSpy = jest.spyOn(
        handler as any,
        "sendRequestedDataPointsNotFoundErrorMsg",
      );

      // Act
      await (handler as any).subscribeData(msg, mockWebSocket);

      // Assert
      expect(notFoundSpy).toHaveBeenCalledWith(
        mockWebSocket,
        msg.path,
        msg.requestId,
      );
    });

    test("unsubscribeData should delegate to session when datapoints are known", async () => {
      // Arrange
      const msg = buildUnsubscribeMessage();
      const knownDatapoints = ["Vehicle_Speed"];

      jest
        .spyOn(handler as any, "getKnownDatapointsByPrefix")
        .mockReturnValue(knownDatapoints);

      const unsubscribeSpy = jest
        .spyOn((handler as any).session, "unsubscribeDataPoints")
        .mockResolvedValue(undefined);

      // Act
      await (handler as any).unsubscribeData(msg, mockWebSocket);

      // Assert
      expect(unsubscribeSpy).toHaveBeenCalledWith(
        msg,
        mockWebSocket,
        knownDatapoints,
        expect.any(Function),
      );
    });

    test("unsubscribeData should be idempotent when called repeatedly", async () => {
      // Arrange
      const msg = buildUnsubscribeMessage();
      const knownDatapoints = ["Vehicle_Speed"];

      jest
        .spyOn(handler as any, "getKnownDatapointsByPrefix")
        .mockReturnValue(knownDatapoints);

      const unsubscribeSpy = jest
        .spyOn((handler as any).session, "unsubscribeDataPoints")
        .mockResolvedValue(undefined);

      // Act
      await (handler as any).unsubscribeData(msg, mockWebSocket);
      await (handler as any).unsubscribeData(msg, mockWebSocket);

      // Assert
      expect(unsubscribeSpy).toHaveBeenCalledTimes(2);
      expect(unsubscribeSpy).toHaveBeenNthCalledWith(
        1,
        msg,
        mockWebSocket,
        knownDatapoints,
        expect.any(Function),
      );
      expect(unsubscribeSpy).toHaveBeenNthCalledWith(
        2,
        msg,
        mockWebSocket,
        knownDatapoints,
        expect.any(Function),
      );
    });

    test("unsubscribeData should send datapoints-not-found when no datapoints are known", async () => {
      // Arrange
      const msg = buildUnsubscribeMessage();

      jest
        .spyOn(handler as any, "getKnownDatapointsByPrefix")
        .mockReturnValue([]);

      const notFoundSpy = jest.spyOn(
        handler as any,
        "sendRequestedDataPointsNotFoundErrorMsg",
      );

      // Act
      await (handler as any).unsubscribeData(msg, mockWebSocket);

      // Assert
      expect(notFoundSpy).toHaveBeenCalledWith(
        mockWebSocket,
        msg.path,
        msg.requestId,
      );
    });
  });

  describe("helper methods", () => {
    test("should correctly format metadata", () => {
      // Arrange
      const mockMessage: SetMessageType = {
        instance: "",
        type: NewMessageType.Set,
        path: "Vehicle.Speed",
        requestId: "123",
        data: {},
        metadata: { some: "123456", other: "abc" },
      };

      // Act
      const result = (handler as any).extractNodesFromMetadata(mockMessage);

      // Assert
      expect(result).toEqual({
        "Vehicle.Speed_some_Metadata": JSON.stringify("123456"),
        "Vehicle.Speed_other_Metadata": JSON.stringify("abc"),
      });
    });
  });
});
