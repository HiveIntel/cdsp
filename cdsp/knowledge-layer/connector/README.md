# Connector

The connector owns the boundary between the Information Layer WebSocket protocol
and the Knowledge Layer.

1. `websocket-client` receives subscribed vehicle messages.
2. `rdf-mapper` converts supported signals to RDF.
3. The selected `ReasonerService` receives the RDF update.
4. Reasoner-result JSON is appended to the WebSocket client reply queue and is
   sent as an outbound WebSocket message.

Reasoner selection belongs to the reasoning profile referenced by the model
configuration; the connector does not select an engine itself.
