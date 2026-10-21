# il-protocol

`il-protocol` owns the Knowledge Layer's side of the Information Layer (IL)
JSON-RPC 2.0 contract. It serializes outbound requests and parses inbound
responses into typed DTOs. It has no transport: pair it with the sibling
[`ws-connection`](../ws-connection/README.md) package to exchange messages over
a WebSocket.

```text
request DTO --to_json--> JSON text --ws-connection--> Information Layer
Information Layer --ws-connection--> JSON text --ILMessageParser--> DataMessageDTO
                                                                  | StatusMessageDTO
                                                                  | ErrorMessageDTO
```

The CMake target is `il-protocol` (C++17). Its only dependency is
`nlohmann_json::nlohmann_json`, which it exposes publicly because the request
serializers and `ErrorDTO::data` use `nlohmann::json`.

## Layout

| Header | Contents |
|---|---|
| `il_request_get_dto.h` | `il_request::GetRequestDTO` and its `to_json` |
| `il_request_set_dto.h` | `il_request::SetRequestDTO` (one data point) and its `to_json` |
| `il_request_subscribe_dto.h` | `il_request::SubscribeRequestDTO` and its `to_json` |
| `il_request_unsubscribe_dto.h` | `il_request::UnsubscribeRequestDTO` and its `to_json` |
| `il_message_parser.h` | `il_message::ILMessageParser` |
| `il_message_dto.h` | Inbound envelopes: `DataMessageDTO`, `StatusMessageDTO`, `ErrorMessageDTO`, `InboundMessageDTO` |
| `il_data_point_dto.h` | `il_data_point::DataPointDTO`, `MetadataPointDTO`, `TimestampsDTO` |

Every DTO has an `operator<<` for diagnostic logging. That output is
human-readable text, not JSON.

## Outbound requests

Each request DTO has an ADL `to_json` overload, so converting it to
`nlohmann::json` produces the complete JSON-RPC request:

```cpp
#include <nlohmann/json.hpp>

#include "il_request_subscribe_dto.h"

il_request::SubscribeRequestDTO subscribe;
subscribe.instance = "VIN_1805";
subscribe.path = "Vehicle.Speed";

const std::string wire = nlohmann::json(subscribe).dump();
```

```json
{
  "id": "subscribe-VIN_1805-Vehicle_Vehicle.Speed",
  "jsonrpc": "2.0",
  "method": "subscribe",
  "params": {
    "format": "flat",
    "instance": "VIN_1805",
    "path": "Vehicle.Speed",
    "root": "absolute",
    "schema": "Vehicle"
  }
}
```

| DTO | `method` | Parameters |
|---|---|---|
| `GetRequestDTO` | `get` | `schema`, `instance`, optional `path`, `format`, `root` |
| `SetRequestDTO` | `set` | `schema`, `instance`, one `data` point, optional generated timestamp |
| `SubscribeRequestDTO` | `subscribe` | `schema` (default `Vehicle`), `instance`, optional `path`, `format` (default `flat`), `root` (default `absolute`) |
| `UnsubscribeRequestDTO` | `unsubscribe` | `schema`, `instance`, optional `path` |

- A disengaged `std::optional` parameter is omitted so the IL uses its default.
  An engaged empty string is sent as `""`. To drop a subscription default,
  reset the optional (`subscribe.format.reset()`).
- An absent `path` selects the schema root.
- The serializers do not validate schema names, paths or values.

### Request ids

Ids are deterministic: `<method>-<instance>-<schema>_<path>`, with an empty
path when none is set. Repeating a request, or sending two GETs that differ only
in `format`/`root`, reuses the same id. The package does not track outstanding
requests; matching a response to its request is the caller's job.

### SET with a timestamp

`SetRequestDTO` writes exactly one scalar. When `generated_timestamp` is set,
it is sent as seconds plus non-negative nanoseconds under the empty metadata
key, which refers to the point named by `params.path`:

```cpp
il_request::SetRequestDTO set{"Vehicle", "VIN_1805", {"Vehicle.Speed", 42.5},
                              std::chrono::system_clock::now()};
```

```json
"params": {
  "data": 42.5,
  "instance": "VIN_1805",
  "metadata": {"": {"timestamps": {"generated": {"seconds": 1776429129, "nanos": 853211000}}}},
  "path": "Vehicle.Speed",
  "schema": "Vehicle"
}
```

## Inbound messages

`ILMessageParser` is stateless; both entry points are static and safe to call
concurrently.

```cpp
#include "il_message_parser.h"

void onMessage(const std::string& wire) {
    const il_message::InboundMessageDTO message = il_message::ILMessageParser::parseWire(wire);
    if (const auto* data = std::get_if<il_message::DataMessageDTO>(&message)) {
        for (const auto& point : data->data) {
            // point.path, point.value (std::variant<std::string, std::int64_t, double, bool>)
        }
    }
}
```

- `parseWire(std::string_view)` decodes JSON text. It throws
  `nlohmann::json::parse_error` for invalid JSON.
- `parse(const nlohmann::json&)` takes an already decoded value.
- Both throw `std::invalid_argument` when the message does not match the
  contract below.

### Accepted envelopes

Every message must be a JSON object with `"jsonrpc": "2.0"` and a **string**
`id`. The one exception is an error the server could not tie to a request
(for example a JSON parse error), which JSON-RPC sends with `"id": null`; it is
returned as an `ErrorMessageDTO` with an empty `id`. It is classified as:

| Envelope | Result |
|---|---|
| `result` object containing `data` | `DataMessageDTO` |
| empty `result` object (`{}`) | `StatusMessageDTO` |
| `error` object with integer `code` and string `message` | `ErrorMessageDTO`; optional `error.data` is kept as raw JSON |

A message with both `result` and `error`, or a non-empty `result` without
`data`, is rejected.

### Data flattening

`result.instance` must be a string and `result.data` must be an object. Nested
objects are flattened into dot-separated paths, and keys that already contain
dots are kept as they are, so flat and nested responses produce the same
points:

```json
{
  "jsonrpc": "2.0",
  "id": "subscribe-VIN_1805-Vehicle_Vehicle.CurrentLocation",
  "result": {
    "instance": "VIN_1805",
    "data": {"Vehicle": {"CurrentLocation": {"Latitude": 60.0, "Longitude": 10.0}}},
    "metadata": {"Vehicle": {"CurrentLocation": {
      "Latitude": {"timestamps": {"generated": {"seconds": 1776429129, "nanos": 853211000}}},
      "Longitude": {"timestamps": {"generated": {"seconds": 1776429129, "nanos": 853211000}}}
    }}}
  }
}
```

parses to

```text
DataMessageDTO{instance=VIN_1805,
  data=[Vehicle.CurrentLocation.Latitude = 60, Vehicle.CurrentLocation.Longitude = 10],
  metadata=[Vehicle.CurrentLocation.Latitude  generated=1776429129.853211000,
            Vehicle.CurrentLocation.Longitude generated=1776429129.853211000]}
```

- Leaf values must be a string, boolean, integer that fits in `int64_t`, or
  floating-point number. `null` and arrays are rejected.
- A scalar `result.data` (for example a `relative` root response for a single
  leaf) is rejected, because the parser has no request context to name it.
- Points come out in `nlohmann::json` key order (sorted), not wire order.
- Paths are not prefixed with the schema or request path; they are exactly what
  the response contains.
- Two leaves that flatten to the same path, such as `{"Vehicle.Speed": 1,
  "Vehicle": {"Speed": 2}}`, are rejected with `std::invalid_argument`.

### Metadata

`result.metadata` is optional. When it is absent or `null`, `metadata` is
`std::nullopt`; an empty object gives an empty vector.

The parser walks the metadata object with the same path rules as `data` and
creates one `MetadataPointDTO` for each object that has a `timestamps` member.
It keeps walking below such an object, so `{"Vehicle": {"timestamps": …,
"Speed": {"timestamps": …}}}` yields points for both `Vehicle` and
`Vehicle.Speed`. Duplicate metadata paths are rejected like duplicate data
paths.
`timestamps.received` and `timestamps.generated` are each optional and must be
`{"seconds": <int>, "nanos": <0..999999999>}` within the `system_clock` range.
Other timestamp fields are ignored.

Data and metadata are separate vectors: match them by `path`, not by index.

## Limitations

- Numeric ids are rejected; only string ids (and `null` on errors) are accepted.
- No request/response correlation, subscription tracking or retries.

## Used by

`driving_style_client` (`cdsp/knowledge-layer/src/main.cpp`) sends one
`SubscribeRequestDTO` per signal in its model configuration after the
WebSocket handshake and prints every parsed message. It reads:

| Variable | Default |
|---|---|
| `MODEL_CONFIG` | required: path to the model configuration |
| `HOST_WEBSOCKET_SERVER` | `127.0.0.1` |
| `PORT_WEBSOCKET_SERVER` | `8080` |
| `WEBSOCKET_TARGET` | `/` |
| `VEHICLE_OBJECT_ID` | `VIN_1805` |

`rdf-mapper` consumes `DataMessageDTO` directly.

## Build and test

The Knowledge Layer root project adds this package and, with
`KL_BUILD_TESTS=ON` (the default), its tests:

```sh
cmake -S cdsp/knowledge-layer -B build/knowledge-layer -DKL_BUILD_TESTS=ON
cmake --build build/knowledge-layer --target il-protocol il-protocol-tests
ctest --test-dir build/knowledge-layer -R il-protocol --output-on-failure
```

`tests/CMakeLists.txt` can also be configured on its own; it fetches
nlohmann/json and GoogleTest when they are not installed:

```sh
cmake -S cdsp/knowledge-layer/connection/il-protocol/tests \
  -B cdsp/knowledge-layer/connection/il-protocol/tests/build
cmake --build cdsp/knowledge-layer/connection/il-protocol/tests/build
ctest --test-dir cdsp/knowledge-layer/connection/il-protocol/tests/build \
  --output-on-failure
```

The tests cover request serialization, inbound parsing (including null-id
errors, nested metadata and duplicate paths), malformed envelopes and
diagnostic output. They do not need a running Information Layer.
