# ADR 0008 — Use one path per read/subscription request and make metadata part of Node

Status: accepted
Date: 2026-08-29

## Context

The deployed Information Layer validates `get`, `subscribe`, and `unsubscribe`
with one optional string `params.path`; it has no `paths` array. The legacy
connector therefore stored `vector<Node>` in a business object but expanded it
into one JSON-RPC DTO per node before sending. That hid the actual wire
cardinality and required a numeric request registry for response correlation.

`Metadata` is meaningful only as an attribute of an individual `Node`, but was
implemented as a separate public class.

## Decision

- `GetRequest`, `SubscribeRequest`, and `UnsubscribeRequest` represent exactly
  one optional path. A node request serializes to exactly one JSON-RPC payload.
- For node requests, the request ID is the node name. Root requests require a
  caller-provided ID because they have no node name.
- Keep `vector<Node>` only in `SetRequest`, where one IL `set` request supports
  an object containing multiple data values.
- Move metadata into `Node::Metadata`, a nested value struct. There is no
  top-level `Metadata` class or header.
- Model all IL-supported response shaping for get/subscribe: `nested`/`flat`
  format and `absolute`/`relative` root.

## Alternatives considered

1. Retain vectors on all message classes and expand during serialization. This
   repeats legacy indirection and makes the class appear to represent a
   multi-path wire request when it does not.
2. Send a JSON-RPC batch. The deployed IL validator accepts one request object,
   not a JSON-RPC batch or a `paths` parameter.
3. Retain a separate metadata class. This adds a top-level type without a
   lifecycle independent of its node.

## Consequences

Responses can be matched directly with their string JSON-RPC ID; the legacy
request registry is not needed for active per-node get/subscription flows.
Node-name IDs must still be unique on one WebSocket connection. A multi-node
SET has one acknowledgement and therefore needs an explicit batch ID.

## Validation and rollback

Serialization tests verify one-path payloads, node-name IDs, all four
format/root combinations, set batching, and metadata encoding. The deployed
IL RequestValidator and HandlerBase formatting tests remain the contract
evidence. Rollback restores vector expansion only in the isolated KL package;
the IL WebSocket contract is unchanged.
