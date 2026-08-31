# Knowledge Layer architecture

## Runtime data flow

```mermaid
flowchart LR
    IL[Information Layer<br/>VISS-style JSON-RPC WebSocket]
    APP[driving_style_client]
    CONN[il-connection<br/>InformationLayerClient]
    REQUEST[il-request<br/>outbound requests + inbound DTO parser]
    MAPPER[rdf-mapper<br/>SHACL/VSS → RDF/Turtle]
    ENGINE[reasoner-engine<br/>ReasonerService]
    RDF4J[RDF4J HTTP service]

    APP --> CONN
    CONN -->|SUBSCRIBE / SET| REQUEST
    IL -->|data or status JSON-RPC response| CONN
    CONN -->|parse response| REQUEST
    CONN -->|VSS JSON| MAPPER
    MAPPER -->|RDF| ENGINE
    ENGINE -->|HTTP ingest + query| RDF4J
    RDF4J -->|inference JSON| ENGINE
    ENGINE -->|result callback| CONN
    CONN -->|SET Vehicle.AI.Reasoner.InferenceResults| IL
```

The maintained runtime uses the pull-based `RDF4JReasonerService`. The CQELS
class is a push-based extension point and currently a placeholder.

## Application, connection, and IL request classes

```mermaid
classDiagram
    class DrivingStyleClient {
        <<executable>>
        +main(argc, argv)
    }
    class ModelConfigParser {
        +parseFile(path) ModelConfig
    }
    class ModelConfig {
        +getSignals() vector~string~
        +getRdfShapes() map~string,string~
        +getReasonerProfile() string
    }
    class InformationLayerClient {
        +initializeConnection()
        +run()
        +queueRequest(json)
        +queueRequests(vector~json~)
        -processMessage(message)
        -enqueueReasoningResult(json)
    }
    class WebSocketClientInterface {
        <<interface>>
        +asyncResolve(host, port)
        +asyncConnect()
        +asyncHandshake()
        +asyncWrite(json)
        +asyncRead()
    }
    class AbstractWebSocketConnection {
        +asyncResolve(host, port)
        +asyncConnect()
        +asyncHandshake()
        +asyncWrite(json)
        +asyncRead()
    }
    class InboundMessageParser {
        +parse(json) InboundMessageDto
        +parseWire(text) InboundMessageDto
    }
    class Request {
        <<abstract>>
        +getInstance() string
        +getSchema() string
        +getRequestId() string
        +getMethod() string_view
    }
    class GetRequest
    class SetRequest
    class SubscribeRequest
    class UnsubscribeRequest
    class Node {
        +name
        +value?
        +Metadata metadata
    }
    class ResponseOptions {
        +ResponseFormat format
        +ResponseRoot root
    }
    class SetOptions {
        +path?
        +SyncMode sync?
        +timeseries?
    }
    class DataMessageDto {
        +MessageId id
        +string instance
        +json data
        +json metadata?
    }
    class StatusMessageDto {
        +MessageId id
        +StatusErrorDto error?
    }

    DrivingStyleClient --> ModelConfigParser
    ModelConfigParser --> ModelConfig
    DrivingStyleClient --> InformationLayerClient
    InformationLayerClient o--> WebSocketClientInterface
    AbstractWebSocketConnection ..|> WebSocketClientInterface
    InformationLayerClient --> AbstractWebSocketConnection : default transport
    InformationLayerClient --> InboundMessageParser
    Request <|-- GetRequest
    Request <|-- SetRequest
    Request <|-- SubscribeRequest
    Request <|-- UnsubscribeRequest
    GetRequest o--> Node
    SubscribeRequest o--> Node
    UnsubscribeRequest o--> Node
    SetRequest *--> Node : one or many
    GetRequest --> ResponseOptions
    SubscribeRequest --> ResponseOptions
    SetRequest --> SetOptions
    InboundMessageParser --> DataMessageDto
    InboundMessageParser --> StatusMessageDto
```

`il-request` is the module and CMake target name. The public C++ include path
and namespace stay `il_request`, because hyphens are not valid C++ identifiers.

## RDF mapping and reasoning classes

```mermaid
classDiagram
    class InformationLayerClient {
        -processMessage(message)
        -enqueueReasoningResult(json)
    }
    class RdfMapper {
        +mapMessageToRdf(json) string
    }
    class ShaclShapeParser {
        +loadFiles(paths) ShapeRegistry
        +loadDirectory(path) ShapeRegistry
    }
    class RdfGenerator {
        +generate(InputMessage, RdfFormat) vector~RdfTriple~
    }
    class RDFWriter {
        +write(vector~RdfTriple~, RdfFormat) string
    }
    class ShapeRegistry
    class InputMessage
    class RdfTriple
    class ReasonerFactory {
        +initReasoner(profilePath) shared_ptr~ReasonerService~
    }
    class ReasonerProfileParser {
        +parse(profilePath) ReasonerProfile
    }
    class ReasonerProfile {
        +name
        +ontologies
        +rules
        +queries
        +Endpoint endpoint
    }
    class ReasonerService {
        <<abstract>>
        +initialize() bool
        +updateData(rdf, syntax) bool
        +updateRules(paths) bool
        +updateOntology(paths) bool
        +setReasoningResultHandler(handler)
    }
    class ReasonerServicePull {
        <<abstract>>
        +initialize() bool
        +updateData(rdf, syntax) bool
        #queryReasoner() vector~json~
    }
    class ReasonerServicePush {
        <<abstract>>
        +initialize() bool
        +updateData(rdf, syntax) bool
        #submitDataToReasoner(rdf, syntax) bool
    }
    class RDF4JReasonerService {
        -request(method, target, contentType, accept, body) string
        -queryReasoner() vector~json~
    }
    class CQELSReasonerService {
        <<placeholder>>
        #submitDataToReasoner(rdf, syntax) bool
    }

    InformationLayerClient o--> RdfMapper
    InformationLayerClient o--> ReasonerService
    RdfMapper *--> ShapeRegistry
    RdfMapper *--> RdfGenerator
    RdfMapper *--> RDFWriter
    ShaclShapeParser --> ShapeRegistry
    RdfGenerator --> ShapeRegistry
    RdfGenerator --> InputMessage
    RdfGenerator --> RdfTriple
    RDFWriter --> RdfTriple
    ReasonerFactory --> ReasonerProfileParser
    ReasonerProfileParser --> ReasonerProfile
    ReasonerFactory --> ReasonerService
    ReasonerService <|-- ReasonerServicePull
    ReasonerService <|-- ReasonerServicePush
    ReasonerServicePull <|-- RDF4JReasonerService
    ReasonerServicePush <|-- CQELSReasonerService
    ReasonerService --> InformationLayerClient : result handler callback
```

## Ownership rules

- `InformationLayerClient` owns the runtime orchestration: WebSocket lifecycle,
  request/reply queues, `RdfMapper`, and one `ReasonerService` instance.
- `RdfMapper` owns parsed shape state and is stateless per input message.
- `ReasonerService` does not own the client. It publishes normalized inference
  JSON through the callback that the client supplies.
- `il-request` is DTO/serialization only; it does not open sockets or retain
  subscriptions.
