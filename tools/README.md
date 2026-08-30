# Demo tools

- `run-rdf4j-reasoner.sh`: builds and starts the top-level RDF4J HTTP service
  on port 12110 (override with `RDF4J_PORT`).
- `run-driving-style-client.sh`: builds the C++ client image and connects it to
  Information Layer and RDF4J using host networking.
- `run-driving-style-demo.sh`: starts the launcher-owned Information Layer and generates
  the deterministic two-event scene. Start the reasoner/client in separate
  terminals, then replay `scene01` as printed by the script.
- `run-remotivelabs-bridge.sh`: runs the existing RemotiveLabs bridge with
  credentials only from environment variables. Set `RL_BROKER_URL`, either
  `RL_API_KEY` or `RL_ACCESS_TOKEN`, and `VEHICLE_OBJECT_ID`; pass the VSS
  signals as arguments, for example `vss:Vehicle.Speed`.

The demo intentionally does not embed RemotiveLabs secrets in scripts or
configuration files.
