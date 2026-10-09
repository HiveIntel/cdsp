# Driving-style reasoning example

This example keeps each artifact by its runtime role:

```text
driving-style/
├── model/                 # client model configuration and RDF input shapes
│   ├── driving_style_reasoning.json
│   └── shapes/
├── solutions/             # one reasoning profile per engine
│   ├── cqels/
│   └── rdf4j/
├── data-generator/        # scene definitions, generator, and stream player
├── launch/                # isolated IL/KL launch artifacts
└── README.md
```

`model/driving_style_reasoning.json` is the entry configuration. It declares
the VSS signals, the SHACL shapes in `model/shapes/`, and the selected reasoner
profile. Paths are resolved relative to the configuration/profile that contains
them.

The RDF4J profile is at
`solutions/rdf4j/driving_style_rdf4j.json`; CQELS has its equivalent under
`solutions/cqels/`. Engine runtime integration is still placeholder-only, so
the example currently validates configuration and client wiring rather than
executing inference.

Use the [data generator](data-generator/README.md) to create and replay sample
vehicle data.

## Isolated launchers

The launcher artifacts are intentionally separate from the default CDSP Docker
Compose stack:

- `launch/launch_information_layer.sh` first checks the configured WebSocket
  endpoint (`127.0.0.1:8080` by default). If it is reachable, it leaves that
  endpoint unchanged; otherwise it starts only the IL and IoTDB stack.
- `launch/launch_knowledge_layer.sh` runs the IL check, then invokes the
  driving-style client launcher in `tools/`, which builds and starts KL.
  The runtime image disables test targets and retains CMake/ccache build
  caches, so subsequent builds compile only changed C++ translation units.

From the repository root:

```bash
examples/driving-style/launch/launch_knowledge_layer.sh
```

The default model configuration is
`/src/examples/driving-style/model/driving_style_reasoning.json` inside the
Knowledge Layer container, and the default vehicle instance is `VIN_1805`,
matching the generator. `MODEL_CONFIG` is required by the executable and is
set to this default by the container. Set it to another path available inside
the container when overriding it. Set `VEHICLE_OBJECT_ID`, `HOST_WEBSOCKET_SERVER`, or
`PORT_WEBSOCKET_SERVER` to override the endpoint settings. `KL_SOURCE_REF`
selects another local Git ref.

Inference results are written to `Vehicle.AI.Reasoner.InferenceResults` as a
serialized JSON value, the single output node defined by the Information Layer
contract.
