#include "il_message_parser.h"
#include "il_request_subscribe_dto.h"
#include "model_config_parser.h"
#include "websocket_client.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

std::string environmentOrDefault(const char* name, const char* default_value) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : default_value;
}

std::string requiredEnvironment(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        throw std::runtime_error(std::string{"Missing required environment variable: "} +
                                 name);
    }
    return value;
}

std::optional<std::string> optionalEnvironment(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string{value};
}

void printInboundMessage(const std::string& wire_message) {
    try {
        const auto message = il_message::ILMessageParser::parseWire(wire_message);
        std::visit(
            [](const auto& typed_message) {
                using Message = std::decay_t<decltype(typed_message)>;
                if constexpr (std::is_same_v<Message, il_message::DataMessageDTO>) {
                    std::cout << "[IL] DATA " << typed_message << '\n';
                } else if constexpr (std::is_same_v<Message,il_message::StatusMessageDTO>) {
                    std::cout << "[IL] STATUS " << typed_message << '\n';
                } else {
                    std::cerr << "[IL] ERROR " << typed_message << '\n';
                }
            },
            message);
    } catch (const std::exception& error) {
        std::cerr << "[IL] Invalid message: " << error.what()
                  << "\n[IL] Raw message: " << wire_message << '\n';
    }
}

}

void printBanner() {
    std::cout << "\033[1;36m"  // Bright blue color
              << "\n"
              << "╭───────────────────────────────────────────╮\n"
              << " ---------     REASONER CLIENT     --------- \n"
              << "╰───────────────────────────────────────────╯\n"
              << "\033[0m\n"  // Reset color
              << std::flush;
}


int main() {
    printBanner();

    try {
        const std::filesystem::path model_file{
            requiredEnvironment("MODEL_CONFIG")};
        const ModelConfig model = ModelConfigParser::parseFile(model_file);

        const std::string host = environmentOrDefault("HOST_WEBSOCKET_SERVER", "127.0.0.1");
        const std::string port =environmentOrDefault("PORT_WEBSOCKET_SERVER", "8080");
        const std::string target = environmentOrDefault("WEBSOCKET_TARGET", "/");
        const std::string instance = environmentOrDefault("VEHICLE_OBJECT_ID", "VIN_1805");

        std::vector<std::string> wire_subscriptions;
        const std::vector<std::string> signals = model.getSignals();
        wire_subscriptions.reserve(signals.size());

        for (const auto& signal : signals) {
            il_request::SubscribeRequestDTO subscription;
            subscription.instance = instance;
            subscription.path = signal;
            std::string wire_subscription = nlohmann::json(subscription).dump();
            std::cout << "[KL] Prepared subscription: " << wire_subscription << '\n';
            wire_subscriptions.push_back(std::move(wire_subscription));
        }

        auto client = websocket_client::createWebSocketClient();
        std::atomic_bool transport_failed{false};
        const std::weak_ptr<websocket_client::WebSocketClient> weak_client = client;

        client->setOnConnectedCallback(
            [weak_client, wire_subscriptions = std::move(wire_subscriptions)] {
                const auto connected_client = weak_client.lock();
                if (!connected_client) {
                    return;
                }
                std::cout << "[KL] Connected; sending " << wire_subscriptions.size() << " subscription(s)\n";
                for (const auto& wire_subscription : wire_subscriptions) {
                    std::cout << "[KL] Sending subscription: " << wire_subscription << '\n';
                    connected_client->send(wire_subscription);
                }
                std::cout << std::flush;
            });

        client->setOnMessageCallback(printInboundMessage);

        client->setOnErrorCallback( [&transport_failed](const std::string& error) {
                transport_failed.store(true);
                std::cerr << "[IL] WebSocket error: " << error << '\n';
            });

        client->setOnDisconnectedCallback(
            [] { std::cout << "[KL] Disconnected\n"; });

        std::cout << "[KL] Connecting to ws://" << host << ':' << port
                  << target << '\n'
                  << std::flush;
        client->connect({host, port, target});
        client->run();

        return transport_failed.load() ? EXIT_FAILURE : EXIT_SUCCESS;

    } catch (const std::exception& error) {
        std::cerr << "[KL] Client failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
