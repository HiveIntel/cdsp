#include "websocket_client.h"

#include <iostream>
#include <stdexcept>

#include "helper.h"
#include "real_websocket_connection.h"
#include "bo_service.h"
#include "bo_to_dto.h"

namespace {
/**
 * @brief Logs errors encountered during asynchronous operations.
 *
 * @param ec The Boost.Beast error code.
 * @param what Description of the operation that failed.
 */
void Fail(const boost::system::error_code& ec, const std::string& what) {
    std::cerr << what << ": " << ec.message() << "\n";
}

}  // namespace

/**
 * @brief Constructs a WebSocketClient object with the specified initialization configuration and
 * connection.
 *
 * This constructor initializes the WebSocketClient with the given configuration and connection
 * interface. RDF mapper and signals are initialized by main.cpp and injected here.
 *
 * @param system_config The initialization configuration containing settings for the
 * WebSocketClient.
 * @param connection A shared pointer to a WebSocketClientInterface, representing the connection to
 * be used.
 */
WebSocketClient::WebSocketClient(SystemConfig system_config,
                                 std::shared_ptr<rdf_mapper::RdfMapper> rdf_mapper,
                                 std::vector<std::string> signals,
                                 std::shared_ptr<ReasonerService> reasoner_service,
                                 std::shared_ptr<WebSocketClientInterface> connection)
    : system_config_(std::move(system_config)),
      connection_(std::move(connection)),
      //request_registry_(std::make_shared<RequestRegistry>()),
      rdf_mapper_(std::move(rdf_mapper)),
      signals_(std::move(signals)),
      reasoner_service_(std::move(reasoner_service)) {

    if (!rdf_mapper_) {
        throw std::invalid_argument("RDF mapper must be initialized before creating WebSocketClient");
    }
    if (!reasoner_service_) {
        throw std::invalid_argument("Reasoner service must be initialized before creating WebSocketClient");
    }

    reasoner_service_->setReasoningResultMessageQueue(reply_messages_queue_);
}

/**
 * @brief Initializes the WebSocket connection.
 *
 * This method checks if the connection is already established. If not, it creates a new
 * instance of RealWebSocketConnection using the io_context_ and a shared pointer to the
 * current WebSocketClient instance.
 */
void WebSocketClient::initializeConnection() {
    const auto weak_client = weak_from_this();
    reasoner_service_->setReasoningResultHandler(
        [weak_client](json result) mutable {
            if (const auto client = weak_client.lock()) {
                net::post(client->io_context_,
                          [weak_client, result = std::move(result)]() mutable {
                              if (const auto scheduled_client = weak_client.lock()) {
                                  scheduled_client->enqueueReasoningResult(std::move(result));
                              }
                          });
            }
        });

    if (!connection_) {
        auto self = std::enable_shared_from_this<WebSocketClient>::shared_from_this();
        connection_ = std::make_shared<RealWebSocketConnection>(io_context_, self);
    }
}

/**
 * @brief Starts the WebSocket client's execution flow.
 *
 * This method resolves the WebSocket server's host and port, and then runs the IO context
 * to process asynchronous events.
 */
void WebSocketClient::run() {
    connection_->asyncResolve(system_config_.websocket_server.host,
                              system_config_.websocket_server.port);
    // Run the IO context to process asynchronous events
    io_context_.run();
}

/**
 * @brief Provides access to the client's configuration.
 *
 * @return The current SystemConfig object.
 */
const SystemConfig& WebSocketClient::getInitConfig() const { return system_config_; }

// Callbacks for connection lifecycle and message handling
void WebSocketClient::onConnect(boost::system::error_code error_code,
                                const boost::asio::ip::tcp::endpoint& endpoint) {
    if (error_code) {
        Fail(error_code, " - Connection failed:");
        return;
    }

    std::cout << " - Connected to Websocket Server: " << endpoint.address() << ":"
              << endpoint.port() << "\n";
    connection_->asyncHandshake();
}

/**
 * @brief Initiates the WebSocket handshake process and subscribes to supported schema types.
 *
 * This function is called to perform the WebSocket handshake. If the handshake
 * fails, it logs the error and returns. Upon successful handshake, it subscribes
 * to all supported schema types defined in the configuration and retrieves all data
 * points for each schema type.
 *
 * @param ec The error code indicating the result of the handshake attempt.
 */
void WebSocketClient::handshake(boost::system::error_code error_code) {
    if (error_code) {
        return Fail(error_code, " - Handshake failed:");
    }

    const std::string object_id = Helper::getEnvVariable("VEHICLE_OBJECT_ID");
    if (object_id.empty()) {
        throw std::invalid_argument("The environment variable VEHICLE_OBJECT_ID has not been set");
    }

    /*

    MessageService::createAndQueueVehicleSubscribeMessage(object_id, signals_, *request_registry_, reply_messages_queue_);


    auto get_message = BoService::createGetMessage(object_id, SchemaType::VEHICLE, signals_);
    std::cout << "Get Message " << get_message << '\n';

    auto get_message_dtos = BoToDto::convert(get_message);

    for (const auto& get_message_dto : get_message_dtos) {
        std::cout << "Get Message DTO: " << json(get_message_dto).dump() << '\n';
        reply_messages_queue_.push_back(json(get_message_dto));
    }
    std::cout << " - Handshake succeeded!\n\n";
    writeReplyMessagesOnQueue();
    */

    int id = 0;

    auto sub_message = BoService::createSubscribeMessage(object_id, SchemaType::VEHICLE, signals_);
    std::cout << "Subscribe Message " << sub_message << '\n';

    auto sub_message_dtos = BoToDto::convert(sub_message);

    for (auto& sub_message_dto : sub_message_dtos) {
        sub_message_dto.id = id++;
        std::cout << "Subscribe Message DTO: " << json(sub_message_dto).dump() << '\n';
        reply_messages_queue_.push_back(json(sub_message_dto));
    }

    writeReplyMessagesOnQueue();
    std::cout << " - Handshake succeeded!\n\n";
}

/**
 * @brief Sends a JSON message to the WebSocket server.
 *
 * @param message The JSON message to be sent.
 */
void WebSocketClient::sendMessage(const json& message) { 
    connection_->asyncWrite(message); 
}



void WebSocketClient::onSendMessage(boost::system::error_code error_code,
                                    std::size_t bytes_transferred) {

    write_in_progress_ = false;

    if (error_code) {
        Fail(error_code, "write");
        return;
    }

    std::cout << "Message sent! " << bytes_transferred << " bytes transferred\n\n";
    startNextReplyWrite();
    startReadIfNeeded();
}



void WebSocketClient::onReceiveMessage(
    boost::beast::error_code error_code,
    std::size_t bytes_transferred) {

    read_in_progress_ = false;

    if (error_code) {
        Fail(error_code, "read");
        return;
    }

    const auto received_message = std::make_shared<std::string>(connection_->getReceivedMessage());
    connection_->consumeBuffer(bytes_transferred);
    processMessage(received_message);
}

/**
 * @brief Processes an incoming WebSocket message.
 *
 * This method handles an incoming message by adding it to the response message queue,
 * extracting the highest priority message, and attempting to transform it into a reasoning triple
 * if it contains valid data. If there are reply messages queued, it writes them to the queue;
 * otherwise, it initiates an asynchronous read operation on the connection.
 *
 * @param message A shared pointer to the incoming message string to be processed.
 */
void WebSocketClient::processMessage(const std::shared_ptr<const std::string>& message) {
   
    response_messages_queue_.push_back(message);
    auto prio_message = *response_messages_queue_.front();
    response_messages_queue_.erase(response_messages_queue_.begin());

    std::cout << "Received message: " << prio_message << std::endl;

    const std::string rdf = rdf_mapper_->mapMessageToRdf(prio_message);
   
    if (!rdf.empty()) {
        std::cout << "Mapped incoming message to RDF:\n" << rdf << std::endl;
        if (!reasoner_service_->updateData(rdf, ReasonerSyntaxType::TURTLE)) {
            std::cerr << "Reasoner service rejected the RDF update.\n";
        }
    }
    
    // Attempt to extract a data message from the priority message
    // or process the status message and log any errors if present
    /*
    const auto data_message = MessageService::getDataOrProcessStatusFromMessage(prio_message, *request_registry_);

    
    // Process the data message if it is valid
    if (data_message.has_value()) {
        try {
            const std::string rdf = rdf_mapper_->mapMessageToRdf(prio_message);
            if (!rdf.empty()) {
                std::cout << "Mapped incoming message to RDF:\n" << rdf << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error mapping message to RDF: " << e.what() << std::endl;
        }

        // TODO(websocket-client): Forward RDF to the downstream reasoner implementation. The
        // legacy TripleAssembler and shared query path:
        // triple_assembler_.transformMessageToTriple(data_message.value());
        // reasoner_query_service_->processReasoningQuery(...);
    }
    */

    startReadIfNeeded();
}

/**
 * @brief Sends messages queued in the reply_messages_queue_.
 *
 * This function sends the first message in the reply_messages_queue_ to the WebSocket server.
 */
void WebSocketClient::writeReplyMessagesOnQueue() {
    const auto weak_client = weak_from_this();
    net::post(io_context_, [weak_client]() {
        if (const auto client = weak_client.lock()) {
            client->startNextReplyWrite();
        }
    });
}

void WebSocketClient::enqueueReasoningResult(json result) {
    reply_messages_queue_.push_back(std::move(result));
    startNextReplyWrite();
}

void WebSocketClient::startNextReplyWrite() {
    if (write_in_progress_ || reply_messages_queue_.empty() || !connection_) {
        return;
    }

    write_in_progress_ = true;
    json reply_message = reply_messages_queue_.front();
    reply_messages_queue_.erase(reply_messages_queue_.begin());
    std::cout << Helper::getFormattedTimestampNow("%Y-%m-%dT%H:%M:%S", true, true)
              << " Sending queue message:\n"
              << reply_message.dump() << std::endl;
    sendMessage(reply_message);
}

void WebSocketClient::startReadIfNeeded() {
    if (read_in_progress_) {
        return;
    }

    read_in_progress_ = true;
    connection_->asyncRead();
}
