# ADR 0011 — Run RDF4J as an HTTP service and keep demo services outside cdsp/

Status: accepted
Date: 2026-08-29

## Context

The legacy CDSP demo runs RDF4J as a standalone Spring Boot process with an
HTTP datastore API. The new Knowledge Layer currently has an RDF4J placeholder
that does not send RDF, install rules, or query results. The monitoring UI and
RDF4J process are runnable demo services, not components of the C++ Knowledge
Layer library.

The active RDF mapper maps one leaf update when its response data and metadata
use a schema-relative dotted path. A `flat + relative` leaf response uses an
empty data key and cannot provide that path.

## Decision

- Extract the legacy UI to repository-root `cdsp-ui/` and the RDF4J service to
  repository-root `reasoner-rdf4j/`; neither is placed under `cdsp/`.
- Implement `RDF4JReasonerService` as a synchronous HTTP adapter to the
  standalone service. It creates a datastore, uploads ontologies and SHACL
  rules, posts Turtle updates, and performs the profile SPARQL query.
- Add optional `endpoint` configuration (`host`, `port`, `datastore`) to an
  RDF4J profile, with local defaults.
- Subscribe each leaf as `flat + absolute` so the existing mapper receives a
  stable VSS path. This is a compatibility constraint of the current mapper,
  not a claim that other IL response shapes are invalid.
- Put runnable wrappers in top-level `tools/`. The RemotiveLabs wrapper only
  forwards explicitly supplied credentials/environment to the existing bridge.

## Consequences

The first vertical slice is data-generator -> IL -> KL -> HTTP RDF4J query.
It is pull-based: every accepted RDF update triggers a query. It does not yet
write a normalized inference result back to IL; that remains the next slice
because the current result mapper has no VSS output contract.

## Validation and rollback

Build the Java service with Maven, start it through the tools script, and use
the deterministic two-event scene to verify that its SPARQL endpoint returns
two inferred segments. Rollback restores the RDF4J placeholder and removes the
top-level extracted services; the Information Layer contract is untouched.
