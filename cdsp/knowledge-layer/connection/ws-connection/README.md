# ws-connection

`ws-connection` is an asynchronous, text-only WebSocket client for one
connection to a plain `ws://` server. It is built on Boost.Asio and Boost.Beast,
but its public header exposes only the standard library, so users do not need
Boost headers.

The Knowledge Layer uses it to talk to the Information Layer. The JSON-RPC
messages themselves are built and parsed by the sibling
[`il-protocol`](../il-protocol/README.md) package.

The CMake target is `ws-connection` (C++17). It links `Boost::system`
privately, so the including project must call `find_package(Boost ...
COMPONENTS system)` first. The Knowledge Layer root project already does.

## Usage

```cpp
#include "websocket_client.h"

#include <chrono>
#include <iostream>

auto client = websocket_client::createWebSocketClient();
const std::weak_ptr<websocket_client::WebSocketClient> weak_client = client;

client->setConnectionTimeout(std::chrono::seconds{10});
client->setOnConnectedCallback([weak_client] {
    if (const auto connected = weak_client.lock()) {
        connected->send(R"({"jsonrpc":"2.0","method":"subscribe", ...})");
    }
});
client->setOnMessageCallback([](const std::string& message) { std::cout << message << '\n'; });
client->setOnErrorCallback([](const std::string& error) { std::cerr << error << '\n'; });
client->setOnDisconnectedCallback([] { std::cout << "disconnected\n"; });

client->connect({"127.0.0.1", "8080", "/"});
client->run();  // blocks until the connection is closed or fails
```

1. Create the client with `createWebSocketClient()` and keep the returned
   `shared_ptr` until every `run()` call has returned.
2. Configure the timeout and callbacks **before** `connect()`. The setters are
   not thread-safe.
3. `connect(Endpoint)` only submits the attempt; nothing happens until `run()`.
4. `run()` processes the event loop and blocks until the connection is
   finished. It may be called from several threads.
5. `send()` and `close()` may be called from any thread, including from inside
   a callback.

When a callback captures its own client, use a `weak_ptr` as above to avoid a
reference cycle.

## Endpoint

```cpp
struct Endpoint {
    std::string host;          // DNS name or IP address, no scheme/port/brackets
    std::string port;          // port number or service name
    std::string target{"/"};   // HTTP request target, already URI-encoded
};
```

`Endpoint` is not a URL parser: pass `{"localhost", "8080", "/events?x=1"}`,
not `ws://localhost:8080/events?x=1`. An empty target becomes `/`. An IPv6
host is written without brackets; the client adds them to the `Host` header.

## Callbacks

| Callback | When |
|---|---|
| `OnConnectedCallback` | Once, after the WebSocket handshake succeeds |
| `OnMessageCallback(const std::string&)` | For each complete inbound text message |
| `OnErrorCallback(const std::string&)` | Transport errors, timeouts and rejected binary messages, prefixed with the operation (`resolve:`, `connect:`, `handshake:`, `read:`, `write:`, `close:`) |
| `OnDisconnectedCallback` | Exactly once when the client becomes terminal, whether it closed normally, failed or never connected |

- When a callback is not set, the event is printed to standard output (errors
  to standard error).
- On failure the error callback runs before the disconnected callback.
- Callbacks run serialized on the client's strand. With several `run()`
  threads, consecutive callbacks may run on different threads.
- The `std::string` passed to the message and error callbacks is only valid
  during the call; copy it to keep it.
- Callbacks must not throw, block for long, or call `run()`. An exception from
  a callback propagates out of `run()`.

## Sending and closing

- `send()` copies the message into an unbounded FIFO queue. Messages may be
  sent before the handshake completes; they are written once the connection is
  open, one at a time and in submission order. Returning from `send()` does not
  mean the message was delivered.
- `close()` is idempotent. On an open connection it first sends the messages
  that are already queued, then performs a normal WebSocket close handshake.
  During connection establishment it cancels the attempt and drops the queue.
- An inbound binary message is reported through the error callback and closes
  the connection with code `unknown_data`, without sending queued messages.

## Errors and lifecycle

| Call | Throws |
|---|---|
| `setConnectionTimeout(t)` with `t <= 0` | `std::invalid_argument` |
| `connect()` with empty host/port, or a target not starting with `/` | `std::invalid_argument` |
| `connect()` a second time, or after `close()` | `std::logic_error` |
| `run()` or `send()` before `connect()` | `std::logic_error` |
| `send()` after `close()`, or after the connection has ended | `std::logic_error` |

Everything that happens on the network is reported asynchronously through the
callbacks, not as exceptions.

A client makes **one** connection attempt. After it ends, create a new client
to reconnect.

## Timeouts

`setConnectionTimeout` (default 10 seconds) bounds the whole establishment:
DNS resolution, TCP connect and the WebSocket handshake. The same value is
used as the timeout for the close handshake. An open connection has no idle
timeout, and draining queued messages during `close()` is not time-limited.

## Not supported

TLS (`wss://`), binary messages, automatic reconnect or retry, keep-alive
pings, and a bounded send queue.

## Build and test

The package is added by the Knowledge Layer root project. From the repository
root:

```sh
cmake -S cdsp/knowledge-layer -B build/knowledge-layer -DKL_BUILD_TESTS=ON
cmake --build build/knowledge-layer --target ws-connection ws-connection-tests
ctest --test-dir build/knowledge-layer -R ws-connection --output-on-failure
```

The root project requires CMake 3.25 or newer and Boost 1.87. The tests start a
WebSocket server on the IPv4 loopback interface and do not need an external
service. They cover timeout validation, callbacks and FIFO sending, default
logging, binary-message rejection, call-order errors and idempotent close.
