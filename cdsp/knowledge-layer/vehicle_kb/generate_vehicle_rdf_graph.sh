#!/usr/bin/env bash
# Build and validate vehicle_graph.ttl from vehicle_model.shacl and $VIN_ID.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"
VENV_DIR="${VEHICLE_KB_VENV_DIR:-/tmp/vehicle-kb-venv}"

if [[ -z "${VIN_ID:-}" ]]; then
    echo "Error: set VIN_ID to the vehicle identification number to model." >&2
    exit 1
fi

if [[ ! -x "${VENV_DIR}/bin/python" ]]; then
    "${PYTHON_BIN}" -m venv "${VENV_DIR}"
fi
if ! "${VENV_DIR}/bin/python" -c 'import pyshacl, rdflib' >/dev/null 2>&1; then
    "${VENV_DIR}/bin/python" -m pip install --quiet --upgrade pip pyshacl
fi

exec "${VENV_DIR}/bin/python" "${SCRIPT_DIR}/generate_vehicle_rdf_graph.py"
