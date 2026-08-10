# Symbolic-Reasoner Examples

The active example is [driving_style](driving_style/). Its model configuration
references a reasoner profile:

```json
{
  "reasoner_profile": "solution_rdf4j/driving_style_rdf4j.json"
}
```

The profile determines the engine and references its ontology, rule, and query
files. `solution_rdf4j` provides an RDF4J profile; `solution_cqels` provides a
CQELS profile. Profile resource paths are relative to the profile file.

The data simulator can generate and replay vehicle scenes independently of the
reasoner service. See its [README](driving_style/data_simulator/README.md).
