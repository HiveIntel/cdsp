#ifndef IL_CONNECTION_IL_CLIENT_H
#define IL_CONNECTION_IL_CLIENT_H

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "rdf_mapper.h"
#include "reasoner_service.h"
#include "websocket_interface.h"

namespace beast = boost::beast;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class InformationLayerClient : public std::enable_shared_from_this<InformationLayerClient> {
   public:
    InformationLayerClient(std::string host,
                           std::string port,
                           std::shared_ptr<rdf_mapper::RdfMapper> rdf_mapper,
                           std::vector<std::string> signals,
                           std::shared_ptr<ReasonerService> reasoner_service,
                           std::shared_ptr<WebSocketClientInterface> connection = nullptr);

    void initializeConnection();
    void run();
    void queueRequest(json message);
    void queueRequests(std::vector<json> messages);
    const std::string& getHost() const;
    void onConnect(boost::system::error_code error_code,
                   const boost::asio::ip::tcp::endpoint& endpoint);
    void handshake(boost::system::error_code error_code);
    void onSendMessage(boost::system::error_code error_code, std::size_t bytes_transferred);
    void onReceiveMessage(beast::error_code error_code, std::size_t bytes_transferred);

   private:
    std::string host_;
    std::string port_;
    std::string vehicle_instance_;
    net::io_context io_context_;
    std::shared_ptr<WebSocketClientInterface> connection_;
    std::shared_ptr<rdf_mapper::RdfMapper> rdf_mapper_;
    std::vector<std::string> signals_;
    std::shared_ptr<ReasonerService> reasoner_service_;
    std::vector<json> reply_messages_queue_;
    std::vector<std::shared_ptr<const std::string>> response_messages_queue_;
    bool write_in_progress_ = false;
    bool read_in_progress_ = false;

    void processMessage(const std::shared_ptr<const std::string>& message);
    void enqueueReasoningResult(json result);
    void sendMessage(const json& message);
    void startNextReplyWrite();
    void startReadIfNeeded();
};

#endif  // IL_CONNECTION_IL_CLIENT_H
