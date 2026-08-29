# ADR 0006 — Run the driving-style launchers from an isolated WIP area

Status: accepted
Date: 2026-08-28

## Context

The driving-style example needs launchers for the Information Layer (IL) and
Knowledge Layer (KL) without depending on the RDFox services in the root CDSP
Compose file. The runnable KL source remains the
`stream_reasoner_service_integration` branch head, whose executable loads the
driving-style model from its source tree.

## Decision

- Keep all launcher artifacts below `examples/driving-style/launch/`.
- Treat IL as a black box: if the configured WebSocket endpoint is reachable,
  do not start anything; otherwise start only an isolated IL+IoTDB Compose
  stack.
- Build the KL image from a clean `git archive` of
  `stream_reasoner_service_integration`, not from the dirty working tree.
- Stage the example's `model/` directory at the executable's configured
  source-tree path and stage its sibling `solutions/` directory so the model's
  relative profile path remains valid.

## Consequences

- The launchers do not modify the Information Layer implementation or root
  Compose topology.
- The KL image is reproducible from the named branch head and the current
  driving-style model resources.
- The isolated launcher remains separate until the direct `il_connection` runtime
  has a complete composition root.

## Validation and rollback

Validate the IL Compose configuration and Dockerfile syntax. Rollback removes
only the WIP launch artifacts; no running IL endpoint is stopped by the
launchers.
