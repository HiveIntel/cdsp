# IL Connection package

This package owns the Information Layer JSON-RPC boundary. It provides
outbound GET, SET, SUBSCRIBE, and UNSUBSCRIBE DTOs plus typed parsing for
inbound data, status, and error messages. Network transport is supplied by the
sibling `ws-connection` package.

The active receive path is:

```text
ws-connection text message -> ILMessageParser -> DataMessageDTO | StatusMessageDTO | ErrorMessageDTO
```

For a data response, `ILMessageParser` flattens flat or nested object payloads
into `DataPointDTO` entries with dot-separated paths. Optional generated and
received timestamps are exposed as `MetaDataPointDTO` entries containing
`std::chrono::system_clock::time_point` values. Data leaves must be strings,
integers, doubles, or booleans; a scalar `result.data` cannot be normalized
without the originating request path and is rejected.

The Knowledge Layer executable sends a root `Vehicle` subscription after the
WebSocket handshake, then prints every parsed message. Its runtime defaults are
`ws://127.0.0.1:8080/`, instance `VIN_1805`, nested response format, and
absolute response paths. The executable requires its model configuration in
`MODEL_CONFIG`; the driving-style container supplies its default. Configure it
with:

- `MODEL_CONFIG`
- `HOST_WEBSOCKET_SERVER`
- `PORT_WEBSOCKET_SERVER`
- `WEBSOCKET_TARGET`
- `VEHICLE_OBJECT_ID`
- `IL_SCHEMA`
- `IL_PATH` (unset or empty means the schema root)
- `IL_FORMAT`
- `IL_ROOT`

## Build and test independently

The test project is intentionally standalone, and all generated files stay
inside this package's `tests` directory:

```sh
cmake -S cdsp/knowledge-layer/connection/il-connection/tests \
  -B cdsp/knowledge-layer/connection/il-connection/tests/build
cmake --build cdsp/knowledge-layer/connection/il-connection/tests/build
ctest --test-dir cdsp/knowledge-layer/connection/il-connection/tests/build \
  --output-on-failure
```

The tests cover request serialization, inbound parsing, malformed input, and a
loopback WebSocket flow from SUBSCRIBE through a parsed data message. The
loopback flow does not require the real Information Layer service.
