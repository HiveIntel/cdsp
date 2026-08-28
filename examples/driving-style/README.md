# Driving-Style Reasoning Example

`driving_style_reasoning.json` is the model configuration used by the client.
It declares vehicle signals, SHACL files, and one `reasoner_profile` link.

## RDF4J profile

`solution_rdf4j/driving_style_rdf4j.json` selects RDF4J and references:

- `driving_style_ontology.ttl`
- `driving_style_rules.ttl`
- `driving_style_query.rq`

## CQELS profile

`solution_cqels/driving_style_cqels.json` selects CQELS and references its
continuous query. Ontology and rule fields are optional for profiles that do
not need them.

The profile parser resolves all of these paths relative to the profile JSON.
RDF4J and CQELS integration code is currently placeholder-only, so this example
validates configuration and wiring but does not execute reasoning yet.

Use the [data simulator](data_simulator/README.md) to generate and replay sample
vehicle data.
