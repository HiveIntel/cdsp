# Symbolic Reasoner

The symbolic-reasoner module exposes a common service contract for multiple
reasoning engines. The engine is selected by the `name` field of a JSON reasoning
profile, not by the shared model configuration.

## Services

- `ReasonerService` defines initialization, data/rule/ontology updates, and
  reasoning-result queue assignment or result-handler delivery.
- `ReasonerServicePull` implements update-then-query orchestration.
- `ReasonerServicePush` implements query-registration and callback-result
  orchestration.
- `ReasonerFactory` parses a profile and creates the matching service.

RDF4J is currently a pull-service placeholder and CQELS a push-service
placeholder. They validate profile selection only; no engine transport or
reasoning behavior is implemented yet.

When a client installs a result handler, a reasoner delivers each result through
that handler. This lets asynchronous engines hand results back without mutating
the WebSocket client's outbound queue from an engine thread.

See [services](services/README.md) for the API and
[examples](examples/README.md) for profile examples.
