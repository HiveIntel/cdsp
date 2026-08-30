# ADR 0013 — Retire the legacy connector pipeline and its private dependencies

Status: accepted
Date: 2026-08-30

## Context

The active path is `il_connection -> rdf_mapper -> reasoner_engine`; it does
not include the legacy connector, BO/DTO model, TripleAssembler, common test
fixtures, or GeographicLib. The former global Docker service builds the
removed `reasoner_client` / RDFox pipeline.

## Decision

Remove `connector/`, `common/`, `common-test-resources/`, and
`third_party/GeographicLib/`. Remove their CMake targets and retire the global
legacy Knowledge Layer Compose service and Dockerfile. The driving-style
launcher and `driving_style_client` image are the replacement runnable path.

## Consequences

The RDFox-specific hello-world example and historical documentation cannot be
used as a current runtime path and must be retired or rewritten separately.

## Validation and rollback

Build the driving-style client image and run focused active-module tests after
the deletion. Rollback restores the deleted directories from Git history; no
Information Layer contract or runtime data is deleted.
