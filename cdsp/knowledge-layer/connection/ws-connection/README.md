# ws-connection

`ws-connection` is an asynchronous, text-only WebSocket client built with
Boost.Asio and Boost.Beast. The public interface does not expose Boost types.

## Usage

```cpp
#include "websocket_client.h"

#include <chrono>
#include <iostream>

auto client = websocket_client::createWebSocketClient();

client->setConnectionTimeout(std::chrono::seconds{10});
client->setOnConnectedCallback([] { std::cout << "connected\n"; });
client->setOnMessageCallback(
    [](const std::string& message) { std::cout << message << '\n'; });
client->setOnErrorCallback(
    [](const std::string& error) { std::cerr << error << '\n'; });
client->setOnDisconnectedCallback([] { std::cout << "disconnected\n"; });

client->connect({"localhost", "8080", "/ws"});
client->send("hello");
client->run();
```

Configure callbacks and the timeout before `connect()`. The default connection
timeout is 10 seconds, and non-positive values are rejected. If a callback is
not configured, the corresponding event is written to standard output or, for
errors, standard error.

The client keeps WebSocket operations serialized on an Asio strand. Messages
passed to `send()` are queued and written in FIFO order. `close()` performs an
idempotent graceful close and drains already queued text messages.

Version one supports plain `ws://` text messages only. TLS, binary messages,
automatic reconnect, and retry policies are outside this package.

## Build and test

The package is registered by the Knowledge Layer root CMake project as the
`ws-connection` target. From the repository root:

```sh
cmake -S cdsp/knowledge-layer -B build/knowledge-layer -DKL_BUILD_TESTS=ON
cmake --build build/knowledge-layer --target ws-connection ws-connection-tests
ctest --test-dir build/knowledge-layer -R ws-connection --output-on-failure
```

The Knowledge Layer build requires CMake 3.25 or newer and its configured Boost
version. Integration tests open a WebSocket server on the IPv4 loopback
interface and do not require an external service.
