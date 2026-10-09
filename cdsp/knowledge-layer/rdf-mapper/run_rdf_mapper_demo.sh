#!/usr/bin/env bash
set -euo pipefail

MODULE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${TMPDIR:-/tmp}/rdf-mapper-demo-build"
BINARY="${BUILD_DIR}/rdf_mapper_demo"
DEFAULT_SHAPES="${MODULE_DIR}/tests/fixtures/vehicle_shacl"

if [[ $# -gt 1 ]]; then
    echo "Usage: $0 [shacl-file-or-directory]" >&2
    exit 2
fi

SHAPE_PATH="${1:-${DEFAULT_SHAPES}}"

if [[ ! -f "${SHAPE_PATH}" && ! -d "${SHAPE_PATH}" ]]; then
    echo "SHACL path does not exist: ${SHAPE_PATH}" >&2
    exit 1
fi

SERD_CXXFLAGS=()
SERD_LDFLAGS=()
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists serd-0; then
    # shellcheck disable=SC2207
    SERD_CXXFLAGS+=( $(pkg-config --cflags serd-0) )
    # shellcheck disable=SC2207
    SERD_LDFLAGS+=( $(pkg-config --libs serd-0) )
else
    SERD_SOURCE_DIR="${MODULE_DIR}/../connection/il-protocol/tests/build-project/_deps/serd-source-src"
    SERD_LIBRARY="${MODULE_DIR}/../connection/il-protocol/tests/build-project/_deps/serd-build/libserd.a"

    if [[ ! -f "${SERD_SOURCE_DIR}/include/serd/serd.h" || ! -f "${SERD_LIBRARY}" ]]; then
        echo "serd-0 was not found through pkg-config or the local CMake build." >&2
        echo "Configure/build the Knowledge Layer first, or install the Serd development files." >&2
        exit 1
    fi

    SERD_CXXFLAGS+=("-I${SERD_SOURCE_DIR}/include")
    SERD_LDFLAGS+=("${SERD_LIBRARY}")
fi

mkdir -p "${BUILD_DIR}"

echo "[shacl-parser-demo] Building ${BINARY}"
"${CXX:-c++}" \
    -std=c++17 \
    -Wall \
    -Wextra \
    -pedantic \
    "-I${MODULE_DIR}/include" \
    "${SERD_CXXFLAGS[@]}" \
    "${MODULE_DIR}/rdf_mapper_demo.cpp" \
    "${MODULE_DIR}/src/rdf_model.cpp" \
    "${MODULE_DIR}/src/serd_rdf_reader.cpp" \
    "${MODULE_DIR}/src/shacl_parser.cpp" \
    "${SERD_LDFLAGS[@]}" \
    -o "${BINARY}"

echo "[shacl-parser-demo] Parsing ${SHAPE_PATH}"
exec "${BINARY}" "${SHAPE_PATH}"
