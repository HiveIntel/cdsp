#include "real_websocket_connection.h"

#include <iostream>
#include <utility>

// Transport implementation for the IL connection package.

RealWebSocketConnection::RealWebSocketConnection(net::io_context& io_context,
                                                 std::weak_ptr<InformationLayerClient> client)
    : resolver_(io_context), ws_(io_context), client_(std::move(client)) {}

void RealWebSocketConnection::asyncResolve(const std::string& host, const std::string& port) {
    resolver_.async_resolve(host, port,
                            beast::bind_front_handler(&RealWebSocketConnection::onResolve, this));
}

void RealWebSocketConnection::onResolve(beast::error_code error_code,
                                        tcp::resolver::results_type results) {
    if (error_code) {
        return fail(error_code, "Resolve failed");
    }
    resolve_results_ = results;
    asyncConnect();
}

void RealWebSocketConnection::asyncConnect() {
    if (const auto client = client_.lock()) {
        net::async_connect(ws_.next_layer(), resolve_results_,
                           [client](boost::system::error_code error_code,
                                    const boost::asio::ip::tcp::endpoint& endpoint) {
                               client->onConnect(error_code, endpoint);
                           });
    } else {
        std::cerr << "WebSocket client no longer exists.\n";
    }
}

void RealWebSocketConnection::asyncHandshake() {
    if (const auto client = client_.lock()) {
        ws_.async_handshake(client->getHost(), "/",
                            [client](boost::system::error_code error_code) {
                                client->handshake(error_code);
                            });
    } else {
        std::cerr << "WebSocket client no longer exists.\n";
    }
}

void RealWebSocketConnection::asyncWrite(const json& message) {
    if (const auto client = client_.lock()) {
        ws_.async_write(net::buffer(message.dump()),
                        [client](boost::system::error_code error_code,
                                 std::size_t bytes_transferred) {
                            client->onSendMessage(error_code, bytes_transferred);
                        });
    } else {
        std::cerr << "WebSocket client no longer exists.\n";
    }
}

void RealWebSocketConnection::asyncRead() {
    if (const auto client = client_.lock()) {
        ws_.async_read(buffer_, [client](beast::error_code error_code, std::size_t bytes_transferred) {
            client->onReceiveMessage(error_code, bytes_transferred);
        });
    } else {
        std::cerr << "WebSocket client no longer exists.\n";
    }
}

std::string RealWebSocketConnection::getReceivedMessage() {
    return beast::buffers_to_string(buffer_.data());
}

void RealWebSocketConnection::consumeBuffer(std::size_t bytes) { buffer_.consume(bytes); }

void RealWebSocketConnection::fail(beast::error_code error_code, const char* operation) {
    std::cerr << operation << ": " << error_code.message() << '\n';
}
