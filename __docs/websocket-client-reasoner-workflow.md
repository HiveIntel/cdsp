# WebSocket client → VSS/RDF → reasoner connector workflow

Status: design only; no connector implementation is changed by this document.

## Decision summary

The application composition root owns both the Information Layer client and a
reasoner connector and injects the connector into the client-side orchestration.
The remote reasoner must **not** own an IL client: it would depend on a
transport and a VSS contract that belong to Knowledge Layer. The connector is
an adapter for the remote engine service, not the engine itself.

~~~mermaid
classDiagram
    class InformationLayerClient {
      +queueRequest(json)
      +onInboundWireMessage(string)
    }
    class IInboundMessageParser {
      +parse(wire) InboundMessageDto
    }
    class SubscriptionCatalog {
      +find(id) SubscriptionContext
    }
    class IVssResponseNormalizer {
      +normalize(DataMessageDto, SubscriptionContext) VssUpdate
    }
    class IRdfMapper {
      +map(VssUpdate) RdfDocument
    }
    class IReasonerConnector {
      +ingest(RdfDocument, InferenceContext)
      +setResultHandler(handler)
    }
    class IInferenceResultMapper {
      +map(result, context) vector~SetRequest~
    }
    InformationLayerClient --> IInboundMessageParser
    InformationLayerClient --> SubscriptionCatalog
    InformationLayerClient --> IVssResponseNormalizer
    InformationLayerClient --> IRdfMapper
    InformationLayerClient --> IReasonerConnector
    IReasonerConnector --> IInferenceResultMapper : callback result
    IInferenceResultMapper --> InformationLayerClient : queueRequest
~~~

The existing RdfMapper and ReasonerService interfaces are useful starting
points, but the target seams should look conceptually like:

~~~cpp
struct SubscriptionContext {
    std::string id;       // normally node name
    std::string instance;
    std::string schema;
    std::optional<std::string> path;
    il_message::ResponseOptions response;
};

class IReasonerConnector {
 public:
    using ResultHandler = std::function<void(ReasonerResult)>;
    virtual ~IReasonerConnector() = default;
    virtual void ingest(const RdfDocument&, const InferenceContext&) = 0;
    virtual void setResultHandler(ResultHandler) = 0;
};
~~~

There is deliberately no fixed data shape in DataMessageDto. The normalizer
uses SubscriptionContext to turn any flat/nested × absolute/relative payload
into canonical VSS paths before SHACL-backed RDF mapping. This is required
because a payload alone cannot always reveal the requested format/root.

## Inbound data path

~~~mermaid
sequenceDiagram
    participant IL as Information Layer
    participant WS as IL WebSocket client
    participant P as Inbound parser
    participant C as Subscription catalog
    participant N as VSS normalizer + SHACL RDF mapper
    participant R as Reasoner connector

    IL->>WS: JSON-RPC response or update
    WS->>P: parse wire payload
    alt StatusMessageDto
        P-->>WS: acknowledgement or error
        WS->>WS: update request/subscription lifecycle :  log error
    else DataMessageDto
        P-->>WS: instance + raw data + metadata + ID
        WS->>C: find context by node-name ID
        C-->>WS: path + format + root + schema
        WS->>N: normalize VSS and map with SHACL
        N->>R: RDF document + inference context
    end
~~~

Status is never sent to RDF mapping. Data validation failures, unrecognised
subscription IDs, and remote connector errors are operational events; they
must not stop the WebSocket read loop.

The catalog is not the old numeric request registry: it holds the durable
configuration of active subscriptions. With ID = node name, request
correlation is direct. It must enforce uniqueness for a connection, or include
instance/schema in the configured ID policy for a multi-instance connection.

## Push reasoner connector

Push engines register a remote continuous query once. An IL update causes an
RDF ingest; inferred output arrives asynchronously through the connector
result handler.

~~~mermaid
sequenceDiagram
    participant WS as IL client
    participant X as worker/executor
    participant RP as push reasoner connector
    participant O as inference result mapper
    participant IL as Information Layer

    WS->>X: RDF update (post: do not block IO)
    X->>RP: ingest RDF
    RP-->>X: asynchronous inference result
    X->>O: map result to SetRequest
    O-->>WS: enqueue SetRequest on IO context
    WS->>IL: set AI.Reasoner.InferenceResults
~~~

The default target is schema Vehicle, path AI.Reasoner, data key
InferenceResults, which resolves to Vehicle.AI.Reasoner.InferenceResults in
the supplied VSS schema. Attach Node::Metadata origin SemanticReasoner and a
generated timestamp. Keep this target out of the subscription set, otherwise
the connector can feed its own output back into the reasoner.

The result mapper should be profile/config driven rather than assume every
reasoner result is a string. It may emit one SET per result or batch a bounded
group, but each output must remain a valid IL SetRequest with a traceable ID.

## Pull reasoner connector

Pull engines must be queried after each accepted RDF update (or after an
explicit debounce/window configured by the profile).

~~~mermaid
sequenceDiagram
    participant WS as IL client
    participant X as worker/executor
    participant RP as pull reasoner connector
    participant O as inference result mapper
    participant IL as Information Layer

    WS->>X: post normalized RDF update
    X->>RP: ingest RDF
    X->>RP: query configured inference
    RP-->>X: finite result rows
    loop each mapped result
        X->>O: map row to SetRequest
        O-->>WS: post outbound write
        WS->>IL: SET inference result
    end
~~~

The current ReasonerServicePull already expresses the ingest-then-query
sequence, and ReasonerServicePush already expresses callback publication.
Their names may remain implementation details of IReasonerConnector; neither
should force raw JSON directly into a WebSocket write queue.

## Threading and ownership rules

1. Keep one WebSocket writer queue and one read loop. All outbound SETs post
   back to that IO context.
2. Parse lightweight JSON on the IO path; dispatch VSS normalization, RDF
   generation, and all remote calls to a bounded worker/executor.
3. Retain only weak/managed references in asynchronous callbacks so teardown
   cannot call a destroyed client.
4. Treat StatusMessageDto errors as lifecycle/observability signals. Do not
   map them to RDF and do not retry non-idempotent SET automatically.
5. Store input source ID and request/subscription context in InferenceContext,
   so outgoing results can be traced without a global legacy registry.

## Incremental implementation path

1. Use InboundMessageParser in InformationLayerClient and route status versus
   data without changing its transport behavior.
2. Add SubscriptionCatalog when creating SubscribeRequest instances.
3. Add IVssResponseNormalizer; keep flat + relative as the active initial
   policy, then test all four response variants through the normalizer.
4. Wrap the existing pull/push reasoner services behind IReasonerConnector.
5. Add the result mapper and controlled SET writes to AI.Reasoner.InferenceResults.
