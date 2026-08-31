# 0015 — Name the default WebSocket transport `AbstractWebSocketConnection`

## Decision

Rename the default Information Layer WebSocket transport implementation from
`RealWebSocketConnection` / `real_websocket_connection.*` to
`AbstractWebSocketConnection` / `abstract_websocket_connection.*`.

## Consequences

The class remains the concrete Boost.Beast implementation of
`WebSocketClientInterface`; the rename changes no WebSocket behaviour or wire
contract. Consumers that construct the default transport must use the new C++
class and file names.
