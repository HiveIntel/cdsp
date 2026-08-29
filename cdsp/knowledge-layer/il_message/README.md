# Information Layer request and inbound-message API

il_message represents the deployed Information Layer's VISS-style JSON-RPC
contract. It has a directional API:

~~~mermaid
flowchart LR
    R[Outbound Request] --> W[WebSocket write]
    W --> IL[Information Layer]
    IL --> P[InboundMessageParser]
    P --> D[DataMessageDto]
    P --> S[StatusMessageDto]
    D --> M[context-aware VSS to RDF mapping]
    S --> L[ack/error lifecycle handling]
~~~

The external contract evidence is the read-only Information Layer
RequestValidator.ts, MessageMapper.ts, and HandlerBase.format.test.ts.
Legacy DTOs were inspected for intent but not copied: they assume an integer
ID and omit result.instance, which conflicts with the current node-name ID
policy.

## Outbound requests

Request is an abstract base that owns instance, schema, and request ID.
The concrete outbound operations are GetRequest, SetRequest,
SubscribeRequest, and UnsubscribeRequest; each serializes one JSON-RPC
object with to_json.

| Request | Wire cardinality | IL options |
| --- | --- | --- |
| GetRequest | one optional path | format: nested/flat; root: absolute/relative |
| SubscribeRequest | one optional path | format: nested/flat; root: absolute/relative |
| UnsubscribeRequest | one optional path | no format/root |
| SetRequest | one data object; may contain several nodes | path, sync: off/local/remote, timeseries: bool, per-node metadata |

All requests require jsonrpc: "2.0", id, params.instance, and params.schema
(default "Vehicle"). GET defaults to nested + relative; SUBSCRIBE defaults to
nested + absolute.

The deployed IL has one optional string params.path, not a paths array or
JSON-RPC batch. Therefore read/subscribe/unsubscribe use one Node per
request, and their default request ID is that node name. A root request needs
an explicit ID. SetRequest is the one valid multi-node case because
params.data is an object; its acknowledgement uses one explicit batch ID.

~~~cpp
#include "il_message/subscribe_request.h"

const il_message::SubscribeRequest speed{
    "VIN_1805",
    il_message::Node{"Speed"},
    {il_message::ResponseFormat::FLAT, il_message::ResponseRoot::RELATIVE}};
client->queueRequest(il_message::to_json(speed));  // id == "Speed"
~~~

For reasoner output, the supplied VSS schema contains
Vehicle.AI.Reasoner.InferenceResults. A valid single write is therefore:

~~~cpp
const il_message::SetRequest result{
    "VIN_1805",
    il_message::Node{"InferenceResults", inference_json.dump()},
    il_message::SetOptions{std::string{"AI.Reasoner"},
                           il_message::SyncMode::REMOTE, std::nullopt}};
client->queueRequest(il_message::to_json(result));
~~~

Node::Metadata is nested in Node, so it cannot be confused with a
message-level type. It supports received/generated timestamps, origin
name/URI, and deductive/probabilistic confidence.

## Inbound responses: data versus status

InboundMessageParser::parseWire(...) returns a variant of DataMessageDto and
StatusMessageDto.

| Wire envelope | Parsed DTO | Meaning |
| --- | --- | --- |
| result.instance + result.data | DataMessageDto | GET value or subscription update; preserves data and optional metadata JSON exactly. |
| result: {} | successful StatusMessageDto | acknowledgement for SET/SUBSCRIBE/UNSUBSCRIBE (and any non-data success). |
| error: { code, message, data? } | failing StatusMessageDto | JSON-RPC/protocol failure. |

Every DTO keeps MessageId = string | int64 | null, since the IL response
contract allows those forms. Active per-node flows normally use a string such
as "Speed", but parser correctness does not rely on that convention.

~~~cpp
const auto inbound = il_message::InboundMessageParser::parseWire(wire_text);
if (const auto* data = std::get_if<il_message::DataMessageDto>(&inbound)) {
    // Pass data->data plus the subscription context to the VSS/RDF normalizer.
} else {
    const auto& status = std::get<il_message::StatusMessageDto>(inbound);
    // status.isSuccess() distinguishes acknowledgement from error.
}
~~~

## All legal GET/SUBSCRIBE data shapes

For requested branch CurrentLocation containing Latitude 60 and Longitude 50,
the Information Layer returns:

| format | root | result.data |
| --- | --- | ---|
| flat | relative | { "Latitude": 60, "Longitude": 50 } |
| flat | absolute | { "CurrentLocation.Latitude": 60, "CurrentLocation.Longitude": 50 } |
| nested | relative | { "Latitude": 60, "Longitude": 50 } |
| nested | absolute | { "CurrentLocation": { "Latitude": 60, "Longitude": 50 } } |

The leaf edge case CurrentLocation.Latitude is important:

| format | root | result.data |
| --- | --- | ---|
| flat | relative | { "": 60 } |
| flat | absolute | { "CurrentLocation.Latitude": 60 } |
| nested | relative | 60 |
| nested | absolute | { "CurrentLocation": { "Latitude": 60 } } |

Metadata is always flat in the deployed IL, irrespective of format. With
relative root it has keys such as "Latitude"; with absolute root it has
"CurrentLocation.Latitude".

The parser deliberately does **not** infer format or root: a branch's
flat-relative and nested-relative forms may be indistinguishable. It accepts
objects, arrays, scalars, and null data without changing them. The RDF stage
must use the saved request/subscription context {schema, path, format, root}
to normalize a DataMessageDto. The existing active client currently chooses
flat + relative specifically because its older mapper expects that shape; the
new parser makes every legal shape visible without silently dropping it.

## Request IDs and subscriptions

Node-name request IDs remove the legacy numeric correlation registry for
one-node requests. They must still be unique per WebSocket connection; an
active subscription needs its persistent context even after its initial
acknowledgement. Keep a small subscription catalog keyed by node name for
path/format/root/schema, not a registry of transient numeric requests.

See ADR 0008 for the one-path decision and ADR 0009 for the directional
request/response boundary.
