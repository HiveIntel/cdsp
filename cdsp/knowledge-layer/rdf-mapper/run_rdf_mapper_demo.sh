#!/usr/bin/env bash
set -euo pipefail

module_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
knowledge_layer_dir="$(cd "${module_dir}/.." && pwd)"
repository_dir="$(cd "${knowledge_layer_dir}/../.." && pwd)"

if [[ $# -gt 4 ]]; then
    echo "Usage: $0 [fixture-directory] [vehicle-id] [instance-base-iri] [turtle|ntriples|nquads|trig]" >&2
    exit 2
fi

fixture_directory="${1:-${module_dir}/tests/fixtures}"
vehicle_id="${2:-VIN_12345}"
instance_base_iri="${3:-https://example.org/instances/}"
output_format="${4:-turtle}"

case "${output_format}" in
    turtle|ntriples|nquads|trig) ;;
    *) echo "Unsupported output format: ${output_format}" >&2; exit 2 ;;
esac

if [[ ! -d "${fixture_directory}" ]]; then
    echo "Fixture directory does not exist: ${fixture_directory}" >&2
    exit 1
fi
if ! command -v "${CXX:-c++}" >/dev/null 2>&1; then
    echo "C++ compiler not found: ${CXX:-c++}. Set CXX to a C++17 compiler executable." >&2
    exit 1
fi

# Reuse conventional CMake dependency caches without fetching or installing anything.
cache_directories=(
    "${knowledge_layer_dir}"/build*
    "${repository_dir}"/build*
    "${module_dir}/tests/build-project"
    "${knowledge_layer_dir}/connection/il-protocol/tests/build-project"
    "${knowledge_layer_dir}/connection/ws-connection/tests/build-project"
)
serd_cxxflags=()
serd_ldflags=()

find_serd() {
    if [[ -n "${SERD_INCLUDE_DIR:-}" || -n "${SERD_LIBRARY:-}" ]]; then
        if [[ -z "${SERD_INCLUDE_DIR:-}" || -z "${SERD_LIBRARY:-}" ]]; then
            echo "Set both SERD_INCLUDE_DIR and SERD_LIBRARY for a custom Serd build." >&2
            return 1
        fi
        if [[ ! -f "${SERD_INCLUDE_DIR}/serd/serd.h" || ! -f "${SERD_LIBRARY}" ]]; then
            echo "Custom Serd paths are invalid; expected <SERD_INCLUDE_DIR>/serd/serd.h and a library file." >&2
            return 1
        fi
        serd_cxxflags=("-I${SERD_INCLUDE_DIR}")
        serd_ldflags=("${SERD_LIBRARY}")
        return 0
    fi

    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists serd-0; then
        read -r -a serd_cxxflags <<< "$(pkg-config --cflags serd-0)"
        read -r -a serd_ldflags <<< "$(pkg-config --libs serd-0)"
        return 0
    fi

    local include_directory library_file cache_directory
    for include_directory in /usr/include/serd-0 /usr/local/include/serd-0 /usr/include /usr/local/include; do
        [[ -f "${include_directory}/serd/serd.h" ]] || continue
        for library_file in /usr/lib/libserd-0.so /usr/lib/libserd-0.a \
            /usr/lib/*/libserd-0.so /usr/lib/*/libserd-0.a \
            /usr/local/lib/libserd-0.so /usr/local/lib/libserd-0.a \
            /usr/lib64/libserd-0.so /usr/lib64/libserd-0.a; do
            [[ -f "${library_file}" ]] || continue
            serd_cxxflags=("-I${include_directory}")
            serd_ldflags=("${library_file}")
            return 0
        done
    done

    for cache_directory in "${cache_directories[@]}"; do
        for include_directory in "${cache_directory}/_deps/serd-source-src/include" \
            "${cache_directory}/_deps/serd-src/include"; do
            [[ -f "${include_directory}/serd/serd.h" ]] || continue
            for library_file in "${cache_directory}/_deps/serd-build/libserd.a" \
                "${cache_directory}/_deps/serd-build/libserd-0.a" \
                "${cache_directory}/_deps/serd-build/libserd.so" \
                "${cache_directory}/_deps/serd-build/libserd-0.so"; do
                [[ -f "${library_file}" ]] || continue
                serd_cxxflags=("-I${include_directory}")
                serd_ldflags=("${library_file}")
                return 0
            done
        done
    done

    echo "Serd headers/library not found through pkg-config, system paths, or Knowledge Layer build caches." >&2
    echo "Use an existing build with SERD_INCLUDE_DIR=<include-root> SERD_LIBRARY=<library-file>." >&2
    return 1
}

find_json() {
    if [[ -n "${JSON_INCLUDE_DIR:-}" ]]; then
        if [[ ! -f "${JSON_INCLUDE_DIR}/nlohmann/json.hpp" ]]; then
            echo "JSON_INCLUDE_DIR must contain nlohmann/json.hpp: ${JSON_INCLUDE_DIR}" >&2
            return 1
        fi
        json_include_directory="${JSON_INCLUDE_DIR}"
        return 0
    fi

    local include_directory cache_directory
    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists nlohmann_json; then
        include_directory="$(pkg-config --variable=includedir nlohmann_json)"
        if [[ -f "${include_directory}/nlohmann/json.hpp" ]]; then
            json_include_directory="${include_directory}"
            return 0
        fi
    fi
    for include_directory in /usr/include /usr/local/include; do
        [[ -f "${include_directory}/nlohmann/json.hpp" ]] || continue
        json_include_directory="${include_directory}"
        return 0
    done
    for cache_directory in "${cache_directories[@]}"; do
        for include_directory in "${cache_directory}/_deps/json-src/single_include" \
            "${cache_directory}/_deps/json-src/include"; do
            [[ -f "${include_directory}/nlohmann/json.hpp" ]] || continue
            json_include_directory="${include_directory}"
            return 0
        done
    done

    echo "nlohmann/json headers not found through pkg-config, system paths, or Knowledge Layer build caches." >&2
    echo "Set JSON_INCLUDE_DIR to an existing include root containing nlohmann/json.hpp." >&2
    return 1
}

find_serd
find_json

# Each default run gets a fresh directory; existing builds and caches are never removed.
if [[ -n "${RDF_MAPPER_DEMO_BUILD_DIR:-}" ]]; then
    build_directory="${RDF_MAPPER_DEMO_BUILD_DIR}"
    mkdir -p "${build_directory}"
else
    build_directory="$(mktemp -d "${TMPDIR:-/tmp}/rdf-mapper-demo.XXXXXX")"
fi
binary="${build_directory}/rdf_mapper_demo"

echo "[rdf-mapper-demo] Building ${binary}"
"${CXX:-c++}" \
    -std=c++17 \
    -Wall \
    -Wextra \
    -pedantic \
    "-I${module_dir}/include" \
    "-I${knowledge_layer_dir}/connection/il-protocol/include" \
    "-I${json_include_directory}" \
    "${serd_cxxflags[@]}" \
    "${module_dir}/rdf_mapper_demo.cpp" \
    "${module_dir}/src/rdf_mapper.cpp" \
    "${module_dir}/src/rdf_model.cpp" \
    "${module_dir}/src/serd_rdf_reader.cpp" \
    "${module_dir}/src/serd_rdf_writer.cpp" \
    "${module_dir}/src/shacl_parser.cpp" \
    "${serd_ldflags[@]}" \
    -o "${binary}"

echo "[rdf-mapper-demo] Mapping fixtures from ${fixture_directory}"
exec "${binary}" "${fixture_directory}" "${vehicle_id}" "${instance_base_iri}" "${output_format}"
