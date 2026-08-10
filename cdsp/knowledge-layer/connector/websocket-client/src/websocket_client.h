#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "data_types.h"
#include "message_service.h"
#include "rdf_mapper.h"
#include "request_registry.h"
#include "websocket_interface.h"

namespace beast = boost::beast;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;

class WebSocketClient : public std::enable_shared_from_this<WebSocketClient> {
   public:
    WebSocketClient(SystemConfig system_config,
                    std::shared_ptr<rdf_mapper::RdfMapper> rdf_mapper,
                    std::vector<std::string> signals,
                    std::shared_ptr<WebSocketClientInterface> connection = nullptr);

    void initializeConnection();
    void run();
    void sendMessage(const json& message);
    const SystemConfig& getInitConfig() const;
    void onConnect(boost::system::error_code ec, const boost::asio::ip::tcp::endpoint& endpoint);
    void handshake(boost::system::error_code ec);
    void onSendMessage(boost::system::error_code ec, std::size_t bytes_transferred);
    void onReceiveMessage(beast::error_code ec, std::size_t bytes_transferred);

   private:
    SystemConfig system_config_;
    net::io_context io_context_;
    std::shared_ptr<WebSocketClientInterface> connection_;
    std::shared_ptr<rdf_mapper::RdfMapper> rdf_mapper_;
    std::vector<std::string> signals_;        
    std::shared_ptr<ReasonerService> reasoner_service_;
    std::vector<json> reply_messages_queue_;
    std::vector<std::shared_ptr<const std::string>> response_messages_queue_;

    void processMessage(const std::shared_ptr<const std::string>& message);
    void writeReplyMessagesOnQueue();
};

#endif  // WEBSOCKET_CLIENT_H
