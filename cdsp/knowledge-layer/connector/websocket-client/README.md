# WebSocket Client

`WebSocketClient` subscribes to vehicle data, maps incoming messages to RDF, and
forwards RDF to an initialized `ReasonerService`.

`main.cpp` loads the model configuration, creates the reasoner through
`ReasonerFactory`, then constructs the client. The client requires both an RDF
mapper and a reasoner service.

```cpp
const auto reasoner = ReasonerFactory::initReasoner(model_config.getReasonerProfile());
auto client = std::make_shared<WebSocketClient>(system_config, rdf_mapper,
                                                model_config.getSignals(), reasoner);
```

During client construction, the reasoner receives `reply_messages_queue_` through
`setReasoningResultMessageQueue`. During connection initialization, the client
also installs a result handler. The handler posts each reasoning result to the
client's I/O context, where it is added to the outbound queue and sent.
`response_messages_queue_` remains exclusively for inbound WebSocket payloads.

`writeReplyMessagesOnQueue()` is public so an owner can request sending queued
messages. The client serializes writes: it starts at most one asynchronous write,
then starts the next queued message when the previous one completes.

The current RDF4J and CQELS services are placeholders. The client will forward
RDF updates now, while actual engine processing is added later.
