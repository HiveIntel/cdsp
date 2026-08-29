# ADR 0007 — Model Information Layer requests with an abstract request base

Status: accepted
Date: 2026-08-29

## Context

The standalone `il_message` package had four concrete message classes. Each
duplicated a `MessageHeader` containing only the IL instance and duplicated its
own request context. `schema` was hard-coded as `Vehicle` during serialization.
The design made the common request context unclear and did not provide a stable
public include layout for the direct `il_connection` client.

## Superseded naming

ADR 0009 changes the public names to `Request` and `*Request` so that
"message" is reserved for inbound `DataMessageDto` and `StatusMessageDto`.
The ownership, validation, and cardinality decisions below remain in force.

## Decision

- Replace `MessageHeader` with abstract `il_message::Request`.
- Let `Request` validate and own the shared `instance`, `request ID`, and
  `schema`. ADR 0008 defines the concrete node cardinality.
- Make `GetRequest`, `SetRequest`, `SubscribeRequest`, and
  `UnsubscribeRequest` final subclasses. Each supplies its JSON-RPC method and
  retains its concrete `to_json` overload.
- Publish the package under `il_message/include/il_message/` and use the
  `il_message` namespace.
- Keep JSON-RPC cardinality concrete: get/subscribe/unsubscribe represent one
  path; set may represent one batched data object (ADR 0008).
- Expose `InformationLayerClient::queueRequest(s)` as the public sending API so
  generated requests are serialized on the client's existing write queue.

## Alternatives considered

1. Retain a separate `MessageHeader`. This preserves old call sites but leaves
   a header that is not a protocol header and retains duplicated state.
2. Use a concrete `Request` with an optional operation. This permits invalid
   messages without a JSON-RPC method and weakens type-specific serialization.
3. Use only free functions and structs. This is lightweight but loses central
   validation and a common request abstraction.

## Consequences

The public construction API takes instance, request context, and optional
schema directly. Callers must qualify types with `il_message::`. There is no
compatibility adapter for `MessageHeader` because `il_message` is an isolated
new package and no supported external API has been released from it.

Callers enqueue serialized payloads rather than initiating raw WebSocket writes;
this preserves the single-write-in-flight invariant of the transport.

## Validation and rollback

The package serialization tests cover each JSON-RPC method, metadata emission,
default schema, explicit schema, and the abstract base. ADR 0008 adds the
single-path and response-format coverage.
Rollback is a source-only reversal of this package and its direct
`il_connection` consumer; it does not change the IL WebSocket contract.
