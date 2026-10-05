#ifndef WS_CONNECTION_WEBSOCKET_CLIENT_H
#define WS_CONNECTION_WEBSOCKET_CLIENT_H

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace websocket_client {

/**
 * @brief Address of a plain-text WebSocket server.
 *
 * Version one supports `ws://` only. `target` is the HTTP request target used
 * during the WebSocket handshake.
 */
struct Endpoint {
    std::string host;
    std::string port;
    std::string target{"/"};
};

using OnConnectedCallback = std::function<void()>;
using OnMessageCallback = std::function<void(const std::string&)>;
using OnErrorCallback = std::function<void(const std::string&)>;
using OnDisconnectedCallback = std::function<void()>;

/**
 * @brief Asynchronous, text-only WebSocket client interface.
 *
 * Configure callbacks and the connection timeout before calling `connect`.
 * Then execute `run` on one or more caller-owned threads. `send` and `close`
 * are safe to call from other threads. The implementation serializes all
 * WebSocket stream access internally.
 *
 * The caller must retain the client until `run` returns. Reconnection, binary
 * messages, TLS, and automatic retry are intentionally outside this contract.
 * Callbacks run serially on the event loop and must not throw or block for long
 * periods.
 */

class WebSocketClient {
   public:
    virtual ~WebSocketClient() = default;

    /// Sets the resolve/connect/handshake timeout. The default is 10 seconds.
    virtual void setConnectionTimeout(std::chrono::seconds timeout) = 0;

    virtual void setOnConnectedCallback(OnConnectedCallback callback) = 0;
    virtual void setOnMessageCallback(OnMessageCallback callback) = 0;
    virtual void setOnErrorCallback(OnErrorCallback callback) = 0;
    virtual void setOnDisconnectedCallback(OnDisconnectedCallback callback) = 0;

    /// Starts asynchronous resolve, TCP connect, and WebSocket handshake.
    virtual void connect(Endpoint endpoint) = 0;

    /// Runs the event loop until the connection reaches a terminal state.
    virtual void run() = 0;

    /// Queues one complete WebSocket text message for asynchronous delivery.
    virtual void send(std::string message) = 0;

    /// Starts an asynchronous WebSocket close handshake. This is idempotent.
    virtual void close() = 0;

};

/**
 * @brief Creates the default WebSocket client implementation.
 *
 * Boost.Asio and Boost.Beast are implementation details and are not exposed by
 * the public interface.
 */
[[nodiscard]] std::shared_ptr<WebSocketClient> createWebSocketClient();

}  // namespace websocket_client

#endif  // WS_CONNECTION_WEBSOCKET_CLIENT_H
