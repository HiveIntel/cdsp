# 0014 — Use kebab-case Knowledge Layer module paths

## Decision

Rename the active Knowledge Layer module directories and CMake targets to:

- `config-parser`
- `il-connection`
- `il-request`
- `reasoner-engine`

`il-request` replaces the old `il_message` package name. Its C++ include path
and namespace are `il_request`, because C++ identifiers cannot contain hyphens.

## Consequences

The public C++ API moves from `il_message/...` and `il_message::` to
`il_request/...` and `il_request::`. The wire protocol and DTO JSON remain
unchanged. CMake consumers link the kebab-case targets.
