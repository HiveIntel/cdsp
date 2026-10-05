#include "websocket_client.h"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
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

class WebSocketClientImpl final
    : public WebSocketClient,
      public std::enable_shared_from_this<WebSocketClientImpl> {
   public:
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
    enum class State {
        Idle,
        Resolving,
        Connecting,
        Handshaking,
        Open,
        Closing,
        Closed,
    };

    static void validateEndpoint(const Endpoint& endpoint);
    static std::string hostHeader(const Endpoint& endpoint);

    bool isEstablishing() const noexcept;
    bool isTerminal() const noexcept;

    void startConnect(Endpoint endpoint);
    void onEstablishmentTimeout(const boost::beast::error_code& error_code);
    void onResolve(
        const boost::beast::error_code& error_code,
        boost::asio::ip::tcp::resolver::results_type results);
    void onConnect(const boost::beast::error_code& error_code);
    void onHandshake(const boost::beast::error_code& error_code);

    void doRead();
    void onRead(const boost::beast::error_code& error_code,
                std::size_t bytes_transferred);

    void enqueueMessage(std::string message);
    void doWrite();
    void onWrite(const boost::beast::error_code& error_code,
                 std::size_t bytes_transferred);

    void beginClose(boost::beast::websocket::close_reason reason,
                    bool drain_writes);
    void doClose();
    void onClose(const boost::beast::error_code& error_code);

    void finishWithError(const char* operation,
                         const boost::beast::error_code& error_code);
    void finishWithError(const char* operation, std::string message);
    void finish(std::string error = {});

    void notifyConnected();
    void notifyMessage(const std::string& message);
    void notifyError(const std::string& message);
    void notifyDisconnected();

    boost::asio::io_context io_context_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::beast::websocket::stream<boost::beast::tcp_stream> websocket_;
    boost::asio::steady_timer establishment_timer_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard_;

    std::chrono::seconds connection_timeout_{10};
    Endpoint endpoint_;
    OnConnectedCallback on_connected_callback_;
    OnMessageCallback on_message_callback_;
    OnErrorCallback on_error_callback_;
    OnDisconnectedCallback on_disconnected_callback_;
    State state_{State::Idle};
    boost::beast::flat_buffer read_buffer_;
    std::deque<std::shared_ptr<const std::string>> write_queue_;
    std::shared_ptr<const std::string> active_write_;
    boost::beast::websocket::close_reason close_reason_{
        boost::beast::websocket::close_code::normal};
    std::atomic_bool connect_requested_{false};
    std::atomic_bool close_requested_{false};
    bool read_in_progress_{false};
    bool write_in_progress_{false};
    bool close_in_progress_{false};
    bool drain_writes_before_close_{true};
    bool terminal_notified_{false};
};

WebSocketClientImpl::WebSocketClientImpl()
    : strand_(boost::asio::make_strand(io_context_)),
      resolver_(strand_),
      websocket_(strand_),
      establishment_timer_(strand_),
      work_guard_(boost::asio::make_work_guard(io_context_)) {}

void WebSocketClientImpl::setConnectionTimeout(
    const std::chrono::seconds timeout) {
    if (timeout <= std::chrono::seconds::zero()) {
        throw std::invalid_argument(
            "WebSocket connection timeout must be positive.");
    }
    connection_timeout_ = timeout;
}

void WebSocketClientImpl::setOnConnectedCallback(
    OnConnectedCallback callback) {
    on_connected_callback_ = std::move(callback);
}

void WebSocketClientImpl::setOnMessageCallback(OnMessageCallback callback) {
    on_message_callback_ = std::move(callback);
}

void WebSocketClientImpl::setOnErrorCallback(OnErrorCallback callback) {
    on_error_callback_ = std::move(callback);
}

void WebSocketClientImpl::setOnDisconnectedCallback(
    OnDisconnectedCallback callback) {
    on_disconnected_callback_ = std::move(callback);
}

void WebSocketClientImpl::connect(Endpoint endpoint) {
    validateEndpoint(endpoint);

    if (close_requested_.load()) {
        throw std::logic_error("Cannot connect a closed WebSocket client.");
    }

    bool expected = false;
    if (!connect_requested_.compare_exchange_strong(expected, true)) {
        throw std::logic_error(
            "WebSocketClient::connect may only be called once.");
    }

    if (endpoint.target.empty()) {
        endpoint.target = "/";
    } else if (endpoint.target.front() != '/') {
        throw std::invalid_argument("WebSocket target must start with '/'.");
    }

    const std::weak_ptr<WebSocketClientImpl> weak_self = shared_from_this();
    boost::asio::post(
        strand_, [weak_self, endpoint = std::move(endpoint)]() mutable {
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
    boost::asio::post(
        strand_, [weak_self, message = std::move(message)]() mutable {
            if (const auto self = weak_self.lock()) {
                self->enqueueMessage(std::move(message));
            }
        });
}

void WebSocketClientImpl::close() {
    if (close_requested_.exchange(true)) {
        return;
    }

    const std::weak_ptr<WebSocketClientImpl> weak_self = shared_from_this();
    boost::asio::post(strand_, [weak_self] {
        if (const auto self = weak_self.lock()) {
            self->beginClose(
                boost::beast::websocket::close_reason{
                    boost::beast::websocket::close_code::normal},
                true);
        }
    });
}

void WebSocketClientImpl::validateEndpoint(const Endpoint& endpoint) {
    if (endpoint.host.empty()) {
        throw std::invalid_argument("WebSocket host cannot be empty.");
    }
    if (endpoint.port.empty()) {
        throw std::invalid_argument("WebSocket port cannot be empty.");
    }
}

std::string WebSocketClientImpl::hostHeader(const Endpoint& endpoint) {
    std::string host = endpoint.host;
    if (host.find(':') != std::string::npos &&
        (host.empty() || host.front() != '[')) {
        host = '[' + host + ']';
    }
    return host + ':' + endpoint.port;
}

bool WebSocketClientImpl::isEstablishing() const noexcept {
    return state_ == State::Resolving || state_ == State::Connecting ||
           state_ == State::Handshaking;
}

bool WebSocketClientImpl::isTerminal() const noexcept {
    return state_ == State::Closed;
}

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
    establishment_timer_.expires_after(connection_timeout_);

    const auto self = shared_from_this();
    establishment_timer_.async_wait(
        [self](const boost::beast::error_code& error_code) {
            self->onEstablishmentTimeout(error_code);
        });

    resolver_.async_resolve(
        endpoint_.host, endpoint_.port,
        [self](const boost::beast::error_code& error_code,
               boost::asio::ip::tcp::resolver::results_type results) {
            self->onResolve(error_code, std::move(results));
        });
}

void WebSocketClientImpl::onEstablishmentTimeout(
    const boost::beast::error_code& error_code) {
    if (error_code == boost::asio::error::operation_aborted ||
        !isEstablishing()) {
        return;
    }
    finishWithError("connect", "connection establishment timed out");
}

void WebSocketClientImpl::onResolve(
    const boost::beast::error_code& error_code,
    boost::asio::ip::tcp::resolver::results_type results) {
    if (isTerminal()) {
        return;
    }
    if (error_code) {
        finishWithError("resolve", error_code);
        return;
    }

    state_ = State::Connecting;
    boost::beast::get_lowest_layer(websocket_)
        .expires_after(connection_timeout_);

    const auto self = shared_from_this();
    boost::beast::get_lowest_layer(websocket_)
        .async_connect(
            results,
            [self](const boost::beast::error_code& connect_error,
                   const boost::asio::ip::tcp::endpoint&) {
                self->onConnect(connect_error);
            });
}

void WebSocketClientImpl::onConnect(
    const boost::beast::error_code& error_code) {
    if (isTerminal()) {
        return;
    }
    if (error_code) {
        finishWithError("connect", error_code);
        return;
    }

    state_ = State::Handshaking;
    boost::beast::get_lowest_layer(websocket_).expires_never();

    auto timeout = boost::beast::websocket::stream_base::timeout::suggested(
        boost::beast::role_type::client);
    timeout.handshake_timeout = connection_timeout_;
    websocket_.set_option(timeout);
    websocket_.set_option(boost::beast::websocket::stream_base::decorator(
        [](boost::beast::websocket::request_type& request) {
            request.set(boost::beast::http::field::user_agent,
                        std::string{BOOST_BEAST_VERSION_STRING} +
                            " websocket-client");
        }));

    const auto self = shared_from_this();
    websocket_.async_handshake(
        hostHeader(endpoint_), endpoint_.target,
        [self](const boost::beast::error_code& handshake_error) {
            self->onHandshake(handshake_error);
        });
}

void WebSocketClientImpl::onHandshake(
    const boost::beast::error_code& error_code) {
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
    websocket_.async_read(
        read_buffer_,
        [self](const boost::beast::error_code& error_code,
               const std::size_t bytes_transferred) {
            self->onRead(error_code, bytes_transferred);
        });
}

void WebSocketClientImpl::onRead(
    const boost::beast::error_code& error_code, const std::size_t) {
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
            return;
        }
        finishWithError("read", error_code);
        return;
    }

    const bool is_text = websocket_.got_text();
    std::string message =
        boost::beast::buffers_to_string(read_buffer_.data());
    read_buffer_.consume(read_buffer_.size());

    if (!is_text) {
        notifyError("read: binary WebSocket messages are not supported");
        beginClose(
            boost::beast::websocket::close_reason{
                boost::beast::websocket::close_code::unknown_data,
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
        return;
    }

    write_queue_.push_back(
        std::make_shared<const std::string>(std::move(message)));
    doWrite();
}

void WebSocketClientImpl::doWrite() {
    if (write_in_progress_ || close_in_progress_ || write_queue_.empty()) {
        return;
    }
    if (state_ != State::Open &&
        !(state_ == State::Closing && drain_writes_before_close_)) {
        return;
    }

    write_in_progress_ = true;
    active_write_ = std::move(write_queue_.front());
    write_queue_.pop_front();

    const auto payload = active_write_;
    const auto self = shared_from_this();
    websocket_.async_write(
        boost::asio::buffer(*payload),
        [self, payload](const boost::beast::error_code& error_code,
                        const std::size_t bytes_transferred) {
            self->onWrite(error_code, bytes_transferred);
        });
}

void WebSocketClientImpl::onWrite(
    const boost::beast::error_code& error_code, const std::size_t) {
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

void WebSocketClientImpl::beginClose(
    boost::beast::websocket::close_reason reason, const bool drain_writes) {
    if (isTerminal()) {
        return;
    }

    close_requested_.store(true);

    if (state_ == State::Idle || isEstablishing()) {
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
    const auto self = shared_from_this();
    websocket_.async_close(
        close_reason_, [self](const boost::beast::error_code& error_code) {
            self->onClose(error_code);
        });
}

void WebSocketClientImpl::onClose(
    const boost::beast::error_code& error_code) {
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

void WebSocketClientImpl::finishWithError(
    const char* operation, const boost::beast::error_code& error_code) {
    finishWithError(operation, error_code.message());
}

void WebSocketClientImpl::finishWithError(const char* operation,
                                          std::string message) {
    finish(std::string{operation} + ": " + std::move(message));
}

void WebSocketClientImpl::finish(std::string error) {
    if (terminal_notified_) {
        return;
    }

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
    if (!read_in_progress_) {
        read_buffer_.consume(read_buffer_.size());
    }
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
