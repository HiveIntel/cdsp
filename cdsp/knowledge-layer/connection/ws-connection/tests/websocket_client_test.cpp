#include "websocket_client.h"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace websocket_client {
namespace {

class LoopbackWebSocketServer {
   public:
    enum class Mode {
        EchoTwoMessages,
        EchoOneMessageThenClose,
        SendBinaryMessage,
        StallHandshake,
    };

    explicit LoopbackWebSocketServer(const Mode mode)
        : acceptor_(io_context_,
                    boost::asio::ip::tcp::endpoint{
                        boost::asio::ip::address_v4::loopback(), 0}),
          port_(acceptor_.local_endpoint().port()),
          mode_(mode),
          thread_([this] { serve(); }) {}

    LoopbackWebSocketServer(const LoopbackWebSocketServer&) = delete;
    LoopbackWebSocketServer& operator=(const LoopbackWebSocketServer&) = delete;

    ~LoopbackWebSocketServer() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    unsigned short port() const noexcept { return port_; }

    const std::vector<std::string>& receivedMessages() const noexcept {
        return received_messages_;
    }

    void wait() {
        if (thread_.joinable()) {
            thread_.join();
        }
        if (failure_) {
            std::rethrow_exception(failure_);
        }
    }

   private:
    void serve() noexcept {
        try {
            boost::asio::ip::tcp::socket socket{io_context_};
            acceptor_.accept(socket);

            if (mode_ == Mode::StallHandshake) {
                drainUntilClosed(socket);
                return;
            }

            boost::beast::websocket::stream<boost::asio::ip::tcp::socket>
                websocket{std::move(socket)};
            auto timeout =
                boost::beast::websocket::stream_base::timeout::suggested(
                    boost::beast::role_type::server);
            timeout.handshake_timeout = std::chrono::seconds{5};
            timeout.idle_timeout = std::chrono::seconds{15};
            websocket.set_option(timeout);
            websocket.accept();

            switch (mode_) {
                case Mode::EchoTwoMessages:
                    echoMessages(websocket, 2);
                    waitForPeerClose(websocket);
                    break;
                case Mode::EchoOneMessageThenClose:
                    echoMessages(websocket, 1);
                    websocket.close(
                        boost::beast::websocket::close_code::normal);
                    break;
                case Mode::SendBinaryMessage:
                    sendBinaryMessage(websocket);
                    waitForPeerClose(websocket);
                    break;
                case Mode::StallHandshake:
                    break;
            }
        } catch (...) {
            failure_ = std::current_exception();
        }
    }

    static void drainUntilClosed(boost::asio::ip::tcp::socket& socket) {
        std::array<char, 1024> bytes{};
        boost::system::error_code error_code;
        while (!error_code) {
            socket.read_some(boost::asio::buffer(bytes), error_code);
        }

        if (error_code != boost::asio::error::eof &&
            error_code != boost::asio::error::connection_reset &&
            error_code != boost::asio::error::operation_aborted) {
            throw boost::system::system_error{error_code};
        }
    }

    void echoMessages(
        boost::beast::websocket::stream<boost::asio::ip::tcp::socket>&
            websocket,
        const std::size_t message_count) {
        for (std::size_t index = 0; index < message_count; ++index) {
            boost::beast::flat_buffer buffer;
            websocket.read(buffer);
            if (!websocket.got_text()) {
                throw std::runtime_error("Expected a text WebSocket message.");
            }

            std::string message =
                boost::beast::buffers_to_string(buffer.data());
            received_messages_.push_back(message);
            websocket.text(true);
            websocket.write(boost::asio::buffer(message));
        }
    }

    static void sendBinaryMessage(
        boost::beast::websocket::stream<boost::asio::ip::tcp::socket>&
            websocket) {
        const std::string payload{"binary-payload"};
        websocket.binary(true);
        websocket.write(boost::asio::buffer(payload));
    }

    static void waitForPeerClose(
        boost::beast::websocket::stream<boost::asio::ip::tcp::socket>&
            websocket) {
        boost::beast::flat_buffer buffer;
        boost::beast::error_code error_code;
        websocket.read(buffer, error_code);
        if (error_code != boost::beast::websocket::error::closed) {
            throw boost::system::system_error{error_code};
        }
    }

    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    unsigned short port_;
    Mode mode_;
    std::thread thread_;
    std::vector<std::string> received_messages_;
    std::exception_ptr failure_;
};

TEST(WebSocketClientConfigurationTest, RejectsNonPositiveConnectionTimeout) {
    const auto client = createWebSocketClient();

    EXPECT_THROW(client->setConnectionTimeout(std::chrono::seconds{0}),
                 std::invalid_argument);
    EXPECT_THROW(client->setConnectionTimeout(std::chrono::seconds{-1}),
                 std::invalid_argument);
    EXPECT_NO_THROW(client->setConnectionTimeout(std::chrono::seconds{4}));
}

TEST(WebSocketClientConfigurationTest, UsesTenSecondTimeoutByDefault) {
    LoopbackWebSocketServer server{
        LoopbackWebSocketServer::Mode::StallHandshake};
    const auto client = createWebSocketClient();
    std::vector<std::string> errors;
    int disconnected_count = 0;
    client->setOnErrorCallback(
        [&errors](const std::string& error) { errors.push_back(error); });
    client->setOnDisconnectedCallback(
        [&disconnected_count] { ++disconnected_count; });

    const auto started_at = std::chrono::steady_clock::now();
    client->connect(
        {"127.0.0.1", std::to_string(server.port()), "/timeout"});
    client->run();
    const auto elapsed = std::chrono::steady_clock::now() - started_at;
    server.wait();

    EXPECT_GE(elapsed, std::chrono::seconds{9});
    EXPECT_LT(elapsed, std::chrono::seconds{20});
    EXPECT_FALSE(errors.empty());
    EXPECT_EQ(disconnected_count, 1);
}

TEST(WebSocketClientCallbackTest, InvokesCallbacksAndSendsInFifoOrder) {
    LoopbackWebSocketServer server{
        LoopbackWebSocketServer::Mode::EchoTwoMessages};
    auto client = createWebSocketClient();
    client->setConnectionTimeout(std::chrono::seconds{3});

    std::vector<std::string> events;
    std::vector<std::string> errors;
    client->setOnConnectedCallback(
        [&events] { events.push_back("connected"); });
    const std::weak_ptr<WebSocketClient> weak_client = client;
    client->setOnMessageCallback(
        [&events, weak_client](const std::string& message) {
            events.push_back("message:" + message);
            if (message == "second") {
                if (const auto locked_client = weak_client.lock()) {
                    locked_client->close();
                }
            }
        });
    client->setOnErrorCallback(
        [&errors](const std::string& error) { errors.push_back(error); });
    client->setOnDisconnectedCallback(
        [&events] { events.push_back("disconnected"); });

    client->connect(
        {"127.0.0.1", std::to_string(server.port()), "/echo"});
    client->send("first");
    client->send("second");
    client->run();
    server.wait();

    EXPECT_TRUE(errors.empty());
    EXPECT_EQ(server.receivedMessages(),
              (std::vector<std::string>{"first", "second"}));
    EXPECT_EQ(events,
              (std::vector<std::string>{"connected", "message:first",
                                        "message:second", "disconnected"}));
}

TEST(WebSocketClientCallbackTest, LogsFallbackEventsWhenCallbacksAreUnset) {
    LoopbackWebSocketServer server{
        LoopbackWebSocketServer::Mode::EchoOneMessageThenClose};
    const auto client = createWebSocketClient();
    client->setConnectionTimeout(std::chrono::seconds{3});

    testing::internal::CaptureStdout();
    client->connect(
        {"127.0.0.1", std::to_string(server.port()), "/fallback"});
    client->send("payload");
    client->run();
    const std::string output = testing::internal::GetCapturedStdout();
    server.wait();

    EXPECT_EQ(output,
              "[WebSocket] Connected\n"
              "[WebSocket] Message: payload\n"
              "[WebSocket] Disconnected\n");
}

TEST(WebSocketClientCallbackTest, LogsFallbackErrorForCustomTimeout) {
    LoopbackWebSocketServer server{
        LoopbackWebSocketServer::Mode::StallHandshake};
    const auto client = createWebSocketClient();
    client->setConnectionTimeout(std::chrono::seconds{1});

    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    const auto started_at = std::chrono::steady_clock::now();
    client->connect(
        {"127.0.0.1", std::to_string(server.port()), "/timeout"});
    client->run();
    const auto elapsed = std::chrono::steady_clock::now() - started_at;
    const std::string error_output = testing::internal::GetCapturedStderr();
    const std::string standard_output = testing::internal::GetCapturedStdout();
    server.wait();

    EXPECT_GE(elapsed, std::chrono::milliseconds{900});
    EXPECT_LT(elapsed, std::chrono::seconds{5});
    EXPECT_NE(error_output.find("[WebSocket] Error: "), std::string::npos);
    EXPECT_EQ(standard_output, "[WebSocket] Disconnected\n");
}

TEST(WebSocketClientLifecycleTest, RejectsBinaryMessageAndDisconnectsOnce) {
    LoopbackWebSocketServer server{
        LoopbackWebSocketServer::Mode::SendBinaryMessage};
    const auto client = createWebSocketClient();
    client->setConnectionTimeout(std::chrono::seconds{3});

    int connected_count = 0;
    int message_count = 0;
    int disconnected_count = 0;
    std::vector<std::string> errors;
    client->setOnConnectedCallback([&connected_count] { ++connected_count; });
    client->setOnMessageCallback(
        [&message_count](const std::string&) { ++message_count; });
    client->setOnErrorCallback(
        [&errors](const std::string& error) { errors.push_back(error); });
    client->setOnDisconnectedCallback(
        [&disconnected_count] { ++disconnected_count; });

    client->connect(
        {"127.0.0.1", std::to_string(server.port()), "/binary"});
    client->run();
    server.wait();

    EXPECT_EQ(connected_count, 1);
    EXPECT_EQ(message_count, 0);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors.front(),
              "read: binary WebSocket messages are not supported");
    EXPECT_EQ(disconnected_count, 1);
}

TEST(WebSocketClientLifecycleTest, RejectsOperationsInInvalidOrder) {
    const auto client = createWebSocketClient();

    EXPECT_THROW(client->run(), std::logic_error);
    EXPECT_THROW(client->send("payload"), std::logic_error);
    EXPECT_THROW(client->connect({"", "8080", "/"}),
                 std::invalid_argument);
    EXPECT_THROW(client->connect({"localhost", "", "/"}),
                 std::invalid_argument);
}

TEST(WebSocketClientLifecycleTest, RejectsInvalidTargetAndSecondConnect) {
    {
        const auto client = createWebSocketClient();
        EXPECT_THROW(client->connect({"localhost", "8080", "ws"}),
                     std::invalid_argument);
    }

    {
        const auto client = createWebSocketClient();
        client->connect({"localhost", "8080", "/"});
        EXPECT_THROW(client->connect({"localhost", "8080", "/"}),
                     std::logic_error);
    }
}

TEST(WebSocketClientLifecycleTest, CloseIsIdempotent) {
    const auto client = createWebSocketClient();

    EXPECT_NO_THROW(client->close());
    EXPECT_NO_THROW(client->close());
    EXPECT_THROW(client->connect({"localhost", "8080", "/"}),
                 std::logic_error);
}

}  // namespace
}  // namespace websocket_client
