#ifndef IL_CONNECTION_ABSTRACT_WEBSOCKET_CONNECTION_H
#define IL_CONNECTION_ABSTRACT_WEBSOCKET_CONNECTION_H

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <memory>

#include "il_client.h"
#include "websocket_interface.h"

class AbstractWebSocketConnection : public WebSocketClientInterface {
   public:
    AbstractWebSocketConnection(net::io_context& io_context,
                            std::weak_ptr<InformationLayerClient> client);

    void asyncResolve(const std::string& host, const std::string& port) override;
    void asyncConnect() override;
    void asyncHandshake() override;
    void asyncWrite(const json& message) override;
    void asyncRead() override;
    std::string getReceivedMessage() override;
    void consumeBuffer(std::size_t bytes) override;

   private:
    tcp::resolver resolver_;
    tcp::resolver::results_type resolve_results_;
    beast::websocket::stream<tcp::socket> ws_;
    std::weak_ptr<InformationLayerClient> client_;
    beast::flat_buffer buffer_;

    void onResolve(beast::error_code error_code, tcp::resolver::results_type results);
    void fail(beast::error_code error_code, const char* operation);
};

#endif  // IL_CONNECTION_ABSTRACT_WEBSOCKET_CONNECTION_H
