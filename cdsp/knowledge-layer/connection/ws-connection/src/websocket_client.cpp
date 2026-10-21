#include "websocket_client.h"

/**
 * @file websocket_client.cpp
 * @brief Boost.Asio/Beast implementation of the WebSocket transport interface.
 */

#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <cstddef>
#include <deque>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace websocket_client {
namespace {

/**
 * @brief Owns one connection attempt and its asynchronous state machine.
 *
 * Public submission methods post work to strand_. After construction, runtime
 * state and private I/O helpers are accessed only on that strand. Endpoint helpers
 * access their arguments only. Configuration setters are used before submission.
 * Only connect_requested_ and close_requested_ are shared with submitting threads.
 *
 * Public posts capture weak ownership so an idle, unrun client can be released.
 * Once an I/O operation starts, its completion handler holds shared ownership until
 * it finishes. The caller must still retain the client while executing run().
 */
class WebSocketClientImpl final : public WebSocketClient,
                                  public std::enable_shared_from_this<WebSocketClientImpl> {
   public:
    /// Binds all I/O objects to one strand and keeps the initially idle loop alive.
    WebSocketClientImpl();

    void setConnectionTimeout(std::chrono::seconds timeout) override;
    void setOnConnectedCallback(OnConnectedCallback callback) override;
    void setOnMessageCallback(OnMessageCallback callback) override;
    void setOnErrorCallback(OnErrorCallback callback) override;
    void setOnDisconnectedCallback(OnDisconnectedCallback callback) override;

    void connect(Endpoint endpoint) override;
    void run() override;
    void send(std::string message) override;
    void close() override;

   private:
    /// Normal progression is Idle -> Resolving -> Connecting -> Handshaking -> Open.
    /// Shutdown transitions through Closing when open; any phase can finish as Closed.
    enum class State {
        Idle,         ///< No establishment operation has started on the strand.
        Resolving,    ///< DNS/service resolution is outstanding.
        Connecting,   ///< TCP connection to a resolved endpoint is outstanding.
        Handshaking,  ///< HTTP upgrade to WebSocket is outstanding.
        Open,         ///< Text messages can be read and queued writes can be started.
        Closing,      ///< Draining writes or completing the WebSocket close handshake.
        Closed,       ///< Terminal state; late completion handlers must not restart I/O.
    };

    /// Rejects empty host/port or a malformed target; normalizes an empty target to "/".
    void validateEndpoint(Endpoint& endpoint);

    /// Builds HTTP Host as host:port, adding brackets around an IPv6 literal.
    std::string hostHeader(const Endpoint& endpoint);

    /// Identifies phases covered by the overall establishment deadline.
    bool isEstablishing() const noexcept;

    /// Identifies a finalized connection, including attempts that never became open.
    bool isTerminal() const noexcept;

    /// Starts resolution and the overall deadline unless shutdown already won admission.
    void startConnect(Endpoint endpoint);

    /// Fails a pending attempt; ignores timer cancellation and late timeout completions.
    void onEstablishmentTimeout(const boost::beast::error_code& error_code);

    /// Starts TCP connection over resolved endpoints or finalizes a resolution error.
    void onResolve(const boost::beast::error_code& error_code,
                   boost::asio::ip::tcp::resolver::results_type results);

    /// Switches from TCP deadlines to WebSocket timeouts and starts the HTTP upgrade.
    void onConnect(const boost::beast::error_code& error_code);

    /// Marks the connection open, notifies the caller, and starts text I/O.
    void onHandshake(const boost::beast::error_code& error_code);

    /// Starts one complete-message read only while open and without another pending read.
    void doRead();

    /// Delivers text, rejects binary input, or finalizes a peer close/transport error.
    /// The byte count is unused: the buffer supplies the complete message contents.
    void onRead(const boost::beast::error_code& error_code, std::size_t bytes_transferred);

    /// Admits a payload to the FIFO before/during Open; drops posts processed after shutdown.
    void enqueueMessage(std::string message);

    /// Starts one write while open or draining; retains the payload while Beast borrows its bytes.
    void doWrite();

    /// Releases the active payload, then advances the queue or the pending close.
    /// The byte count is unused: successful async_write completes the whole message.
    void onWrite(const boost::beast::error_code& error_code, std::size_t bytes_transferred);

    /**
     * @brief Cancels establishment or enters the open connection's shutdown sequence.
     * @param reason Close code/reason retained until the close handshake completes.
     * @param drain_writes Whether to send pending messages before closing. False drops
     *        queued messages, but still waits for an already-active write to complete.
     */
    void beginClose(boost::beast::websocket::close_reason reason, bool drain_writes);

    /// Starts the close handshake after write completion; an existing read may remain pending.
    /// @pre Shutdown was initiated on an established connection.
    void doClose();

    /// Accepts both local close success and an already-completed peer close as normal shutdown.
    void onClose(const boost::beast::error_code& error_code);

    /// Converts a transport error into the operation-prefixed terminal diagnostic.
    void finishWithError(const char* operation, const boost::beast::error_code& error_code);

    /// Prefixes a diagnostic with its operation name, then performs terminal cleanup.
    void finishWithError(const char* operation, std::string message);
    /**
     * @brief Finalizes transport state exactly once and releases the event-loop work guard.
     * @param error Optional diagnostic delivered before the disconnected notification.
     * @note Cancellation handlers still need to run; their buffers remain valid until then.
     */
    void finish(std::string error = {});

    /// Invokes the connected handler, or writes the default event to stdout.
    void notifyConnected();
    /// Borrows the message during callback delivery, or writes it to stdout.
    void notifyMessage(const std::string& message);
    /// Borrows the diagnostic during callback delivery, or writes it to stderr.
    void notifyError(const std::string& message);
    /// Invokes the terminal handler, or writes the default event to stdout.
    void notifyDisconnected();

    // Declaration order keeps the event loop alive until all objects using its executor die.
    /// Owns the event loop used by this client instance.
    boost::asio::io_context io_context_;

    /// Serializes operation initiation and completion handlers, including user callbacks.
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;

    /// Resolves the configured host and port before establishing TCP.
    boost::asio::ip::tcp::resolver resolver_;

    /// Owns the TCP connection and the WebSocket protocol state.
    boost::beast::websocket::stream<boost::beast::tcp_stream> websocket_;

    /// Bounds the complete resolve, connect, and handshake sequence.
    boost::asio::steady_timer establishment_timer_;

    /// Prevents an idle loop from returning before finish() releases outstanding work.
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;

    /// Pre-connect configuration for the overall deadline and WebSocket handshake/close timeout.
    std::chrono::seconds connection_timeout_{10};

    /// Normalized endpoint retained for the handshake host header and target.
    Endpoint endpoint_;

    // Handlers are configured before connect() and invoked exclusively from the strand.
    /// Optional successful-handshake handler; empty uses stdout.
    OnConnectedCallback on_connected_callback_;

    /// Optional complete-text-message handler; empty uses stdout.
    OnMessageCallback on_message_callback_;

    /// Optional diagnostic handler; empty uses stderr.
    OnErrorCallback on_error_callback_;

    /// Optional terminal handler; empty uses stdout.
    OnDisconnectedCallback on_disconnected_callback_;

    /// Current state-machine phase; accessed only on `strand_` after startup.
    State state_{State::Idle};

    /// Storage for a complete inbound message, which may span multiple WebSocket frames.
    /// Must not be consumed while async_read is outstanding, even during cancellation.
    boost::beast::flat_buffer read_buffer_;

    /// Unbounded FIFO of immutable payloads; excludes the write currently in progress.
    std::deque<std::shared_ptr<const std::string>> write_queue_;

    /// Current write's payload; also retained independently by its completion handler.
    std::shared_ptr<const std::string> active_write_;

    /// Close code and optional reason supplied to the WebSocket close handshake.
    boost::beast::websocket::close_reason close_reason_{
        boost::beast::websocket::close_code::normal};

    // These atomic gates do not protect configuration or the strand-owned state machine.
    /// Claims the single connection attempt before posting it to the strand.
    std::atomic_bool connect_requested_{false};
    /// Rejects further submissions immediately once close or terminal cleanup is requested.
    std::atomic_bool close_requested_{false};

    /// Prevents overlapping reads and protects read_buffer_ during terminal cleanup.
    bool read_in_progress_{false};
    /// Prevents overlapping writes and defers close until the active write completes.
    bool write_in_progress_{false};
    /// Prevents duplicate close operations; reads can remain outstanding during close.
    bool close_in_progress_{false};
    /// Determines whether queued messages are sent before the close handshake begins.
    bool drain_writes_before_close_{true};
    /// Guards terminal cleanup/notification against late timeout, read, write, and close handlers.
    bool terminal_notified_{false};
};

WebSocketClientImpl::WebSocketClientImpl()
    : strand_(boost::asio::make_strand(io_context_)),
      resolver_(strand_),
      websocket_(strand_),
      establishment_timer_(strand_),
      work_guard_(boost::asio::make_work_guard(io_context_)) {}

void WebSocketClientImpl::setConnectionTimeout(const std::chrono::seconds timeout) {
    if (timeout <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("WebSocket connection timeout must be positive.");
    }
    connection_timeout_ = timeout;
}

void WebSocketClientImpl::setOnConnectedCallback(OnConnectedCallback callback) {
    on_connected_callback_ = std::move(callback);
}

void WebSocketClientImpl::setOnMessageCallback(OnMessageCallback callback) {
    on_message_callback_ = std::move(callback);
}

void WebSocketClientImpl::setOnErrorCallback(OnErrorCallback callback) {
    on_error_callback_ = std::move(callback);
}

void WebSocketClientImpl::setOnDisconnectedCallback(OnDisconnectedCallback callback) {
    on_disconnected_callback_ = std::move(callback);
}

void WebSocketClientImpl::connect(Endpoint endpoint) {
    validateEndpoint(endpoint);

    if (close_requested_.load()) {
        throw std::logic_error("Cannot connect a closed WebSocket client.");
    }

    bool expected = false;
    if (!connect_requested_.compare_exchange_strong(expected, true)) {
        throw std::logic_error("WebSocketClient::connect may only be called once.");
    }

    // Posted work must not keep an unused client alive before the caller starts run().
    const std::weak_ptr<WebSocketClientImpl> weak_self = shared_from_this();
    boost::asio::post(strand_, [weak_self, endpoint = std::move(endpoint)]() mutable {
        if (const auto self = weak_self.lock()) {
            self->startConnect(std::move(endpoint));
        }
    });
}

void WebSocketClientImpl::run() {
    if (!connect_requested_.load()) {
        throw std::logic_error("Call WebSocketClient::connect before run.");
    }
    io_context_.run();
}

void WebSocketClientImpl::send(std::string message) {
    if (!connect_requested_.load()) {
        throw std::logic_error("Call WebSocketClient::connect before send.");
    }
    if (close_requested_.load()) {
        throw std::logic_error("Cannot send on a closing WebSocket client.");
    }

    const std::weak_ptr<WebSocketClientImpl> weak_self = shared_from_this();
    boost::asio::post(strand_, [weak_self, message = std::move(message)]() mutable {
        if (const auto self = weak_self.lock()) {
            self->enqueueMessage(std::move(message));
        }
    });
}

void WebSocketClientImpl::close() {
    // Publish shutdown immediately; actual stream access remains serialized on the strand.
    if (close_requested_.exchange(true)) {
        return;
    }

    const std::weak_ptr<WebSocketClientImpl> weak_self = shared_from_this();
    boost::asio::post(strand_, [weak_self] {
        if (const auto self = weak_self.lock()) {
            self->beginClose(
                boost::beast::websocket::close_reason{boost::beast::websocket::close_code::normal},
                true);
        }
    });
}

void WebSocketClientImpl::validateEndpoint(Endpoint& endpoint) {
    if (endpoint.host.empty()) {
        throw std::invalid_argument("WebSocket host cannot be empty.");
    }
    if (endpoint.port.empty()) {
        throw std::invalid_argument("WebSocket port cannot be empty.");
    }
    if (endpoint.target.empty()) {
        endpoint.target = "/";
    }
    if (endpoint.target.front() != '/') {
        throw std::invalid_argument("WebSocket target must start with '/'.");
    }
}

std::string WebSocketClientImpl::hostHeader(const Endpoint& endpoint) {
    std::string host = endpoint.host;
    // IPv6 colons belong inside brackets so the appended port remains unambiguous.
    if (host.find(':') != std::string::npos && (host.empty() || host.front() != '[')) {
        host = '[' + host + ']';
    }
    return host + ':' + endpoint.port;
}

bool WebSocketClientImpl::isEstablishing() const noexcept {
    return state_ == State::Resolving || state_ == State::Connecting ||
           state_ == State::Handshaking;
}

bool WebSocketClientImpl::isTerminal() const noexcept { return state_ == State::Closed; }

void WebSocketClientImpl::startConnect(Endpoint endpoint) {
    if (state_ != State::Idle || terminal_notified_) {
        return;
    }

    endpoint_ = std::move(endpoint);

    if (close_requested_.load()) {
        finish();
        return;
    }

    state_ = State::Resolving;
    // A single deadline spans all establishment phases; later phases do not restart it.
    establishment_timer_.expires_after(connection_timeout_);

    // Active I/O retains the client until its handler runs, including after cancellation.
    const auto self = shared_from_this();
    establishment_timer_.async_wait([self](const boost::beast::error_code& error_code) {
        self->onEstablishmentTimeout(error_code);
    });

    resolver_.async_resolve(endpoint_.host, endpoint_.port,
                            [self](const boost::beast::error_code& error_code,
                                   boost::asio::ip::tcp::resolver::results_type results) {
                                self->onResolve(error_code, std::move(results));
                            });
}

void WebSocketClientImpl::onEstablishmentTimeout(const boost::beast::error_code& error_code) {
    if (error_code == boost::asio::error::operation_aborted || !isEstablishing()) {
        return;
    }
    finishWithError("connect", "connection establishment timed out");
}

void WebSocketClientImpl::onResolve(const boost::beast::error_code& error_code,
                                    boost::asio::ip::tcp::resolver::results_type results) {
    if (isTerminal()) {
        return;
    }
    if (error_code) {
        finishWithError("resolve", error_code);
        return;
    }

    state_ = State::Connecting;
    boost::beast::get_lowest_layer(websocket_).expires_after(connection_timeout_);

    const auto self = shared_from_this();
    boost::beast::get_lowest_layer(websocket_)
        .async_connect(results, [self](const boost::beast::error_code& connect_error,
                                       const boost::asio::ip::tcp::endpoint&) {
            self->onConnect(connect_error);
        });
}

void WebSocketClientImpl::onConnect(const boost::beast::error_code& error_code) {
    if (isTerminal()) {
        return;
    }
    if (error_code) {
        finishWithError("connect", error_code);
        return;
    }

    state_ = State::Handshaking;
    // WebSocket timeouts take over after TCP connect; avoid competing TCP stream timers.
    boost::beast::get_lowest_layer(websocket_).expires_never();

    auto timeout =
        boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::client);
    timeout.handshake_timeout = connection_timeout_;
    websocket_.set_option(timeout);
    websocket_.set_option(boost::beast::websocket::stream_base::decorator(
        [](boost::beast::websocket::request_type& request) {
            request.set(boost::beast::http::field::user_agent,
                        std::string{BOOST_BEAST_VERSION_STRING} + " websocket-client");
        }));

    const auto self = shared_from_this();
    websocket_.async_handshake(hostHeader(endpoint_), endpoint_.target,
                               [self](const boost::beast::error_code& handshake_error) {
                                   self->onHandshake(handshake_error);
                               });
}

void WebSocketClientImpl::onHandshake(const boost::beast::error_code& error_code) {
    if (isTerminal()) {
        return;
    }
    if (error_code) {
        finishWithError("handshake", error_code);
        return;
    }

    static_cast<void>(establishment_timer_.cancel());
    state_ = State::Open;
    websocket_.text(true);

    notifyConnected();

    doRead();
    doWrite();
}

void WebSocketClientImpl::doRead() {
    if (state_ != State::Open || read_in_progress_) {
        return;
    }

    read_in_progress_ = true;
    const auto self = shared_from_this();
    websocket_.async_read(read_buffer_, [self](const boost::beast::error_code& error_code,
                                               const std::size_t bytes_transferred) {
        self->onRead(error_code, bytes_transferred);
    });
}

void WebSocketClientImpl::onRead(const boost::beast::error_code& error_code, const std::size_t) {
    read_in_progress_ = false;

    if (isTerminal()) {
        read_buffer_.consume(read_buffer_.size());
        return;
    }

    if (error_code == boost::beast::websocket::error::closed) {
        finish();
        return;
    }
    if (error_code) {
        if (state_ == State::Closing && close_in_progress_ &&
            error_code == boost::asio::error::operation_aborted) {
            // Let the close completion decide the terminal result of this shutdown.
            return;
        }
        finishWithError("read", error_code);
        return;
    }

    const bool is_text = websocket_.got_text();
    std::string message = boost::beast::buffers_to_string(read_buffer_.data());
    // Own a copy before consuming the buffer; callback references borrow that copy.
    read_buffer_.consume(read_buffer_.size());

    if (!is_text) {
        // Do not deliver binary input; request shutdown without draining queued text.
        notifyError("read: binary WebSocket messages are not supported");
        beginClose(
            boost::beast::websocket::close_reason{boost::beast::websocket::close_code::unknown_data,
                                                  "text messages only"},
            false);
        return;
    }

    notifyMessage(message);

    if (state_ == State::Open) {
        doRead();
    }
}

void WebSocketClientImpl::enqueueMessage(std::string message) {
    if (isTerminal() || state_ == State::Closing) {
        // A send admitted by the atomic gate may still lose a race with strand shutdown.
        return;
    }

    write_queue_.push_back(std::make_shared<const std::string>(std::move(message)));
    doWrite();
}

void WebSocketClientImpl::doWrite() {
    if (write_in_progress_ || close_in_progress_ || write_queue_.empty()) {
        return;
    }
    if (state_ != State::Open && !(state_ == State::Closing && drain_writes_before_close_)) {
        return;
    }

    write_in_progress_ = true;
    active_write_ = std::move(write_queue_.front());
    write_queue_.pop_front();

    // Beast borrows the bytes. Capture the payload independently so queue cleanup and
    // member resets cannot invalidate its buffer before the completion handler runs.
    const auto payload = active_write_;
    const auto self = shared_from_this();
    websocket_.async_write(boost::asio::buffer(*payload),
                           [self, payload](const boost::beast::error_code& error_code,
                                           const std::size_t bytes_transferred) {
                               self->onWrite(error_code, bytes_transferred);
                           });
}

void WebSocketClientImpl::onWrite(const boost::beast::error_code& error_code, const std::size_t) {
    write_in_progress_ = false;
    active_write_.reset();

    if (isTerminal()) {
        return;
    }
    if (error_code) {
        finishWithError("write", error_code);
        return;
    }

    if (state_ == State::Closing) {
        if (drain_writes_before_close_ && !write_queue_.empty()) {
            doWrite();
        } else {
            write_queue_.clear();
            doClose();
        }
        return;
    }

    doWrite();
}

void WebSocketClientImpl::beginClose(boost::beast::websocket::close_reason reason,
                                     const bool drain_writes) {
    if (isTerminal()) {
        return;
    }

    close_requested_.store(true);

    if (state_ == State::Idle || isEstablishing()) {
        // No WebSocket session exists yet, so cancel transport work instead of handshaking.
        finish();
        return;
    }

    if (state_ == State::Closing) {
        return;
    }

    state_ = State::Closing;
    close_reason_ = std::move(reason);
    drain_writes_before_close_ = drain_writes;

    if (!drain_writes_before_close_) {
        write_queue_.clear();
    }

    if (write_in_progress_) {
        // onWrite() continues shutdown; async_close must wait for this write to finish.
        return;
    }

    if (drain_writes_before_close_ && !write_queue_.empty()) {
        doWrite();
        return;
    }

    doClose();
}

void WebSocketClientImpl::doClose() {
    if (close_in_progress_ || write_in_progress_ || isTerminal()) {
        return;
    }

    close_in_progress_ = true;
    // A pending read can coexist with close and may observe the peer's close first.
    const auto self = shared_from_this();
    websocket_.async_close(close_reason_, [self](const boost::beast::error_code& error_code) {
        self->onClose(error_code);
    });
}

void WebSocketClientImpl::onClose(const boost::beast::error_code& error_code) {
    close_in_progress_ = false;

    if (isTerminal()) {
        return;
    }
    if (error_code && error_code != boost::beast::websocket::error::closed) {
        finishWithError("close", error_code);
        return;
    }

    finish();
}

void WebSocketClientImpl::finishWithError(const char* operation,
                                          const boost::beast::error_code& error_code) {
    finishWithError(operation, error_code.message());
}

void WebSocketClientImpl::finishWithError(const char* operation, std::string message) {
    finish(std::string{operation} + ": " + std::move(message));
}

void WebSocketClientImpl::finish(std::string error) {
    if (terminal_notified_) {
        return;
    }

    // Publish terminal state before cancellation; late handlers must not restart I/O.
    terminal_notified_ = true;
    state_ = State::Closed;
    close_requested_.store(true);

    boost::beast::error_code ignored;
    static_cast<void>(establishment_timer_.cancel());

    resolver_.cancel();
    boost::beast::get_lowest_layer(websocket_).expires_never();
    boost::beast::get_lowest_layer(websocket_).socket().cancel(ignored);
    boost::beast::get_lowest_layer(websocket_).socket().close(ignored);

    write_queue_.clear();
    // An outstanding read still owns mutable buffer storage until onRead() runs.
    if (!read_in_progress_) {
        read_buffer_.consume(read_buffer_.size());
    }
    // Release artificial work rather than stopping the loop: cancelled handlers must drain.
    work_guard_.reset();

    if (!error.empty()) {
        notifyError(error);
    }
    notifyDisconnected();
}

void WebSocketClientImpl::notifyConnected() {
    if (on_connected_callback_) {
        on_connected_callback_();
    } else {
        std::cout << "[WebSocket] Connected\n";
    }
}

void WebSocketClientImpl::notifyMessage(const std::string& message) {
    if (on_message_callback_) {
        on_message_callback_(message);
    } else {
        std::cout << "[WebSocket] Message: " << message << '\n';
    }
}

void WebSocketClientImpl::notifyError(const std::string& message) {
    if (on_error_callback_) {
        on_error_callback_(message);
    } else {
        std::cerr << "[WebSocket] Error: " << message << '\n';
    }
}

void WebSocketClientImpl::notifyDisconnected() {
    if (on_disconnected_callback_) {
        on_disconnected_callback_();
    } else {
        std::cout << "[WebSocket] Disconnected\n";
    }
}

}  // namespace

std::shared_ptr<WebSocketClient> createWebSocketClient() {
    return std::make_shared<WebSocketClientImpl>();
}

}  // namespace websocket_client
