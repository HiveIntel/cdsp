# Configuration parser

`config-parser` owns model-configuration file parsing only. It does not parse
Information Layer data messages, environment variables, or construct RDF.

- `ModelConfigParser` accepts `input.signals`, `input.rdf_shapes`, and
  `reasoner_profile`.
- `ModelConfig` preserves signal names, RDF-shape name/path pairs, and the
  reasoner-profile path exactly as declared in the JSON.
- The application composition root will read its WebSocket host and port
  directly from the environment in a later wiring slice.

The package is intentionally independent of WebSocket transport and reasoner
execution.
