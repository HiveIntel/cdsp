# ADR 0012 — Keep driving-style Docker runtime state beside its launcher

Status: accepted
Date: 2026-08-30

## Context

The running Information Layer and IoTDB containers were created from an
untracked `cdsp/knowledge-layer/wip/if_docker_launch` Compose project. Its
IoTDB data, logs, and Information Layer logs are live bind mounts, not source
code. That location is unrelated to the current driving-style launcher.

## Decision

Move the live runtime state to
`examples/driving-style/launch/information-layer/runtime/` and declare the
three bind mounts in that launcher's Compose file. Ignore the runtime state in
Git. Stop the old project before moving data, then recreate the two services
from the launcher-owned Compose project.

## Consequences

The WIP directory can be retired after health verification. Runtime data stays
local and is not included in source-code deletion or commits.

## Validation and rollback

Verify IoTDB health and the Information Layer port after recreation. Rollback
stops the new project and moves the runtime directory back before starting a
Compose file that uses the original bind mounts.
