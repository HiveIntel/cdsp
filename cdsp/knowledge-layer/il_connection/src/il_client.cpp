#include "il_client.h"

#include <cstdlib>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>

#include "real_websocket_connection.h"
#include "il_message/subscribe_request.h"

namespace {

void fail(const boost::system::error_code& error_code, const std::string& operation) {
    std::cerr << operation << ": " << error_code.message() << '\n';
}

}  // namespace

InformationLayerClient::InformationLayerClient(
    std::string host,
    std::string port,
    std::shared_ptr<rdf_mapper::RdfMapper> rdf_mapper,
    std::vector<std::string> signals,
    std::shared_ptr<ReasonerService> reasoner_service,
    std::shared_ptr<WebSocketClientInterface> connection)
    : host_(std::move(host)),
      port_(std::move(port)),
      connection_(std::move(connection)),
      rdf_mapper_(std::move(rdf_mapper)),
      signals_(std::move(signals)),
      reasoner_service_(std::move(reasoner_service)) {
    if (host_.empty()) {
        throw std::invalid_argument("WebSocket host cannot be empty.");
    }
    if (port_.empty()) {
        throw std::invalid_argument("WebSocket port cannot be empty.");
    }
    if (!rdf_mapper_) {
        throw std::invalid_argument(
            "RDF mapper must be initialized before creating InformationLayerClient.");
    }
    if (!reasoner_service_) {
        throw std::invalid_argument(
            "Reasoner service must be initialized before creating InformationLayerClient.");
    }

    reasoner_service_->setReasoningResultMessageQueue(reply_messages_queue_);
}

void InformationLayerClient::initializeConnection() {
    const auto weak_client = weak_from_this();
    reasoner_service_->setReasoningResultHandler([weak_client](json result) mutable {
        if (const auto client = weak_client.lock()) {
            net::post(client->io_context_, [weak_client, result = std::move(result)]() mutable {
                if (const auto scheduled_client = weak_client.lock()) {
                    scheduled_client->enqueueReasoningResult(std::move(result));
                }
            });
        }
    });

    if (!connection_) {
        connection_ = std::make_shared<RealWebSocketConnection>(io_context_, shared_from_this());
    }
}

void InformationLayerClient::run() {
    connection_->asyncResolve(host_, port_);
    io_context_.run();
}

const std::string& InformationLayerClient::getHost() const { return host_; }

void InformationLayerClient::onConnect(boost::system::error_code error_code,
                                       const boost::asio::ip::tcp::endpoint& endpoint) {
    if (error_code) {
        fail(error_code, "Connection failed");
        return;
    }

    std::cout << "Connected to WebSocket server: " << endpoint.address() << ':' << endpoint.port()
              << '\n';
    connection_->asyncHandshake();
}

void InformationLayerClient::handshake(boost::system::error_code error_code) {
    if (error_code) {
        return fail(error_code, "Handshake failed");
    }

    const char* vehicle_object_id = std::getenv("VEHICLE_OBJECT_ID");
    if (vehicle_object_id == nullptr || *vehicle_object_id == '\0') {
        throw std::invalid_argument("The environment variable VEHICLE_OBJECT_ID has not been set.");
    }

    std::vector<json> requests;
    requests.reserve(signals_.size());
    for (const auto& signal : signals_) {
        const il_message::SubscribeRequest subscribe_request(
            vehicle_object_id,
            il_message::Node{signal},
            {il_message::ResponseFormat::FLAT, il_message::ResponseRoot::RELATIVE});
        requests.push_back(il_message::to_json(subscribe_request));
    }
    queueRequests(std::move(requests));
}

void InformationLayerClient::queueRequest(json message) {
    std::vector<json> requests;
    requests.push_back(std::move(message));
    queueRequests(std::move(requests));
}

void InformationLayerClient::queueRequests(std::vector<json> messages) {
    if (messages.empty()) {
        return;
    }

    const auto weak_client = weak_from_this();
    net::post(io_context_, [weak_client, messages = std::move(messages)]() mutable {
        if (const auto client = weak_client.lock()) {
            client->reply_messages_queue_.insert(client->reply_messages_queue_.end(),
                                                  std::make_move_iterator(messages.begin()),
                                                  std::make_move_iterator(messages.end()));
            client->startNextReplyWrite();
        }
    });
}

void InformationLayerClient::sendMessage(const json& message) { connection_->asyncWrite(message); }

void InformationLayerClient::onSendMessage(boost::system::error_code error_code,
                                           std::size_t bytes_transferred) {
    write_in_progress_ = false;
    if (error_code) {
        fail(error_code, "Write failed");
        return;
    }

    std::cout << "Message sent: " << bytes_transferred << " bytes transferred\n";
    startNextReplyWrite();
    startReadIfNeeded();
}

void InformationLayerClient::onReceiveMessage(beast::error_code error_code,
                                              std::size_t bytes_transferred) {
    read_in_progress_ = false;
    if (error_code) {
        fail(error_code, "Read failed");
        return;
    }

    const auto received_message = std::make_shared<std::string>(connection_->getReceivedMessage());
    connection_->consumeBuffer(bytes_transferred);
    processMessage(received_message);
}

void InformationLayerClient::processMessage(const std::shared_ptr<const std::string>& message) {
    response_messages_queue_.push_back(message);
    const std::string incoming_message = *response_messages_queue_.front();
    response_messages_queue_.erase(response_messages_queue_.begin());

    std::cout << "Received message: " << incoming_message << '\n';
    const std::string rdf = rdf_mapper_->mapMessageToRdf(incoming_message);
    if (!rdf.empty()) {
        std::cout << "Mapped incoming message to RDF:\n" << rdf << '\n';
        if (!reasoner_service_->updateData(rdf, ReasonerSyntaxType::TURTLE)) {
            std::cerr << "Reasoner service rejected the RDF update.\n";
        }
    }

    startReadIfNeeded();
}

void InformationLayerClient::enqueueReasoningResult(json result) {
    reply_messages_queue_.push_back(std::move(result));
    startNextReplyWrite();
}

void InformationLayerClient::startNextReplyWrite() {
    if (write_in_progress_ || reply_messages_queue_.empty() || !connection_) {
        return;
    }

    write_in_progress_ = true;
    const json reply_message = reply_messages_queue_.front();
    reply_messages_queue_.erase(reply_messages_queue_.begin());
    std::cout << "Sending queued message:\n"
              << reply_message.dump() << '\n';
    sendMessage(reply_message);
}

void InformationLayerClient::startReadIfNeeded() {
    if (read_in_progress_) {
        return;
    }

    read_in_progress_ = true;
    connection_->asyncRead();
}
