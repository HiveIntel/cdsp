# ADR 0009 — Separate outbound IL requests from inbound data and status messages

Status: accepted
Date: 2026-08-29

## Context

The previous standalone API called outbound JSON-RPC operations `GetMessage`,
`SetMessage`, `SubscribeMessage`, and `UnsubscribeMessage`, while the
WebSocket client passed every inbound JSON string directly to the RDF mapper.
That obscures the protocol direction: get/set/subscribe/unsubscribe are
requests, whereas IL sends a data response/update or a status acknowledgement
or error.

The deployed IL permits string, integer, or null JSON-RPC IDs in responses.
It emits a data envelope as `result.instance`, `result.data`, and optional
`result.metadata`; an acknowledgement as `result: {}`; and a failure as an
`error` object. `result.data` has no one fixed JSON shape: all four
`format × root` choices, including a scalar nested-relative leaf, are valid.

## Decision

- Name outbound types `Request`, `GetRequest`, `SetRequest`,
  `SubscribeRequest`, and `UnsubscribeRequest`. `Request` is abstract and
  owns the shared JSON-RPC request context.
- Model inbound protocol messages separately as `DataMessageDto` and
  `StatusMessageDto`. `StatusMessageDto` represents both successful
  acknowledgements and JSON-RPC errors.
- `InboundMessageParser` classifies a validated envelope by `error`, then
  `result.data`, then an acknowledgement `result`. A data DTO preserves data
  and metadata as JSON without attempting to infer `format` or `root`.
- Preserve response IDs as `MessageId` (`string`, integer, or null). Node-name
  IDs remain the normal active-client policy, but the parser reflects the IL
  contract rather than assuming it.
- A later RDF-normalization layer must receive the original request or
  subscription context (path, schema, format, and root). Shape cannot always
  be inferred from response JSON alone.

## Alternatives considered

1. Keep the outbound `*Message` names and add only DTOs. This leaves an
   ambiguous API where a "message" can mean either direction.
2. Parse directly into a fixed node/value structure. This loses legal branch,
   flat, and scalar response representations.
3. Copy the legacy DTOs. They require an integer ID and discard
   `result.instance`, so they do not represent the deployed contract or the
   direct node-name ID policy.

## Consequences

The public request class names are intentionally changed; this isolated WIP
package has no compatibility aliases. Callers must include the new
`*_request.h` headers. Inbound handling can ignore status acknowledgements for
RDF mapping while retaining failures and request IDs for lifecycle reporting.

## Validation and rollback

Unit tests cover every legal response data representation, success and error
statuses, string/integer/null IDs, and malformed envelopes. Rollback restores
the prior class names and direct raw-string mapper path only; it does not alter
the Information Layer contract.
