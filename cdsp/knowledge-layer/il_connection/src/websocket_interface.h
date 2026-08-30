#ifndef IL_CONNECTION_WEBSOCKET_INTERFACE_H
#define IL_CONNECTION_WEBSOCKET_INTERFACE_H

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>

using json = nlohmann::json;

class WebSocketClientInterface {
   public:
    virtual ~WebSocketClientInterface() = default;

    virtual void asyncResolve(const std::string& host, const std::string& port) = 0;
    virtual void asyncConnect() = 0;
    virtual void asyncHandshake() = 0;
    virtual void asyncWrite(const json& message) = 0;
    virtual void asyncRead() = 0;
    virtual std::string getReceivedMessage() = 0;
    virtual void consumeBuffer(std::size_t bytes) = 0;
};

#endif  // IL_CONNECTION_WEBSOCKET_INTERFACE_H
