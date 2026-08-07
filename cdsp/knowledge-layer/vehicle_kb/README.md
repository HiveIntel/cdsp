# Vehicle knowledge base

`vehicle_model.shacl` is the SHACL model generated from the Information Layer
VSS data-point configuration.

Regenerate the model with no arguments:

```bash
./generate_vehicle_model_shacl.sh
```

Create and validate a complete RDF knowledge-base graph for a vehicle:

```bash
`VIN_ID=VIN1805 ./generate_vehicle_rdf_graph.sh`
```

The latter command scans every SHACL node shape, creates one VIN-qualified RDF
resource per shape, preserves every SHACL object-property path, validates the
result with `pyshacl`, and writes `vehicle_graph.ttl` in this directory. Its
RDF types and predicates come exclusively from `vehicle_model.shacl`; the
generated `urn:vehicle:` IRIs identify the instance resources.
