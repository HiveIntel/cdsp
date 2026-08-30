# IL Connection package

This package is an unintegrated extraction of the RDF-aware WebSocket client
from `stream_reasoner_service_integration`.

Its active inbound path is:

`WebSocket message -> RdfMapper::mapMessageToRdf -> ReasonerService::updateData`

`il_connection` is registered by the top-level CMake file. It creates outgoing
JSON-RPC payloads directly from `il_message` and does not depend on legacy
`SystemConfig`, `BoService`, or `BoToDto` services.
