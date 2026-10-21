#ifndef WS_CONNECTION_WEBSOCKET_CLIENT_H
#define WS_CONNECTION_WEBSOCKET_CLIENT_H

/**
 * @file websocket_client.h
 * @brief Transport interface for a single asynchronous, non-TLS WebSocket connection.
 */

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace websocket_client {

/**
 * @brief Resolver inputs and HTTP request target for a non-TLS WebSocket server.
 *
 * Supply the address as separate fields, for example {"localhost", "8080", "/events"}.
 * Only `ws://` connections are supported; this structure does not parse a URL.
 */
struct Endpoint {
    /// Non-empty DNS name or IP address, without a scheme, port, or IPv6 brackets.
    std::string host;
    /// Non-empty port number or service name accepted by the TCP resolver.
    std::string port;
    /// URI-encoded HTTP handshake target; include any query string and exclude spaces/control
    /// bytes. An empty value becomes "/"; this client checks the leading '/' but performs no URI
    /// encoding.
    std::string target{"/"};
};

/// Called after the WebSocket handshake succeeds, before messages are delivered.
using OnConnectedCallback = std::function<void()>;
/// Receives one complete text message; the borrowed reference is valid only during the call.
using OnMessageCallback = std::function<void(const std::string&)>;
/// Receives an operation-prefixed diagnostic; copy it if it must outlive the callback.
using OnErrorCallback = std::function<void(const std::string&)>;
/// Called at most once when the client becomes terminal on the loop, even if establishment failed.
using OnDisconnectedCallback = std::function<void()>;

/**
 * @brief Asynchronous, text-only WebSocket client for a single connection attempt.
 *
 * Configure callbacks and the timeout, call connect(), then execute run() on one
 * or more caller-owned threads. Each instance supports a single connection
 * attempt; reconnection, TLS, binary messages, and automatic retry are unsupported.
 *
 * @par Threading
 * Configuration setters require exclusive access before connect(). Once configured,
 * send() and close() may be called from other threads or from callbacks. Stream
 * operations and callbacks are serialized internally, although a read and a write
 * may both be outstanding. With multiple run() threads, successive callbacks may
 * execute on different threads. Callbacks must not throw, block for long periods,
 * or call run() recursively; callback exceptions propagate out of run().
 *
 * @par Ownership
 * Retain the client until all run() calls return, and keep callback dependencies
 * alive for that duration. Prefer a weak_ptr when a stored callback refers to its
 * own client, to avoid a shared-ownership cycle. Message and error references are
 * borrowed for the duration of their callback only.
 */
class WebSocketClient {
   public:
    virtual ~WebSocketClient() = default;

    /**
     * @brief Sets the total connection-establishment deadline; defaults to 10 seconds.
     * @param timeout Positive deadline covering resolve, TCP connect, and handshake.
     * @pre Configure before connect(), with no concurrent client operations.
     * @throws std::invalid_argument if timeout is zero or negative.
     * @note The deadline starts when the event loop begins resolving. This duration
     *       also configures the WebSocket close-handshake timeout, but does not bound
     *       the lifetime of an open connection or the time spent draining queued writes.
     */
    virtual void setConnectionTimeout(std::chrono::seconds timeout) = 0;

    /**
     * @brief Installs the handler called once after a successful handshake.
     * @param callback Handler to store, or an empty handler to enable stdout logging.
     * @pre Configure before connect(), with no concurrent client operations.
     */
    virtual void setOnConnectedCallback(OnConnectedCallback callback) = 0;

    /**
     * @brief Installs the handler for complete inbound text messages.
     * @param callback Handler to store, or an empty handler to enable stdout logging.
     * @pre Configure before connect(), with no concurrent client operations.
     * @note Copy the message if it must be used after the callback returns.
     */
    virtual void setOnMessageCallback(OnMessageCallback callback) = 0;

    /**
     * @brief Installs the handler for asynchronous transport or message-type errors.
     * @param callback Handler to store, or an empty handler to enable stderr logging.
     * @pre Configure before connect(), with no concurrent client operations.
     * @note Synchronous argument/order exceptions are thrown directly to the caller.
     */
    virtual void setOnErrorCallback(OnErrorCallback callback) = 0;

    /**
     * @brief Installs the terminal handler for success, failure, or cancellation.
     * @param callback Handler to store, or an empty handler to enable stdout logging.
     * @pre Configure before connect(), with no concurrent client operations.
     * @note No successful connection is required. On terminal failure, the error
     *       handler runs first; cancelled I/O handlers may still need to complete.
     */
    virtual void setOnDisconnectedCallback(OnDisconnectedCallback callback) = 0;

    /**
     * @brief Posts a resolve, TCP connect, and WebSocket handshake sequence.
     * @param endpoint Address to own for the attempt; an empty target becomes "/".
     * @pre Configure the client before calling this method.
     * @throws std::invalid_argument if host/port is empty or a non-empty target
     *         does not start with '/'.
     * @throws std::logic_error if an attempt was already requested or close() was called.
     * @note Returning only confirms submission; callbacks require run(). Transport
     *       failures are reported asynchronously and the instance cannot reconnect.
     */
    virtual void connect(Endpoint endpoint) = 0;

    /**
     * @brief Blocks while processing this client's event loop.
     * @pre connect() returned successfully and the caller retains the client.
     * @throws std::logic_error if no connection attempt has been requested.
     * @note Multiple caller-owned threads may run the loop. It returns once terminal
     *       cleanup releases the work guard and outstanding handlers have completed.
     */
    virtual void run() = 0;

    /**
     * @brief Posts one complete outbound text message, including an empty message.
     * @param message Payload to own until its asynchronous write completes.
     * @pre connect() returned successfully; run() must execute for delivery.
     * @pre Payload is valid UTF-8; outbound bytes are not validated before sending as text.
     * @throws std::logic_error if connect() was not requested or closure was requested.
     * @note Messages may queue before the handshake completes. FIFO order follows
     *       strand processing; overlapping calls from different threads have no
     *       defined relative order. Submission does not acknowledge delivery:
     *       shutdown or failure may discard messages. The queue has no size limit,
     *       so callers must control their submission rate.
     */
    virtual void send(std::string message) = 0;

    /**
     * @brief Requests shutdown; repeated calls have no additional effect.
     *
     * An open connection drains messages already queued on the strand before a
     * normal close handshake. During establishment, shutdown cancels the attempt
     * and discards queued messages. Messages that reach the strand after shutdown
     * begins are discarded, even if send() returned before close() was processed.
     *
     * @note May be called from any thread or before connect(); calling it before
     *       connect() permanently prevents a connection attempt. Event-loop cleanup
     *       and notifications require a running loop; this method does not join it.
     */
    virtual void close() = 0;
};

/**
 * @brief Creates an idle client with a 10-second establishment timeout and no handlers.
 *
 * Boost.Asio and Boost.Beast are implementation details and are not exposed by
 * the public interface. Unset handlers log events to stdout and errors to stderr.
 * @return Shared ownership of an independent client and its event loop.
 */
[[nodiscard]] std::shared_ptr<WebSocketClient> createWebSocketClient();

}  // namespace websocket_client

#endif  // WS_CONNECTION_WEBSOCKET_CLIENT_H
