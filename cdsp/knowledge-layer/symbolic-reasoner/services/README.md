# Reasoner Services

## Contract

Every `ReasonerService` is initialized from a parsed `ReasonerProfile` and
provides:

- `initialize()`
- `updateData(data, syntax)`
- `updateRules(rulePaths)`
- `updateOntology(ontologyPaths)`
- `setReasoningResultMessageQueue(queue)`
- `setReasoningResultHandler(handler)`

The result queue is owned by `WebSocketClient` and contains outbound JSON
messages. A service using queue delivery publishes only after the client assigns
that queue. For asynchronous engine callbacks, the client instead installs a
result handler; the handler schedules the queue update on the client's WebSocket
I/O context.
The queue setter remains available as the fallback for callers without a handler.

## Profiles

`ReasonerProfileParser` reads a profile file, checks its resources, and resolves
relative paths against the profile directory. `name` selects an engine in
`ReasonerFactory`; `ontologies` and `rules` are optional; `query` is required.
String and array forms are both accepted for resource fields.

## Pull and push bases

`ReasonerServicePull` owns the sequence:

```text
initialize → update engine data → query engine → publish each result
```

`ReasonerServicePush` owns the sequence:

```text
initialize → register profile queries → submit data → publish callback result
```

Concrete engines implement only their engine-specific hooks. RDF4J and CQELS
classes exist as placeholders and deliberately return failure for actual engine
operations until their integrations are implemented.
