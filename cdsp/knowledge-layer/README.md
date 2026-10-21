# Knowledge Layer

The Knowledge Layer (KL) subscribes to vehicle signals from the Information
Layer (IL), turns each update into RDF, lets a reasoner derive new knowledge
from it, and writes the results back to the IL.

## Pipeline

```text
                  ┌──────────────────────── on-message callback ─────────────────────────┐
Information ──WS──► ws-connection ─► il-protocol ─► rdf-mapper ─► Turtle ─► reasoner-engine │
Layer (IL)        │  (text frames)   ILMessageParser  RDFMapper            ReasonerService  │
   ▲              └──────────────────  DataMessageDTO ──────────────────────────┬───────────┘
   │                                                                            │ result (JSON)
   └──WS── ws-connection ◄── il-protocol SetRequestDTO (JSON-RPC "set") ◄──────┘
```

1. **Subscribe.** After the WebSocket handshake, the KL sends one
   `SubscribeRequestDTO` per signal in the model configuration.
2. **Receive.** `ws-connection` delivers each IL text message to the
   on-message callback.
3. **Parse.** `ILMessageParser::parseWire` turns it into a
   `DataMessageDTO` (paths, typed values, generated timestamps). Status and
   error replies are handled separately.
4. **Map.** `RDFMapper::signalToRdf` turns the data points into Turtle using the
   vehicle and signal SHACL shapes from the model configuration.
5. **Reason.** `ReasonerService::updateData(turtle, ReasonerSyntaxType::TURTLE)`
   feeds the graph to the engine selected by the reasoner profile. Results are
   delivered as JSON through the result handler (or queue).
6. **Write back.** Each result is converted to a `SetRequestDTO` and sent to
   the IL through the same WebSocket connection.

### Status

The pipeline is not wired end to end yet; reasoning is still work in progress.

| Step | Status |
|---|---|
| 1–3 Subscribe, receive, parse | Implemented in `driving_style_client` (`src/main.cpp`), which currently **prints** each parsed message. |
| 4 Map to Turtle | `rdf-mapper` is implemented and tested, but not yet called from `main.cpp`. |
| 5 Reason | `ReasonerService` contract, profiles and factory exist. RDF4J and CQELS services are placeholders that return failure. |
| 6 Write back | `SetRequestDTO` serialization exists. The mapping from a reasoning result to SET requests is not defined yet. |

The driving-style shapes in `examples/driving-style/model/shapes/` follow the
`rdf-mapper` conventions. Mapping the generated `scene01.jsonl` stream with them
and applying `solutions/rdf4j/driving_style_rules.ttl` offline (rdflib, not
RDF4J) detects the aggressive-driving segment, so steps 4 and 5 agree on the
vocabulary. See the [TODO list](#todo) for what is still missing.

## Modules

| Module | CMake target | Role |
|---|---|---|
| [`connection/ws-connection`](connection/ws-connection/README.md) | `ws-connection` | Asynchronous text WebSocket client (Boost.Beast) |
| [`connection/il-protocol`](connection/il-protocol/README.md) | `il-protocol` | IL JSON-RPC request DTOs and typed response parsing |
| [`rdf-mapper`](rdf-mapper/README.md) | `rdf-mapper` | SHACL-driven mapping from IL data to RDF, Serd reader/writer |
| [`config-parser`](config-parser/README.md) | `config-parser` | Model configuration: signals, SHACL shape files, reasoner profile |
| [`reasoner-engine`](reasoner-engine/README.md) | `reasoner-engine` | Reasoner profiles, factory and pull/push service contracts |
| `src/main.cpp` | `driving_style_client` | Composition root (steps 1–3 today) |

`il-protocol` does not depend on the transport, so `rdf-mapper` uses its DTOs
without linking `ws-connection`.

## Configuration

`driving_style_client` reads:

| Variable | Default | Meaning |
|---|---|---|
| `MODEL_CONFIG` | required | Path to the model configuration JSON |
| `HOST_WEBSOCKET_SERVER` | `127.0.0.1` | IL WebSocket host |
| `PORT_WEBSOCKET_SERVER` | `8080` | IL WebSocket port |
| `WEBSOCKET_TARGET` | `/` | HTTP target of the WebSocket handshake |
| `VEHICLE_OBJECT_ID` | `VIN_1805` | IL instance (vehicle) to subscribe to |

The model configuration (parsed by `config-parser`) declares the input signals,
the SHACL shapes and the reasoner profile:

```json
{
  "input": {
    "signals": ["Speed", "CurrentLocation.Latitude"],
    "rdf_shapes": {
      "vehicle": "shapes/vehicle_shape.shacl.ttl",
      "signal": "shapes/signals_shapes.shacl.ttl"
    }
  },
  "reasoner_profile": "../solutions/rdf4j/driving_style_rdf4j.json"
}
```

## Build and test

The root project requires CMake 3.25 or newer and Boost 1.87 (system). It
uses installed nlohmann/json, Serd and GoogleTest when available and fetches
them otherwise. From the repository root:

```sh
cmake -S cdsp/knowledge-layer -B build/knowledge-layer -DKL_BUILD_TESTS=ON
cmake --build build/knowledge-layer
ctest --test-dir build/knowledge-layer --output-on-failure
```

The tests do not need a running IL; the WebSocket tests use a loopback server.
To run the client against a real IL:

```sh
MODEL_CONFIG=examples/driving-style/model/driving_style_reasoning.json \
  build/knowledge-layer/bin/driving_style_client
```

### RDF mapper demo

[`examples/driving-style/rdf-mapper-demo`](../../examples/driving-style/rdf-mapper-demo/)
maps sample speed and location messages with the `rdf-mapper` test fixtures and
prints the triples and serialized RDF:

```sh
examples/driving-style/rdf-mapper-demo/run_rdf_mapper_demo.sh
examples/driving-style/rdf-mapper-demo/run_rdf_mapper_demo.sh \
  path/to/fixtures VIN_12345 https://example.org/instances/ turtle
```

If Serd or nlohmann/json is not found automatically, set
`SERD_INCLUDE_DIR`/`SERD_LIBRARY` and `JSON_INCLUDE_DIR`.

## TODO

### Wire the pipeline (`src/main.cpp`)

- [ ] Build an `RDFMapper` from the model configuration's `rdf_shapes.vehicle`
      and `rdf_shapes.signal`, resolving them relative to the configuration
      file (`config-parser` keeps paths as declared).
- [ ] Choose the instance base IRI (new environment variable or model setting)
      and use `VEHICLE_OBJECT_ID` as the mapper's vehicle ID.
- [ ] In the on-message callback, map each `DataMessageDTO` with
      `signalToRdf`; log and skip a message that fails to map instead of
      stopping the client.
- [ ] Create the reasoner with `ReasonerFactory::initReasoner` from
      `reasoner_profile`, then load its ontology and rules.
- [ ] Pass each Turtle document to `ReasonerService::updateData`. Engine calls
      may block, so run them off the WebSocket strand and post results back
      through `setReasoningResultHandler`.
- [ ] Define how a reasoning result (for example the driving-style query
      columns `Vehicle_DrivingStyle_*`) becomes IL paths and values, build
      `SetRequestDTO`s and `send()` them.

### Reasoner engine

- [ ] Implement the RDF4J service (HTTP to the profile's `endpoint`) and the
      CQELS service; both are placeholders today.
- [ ] Decide how long observations stay in the engine's store. Every update
      adds triples, and the time-window rules only need recent ones.
- [ ] Add tests for `reasoner-engine` (profile parsing, factory, pull/push
      flow with a fake engine).
- [ ] Update `reasoner-engine/README.md`: it still says the result queue is
      owned by `WebSocketClient`.

### rdf-mapper

- [ ] Accept several signal shape files in one mapper, instead of one mapper
      (and one vehicle parse) per signal file.
- [ ] Parse the two shape files without sharing `@base` and blank-node labels
      (today they are concatenated into one Turtle document).
- [ ] Allow one part shape under several parents (repeated VSS branches such
      as seat rows), which the single-parent tree rule rejects.
- [ ] Support `xsd:decimal` values.
- [ ] Emit literal vehicle properties (for example `vss:id`) if the reasoner
      needs them; the vehicle graph is structural only.

### Connection

- [ ] Reconnect after the IL drops the connection; a `WebSocketClient` makes a
      single attempt, so the KL needs to create a new client and resubscribe.
- [ ] Make `send()` after a peer close non-fatal; it currently throws
      `std::logic_error`.
- [ ] Use unique request ids if responses must be matched to requests; ids
      are currently derived from method, instance, schema and path.

### Build

- [ ] Align the Boost requirement (root project: 1.87; Ubuntu 22.04 ships
      1.74) and document the minimum CMake version (3.25) for developers.

## Retired components

The legacy connector, BO/DTO layer, TripleAssembler, RDFox client and
GeographicLib integration have been removed and are not part of the build.
