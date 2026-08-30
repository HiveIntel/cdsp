# Knowledge Layer

The active driving-style path is:

~~~text
Information Layer WebSocket
  -> il_connection
  -> rdf_mapper (SHACL/VSS to Turtle)
  -> reasoner_engine (HTTP RDF4J adapter)
  -> Information Layer SET AI.Reasoner.InferenceResults
~~~

## Modules

- il_message: outbound JSON-RPC requests and inbound data/status DTO parsing.
- il_connection: serialized WebSocket transport and subscriptions.
- rdf-mapper: VSS response to SHACL-backed RDF mapping.
- config_parser: driving-style model configuration.
- reasoner_engine: reasoner profiles and pull/push connector contracts.

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
