# Knowledge Layer

The active executable path is:

~~~text
Information Layer WebSocket
  -> ws-connection (text transport)
  -> il-connection (request DTOs and typed inbound parsing)
  -> stdout (DATA / STATUS / ERROR)
~~~

## Modules

- ws-connection: reusable asynchronous text WebSocket transport.
- il-connection: Information Layer request DTOs and typed inbound parsing. The
  compatibility target `il-connection-protocol` exposes the same library to
  `rdf-mapper` without coupling it to the WebSocket transport.
- rdf-mapper: VSS response to SHACL-backed RDF mapping.
- config-parser: driving-style model configuration.
- reasoner-engine: reasoner profiles and pull/push connector contracts.

The legacy connector, BO/DTO, TripleAssembler, RDFox client, GeographicLib,
and their test fixtures have been retired. They are not part of the supported
build or runtime path.

## Build and run

Configure and build with CMake 3.25 or newer. To keep generated files under the
IL connection test package, use its standalone test project:

~~~bash
cmake -S cdsp/knowledge-layer/connection/il-connection/tests \
  -B cdsp/knowledge-layer/connection/il-connection/tests/build
cmake --build cdsp/knowledge-layer/connection/il-connection/tests/build
ctest --test-dir cdsp/knowledge-layer/connection/il-connection/tests/build \
  --output-on-failure
~~~

The real Information Layer is an external service and is not started by these
tests. `driving_style_client` connects to it, subscribes, and prints inbound
messages; the connection environment variables are documented in the
`il-connection` README.

To build and run the standalone RDF mapper demo with the SHACL files under
`rdf-mapper/tests/fixtures/rdf_mapper_shacl` and the default signal message
`rdf-mapper/tests/fixtures/rdf_mapper_signal/speed_message.json`:

~~~bash
cdsp/knowledge-layer/rdf-mapper/run_rdf_mapper_demo.sh
~~~

Pass a SHACL file or directory and an Information Layer signal-message JSON file
to use custom inputs:

~~~bash
cdsp/knowledge-layer/rdf-mapper/run_rdf_mapper_demo.sh \
  path/to/shapes.ttl path/to/signal-message.json
~~~

The demo currently exercises SHACL loading, mapping compilation, message
parsing, and triple generation. It will report empty serialized output until
`RDFWriter` is implemented.

See the [architecture and class diagrams](../../__docs/knowledge-layer-architecture.md)
for module responsibilities, ownership, and the runtime data flow.
