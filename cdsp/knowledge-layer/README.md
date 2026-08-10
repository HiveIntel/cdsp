# Knowledge Layer

The Knowledge Layer receives vehicle messages over WebSocket, maps them to RDF,
and delegates the RDF updates to a symbolic reasoner selected by a reasoning profile.

## Reasoning flow

```text
model configuration → reasoner-profile path → ReasonerFactory → reasoner service
incoming WebSocket data → RDF mapper → ReasonerService::updateData
reasoning result JSON → WebSocketClient reply queue → sequential outgoing WebSocket messages
```

The model configuration contains signals, RDF shapes, and a `reasoner_profile`
path. The profile is resolved relative to that model configuration.

## Reasoner profiles

Profiles live beside their engine-specific resources. A profile has a `name` used
by `ReasonerFactory` to select the service, optional `ontologies` and `rules`,
and a required `query`. Each resource can be a string or an array of paths; paths
are resolved relative to the profile.

```json
{
  "name": "RDF4J",
  "ontologies": "driving_style_ontology.ttl",
  "rules": "driving_style_rules.ttl",
  "query": "driving_style_query.rq"
}
```

RDF4J and CQELS profile selection and service wiring are implemented. Their real
engine integrations are currently placeholders, so they do not yet load files,
execute queries, or publish engine results.

When an engine publishes a result, the reasoner notifies the client. The client
adds the result to its outbound queue on its WebSocket I/O context and drains the
queue one asynchronous write at a time.

## Build and test

The project requires CMake 3.25 or newer, Boost, and a C++17 compiler.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

See [symbolic-reasoner](symbolic-reasoner/README.md) for service architecture
and [driving_style](symbolic-reasoner/examples/README.md) for the active sample.
