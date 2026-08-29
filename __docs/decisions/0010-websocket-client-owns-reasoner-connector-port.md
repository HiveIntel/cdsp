# ADR 0010 — WebSocket orchestration owns a reasoner connector port

Status: proposed
Date: 2026-08-29

## Context

Reasoning engines run as separate services. The Knowledge Layer WebSocket
client receives IL data, maps VSS values to RDF, sends RDF to a remote
reasoner, and writes inferred results back to IL. The current
InformationLayerClient directly holds a ReasonerService and forwards raw JSON
to the mapper, which combines protocol parsing, RDF mapping, and engine
coordination in one callback.

## Decision

Use a composition root to construct the IL client and an injected
IReasonerConnector port. The client-side orchestration owns the connector
reference and registers an output callback; the remote reasoner does not own
or depend on an IL client. The connector may implement either pull or push
semantics behind the same port.

Before RDF mapping, parse the inbound IL envelope into DataMessageDto or
StatusMessageDto. A subscription catalog, keyed by the node-name request ID,
stores schema/path/format/root to normalize every legal response shape.

Map reasoner outputs through an explicit InferenceResultMapper into SetRequest
objects. The default VSS target is Vehicle.AI.Reasoner.InferenceResults, with
origin metadata SemanticReasoner. Do not subscribe to this target, preventing
an output-to-input feedback loop.

## Alternatives considered

1. Let the remote reasoner own IL client code. This couples the remote engine
   to the IL transport and reverses the deployment boundary.
2. Keep raw WebSocket JSON flowing to the RDF mapper. This cannot reliably
   distinguish status/error envelopes or interpret all format/root shapes.
3. Use a transient request registry for every update. Persistent subscription
   context is necessary, but node-name IDs allow a small subscription catalog
   instead of a legacy numeric request registry.

## Consequences

The design is an interface/workflow only in this change; no remote protocol or
runtime topology is changed. Future implementation must keep WebSocket I/O
non-blocking by posting mapping and reasoner work to an executor, then posting
outbound SET requests back onto the WebSocket write queue.
