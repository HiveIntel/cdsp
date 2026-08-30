# Knowledge Layer

The active driving-style path is:

~~~text
Information Layer WebSocket
  -> il-connection
  -> rdf_mapper (SHACL/VSS to Turtle)
  -> reasoner-engine (HTTP RDF4J adapter)
  -> Information Layer SET AI.Reasoner.InferenceResults
~~~

## Modules

- il-request: outbound JSON-RPC requests and inbound data/status DTO parsing
  (`il_request` is its C++ include path and namespace).
- il-connection: serialized WebSocket transport and subscriptions.
- rdf-mapper: VSS response to SHACL-backed RDF mapping.
- config-parser: driving-style model configuration.
- reasoner-engine: reasoner profiles and pull/push connector contracts.

The legacy connector, BO/DTO, TripleAssembler, RDFox client, GeographicLib,
and their test fixtures have been retired. They are not part of the supported
build or runtime path.

## Build and run

Use the top-level tools:

~~~bash
tools/run-rdf4j-reasoner.sh
tools/run-driving-style-client.sh
~~~

The deterministic data generator and Information Layer launcher are described
in the driving-style example README.

See the [architecture and class diagrams](../../__docs/knowledge-layer-architecture.md)
for module responsibilities, ownership, and the runtime data flow.
