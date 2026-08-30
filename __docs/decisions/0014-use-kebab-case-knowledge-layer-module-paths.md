# 0014 — Rename Knowledge Layer modules consistently

## Decision

Rename the active Knowledge Layer module directories and CMake targets to:

- `config-parser`
- `il-connection`
- `il_request`
- `reasoner-engine`

`il_request` replaces the old `il_message` package name. Its C++ include path
and namespace are also `il_request`.

## Consequences

The public C++ API moves from `il_message/...` and `il_message::` to
`il_request/...` and `il_request::`. The wire protocol and DTO JSON remain
unchanged. CMake consumers link these target names.
