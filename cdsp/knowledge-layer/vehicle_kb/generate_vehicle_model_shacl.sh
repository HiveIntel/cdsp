#!/usr/bin/env bash
# Generate vehicle_model.shacl from the Information Layer VSS configuration.
#
# Sources:
#   https://github.com/COVESA/vss-tools
#   https://github.com/COVESA/s2dm
#
# Optional environment variables:
#   VEHICLE_MODEL_VERSION  Version embedded in the SHACL namespaces (default: $VERSION, then latest)
#   VERSION                Generic fallback for VEHICLE_MODEL_VERSION
#   VSS_TOOLS_VERSION      vss-tools Git ref to clone when /tmp/vss-tools is absent
#   S2DM_VERSION           s2dm Git ref to clone when /tmp/s2dm is absent
#   VSS_TOOLS_DIR          Existing vss-tools checkout (default: /tmp/vss-tools)
#   S2DM_DIR               Existing s2dm checkout (default: /tmp/s2dm)

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd -- "${SCRIPT_DIR}/../../.." && pwd)"
INPUT_FILE="${REPOSITORY_ROOT}/cdsp/information-layer/handlers/config/schema-files/vss_data_points.yaml"
OUTPUT_FILE="${SCRIPT_DIR}/vehicle_model.shacl"

MODEL_VERSION="${VEHICLE_MODEL_VERSION:-${VERSION:-latest}}"
VSS_TOOLS_DIR="${VSS_TOOLS_DIR:-/tmp/vss-tools}"
S2DM_DIR="${S2DM_DIR:-/tmp/s2dm}"
VSS_TOOLS_VERSION="${VSS_TOOLS_VERSION:-latest}"
S2DM_VERSION="${S2DM_VERSION:-latest}"
WORK_DIR="$(mktemp -d /tmp/vehicle-model-shacl.XXXXXX)"
VENV_DIR="${WORK_DIR}/venv"
PYTHON_BIN=""

cleanup() {
    rm -rf -- "${WORK_DIR}"
}
trap cleanup EXIT

require_git_checkout() {
    local directory="$1"
    local repository="$2"
    local version="$3"

    if [[ -d "${directory}/.git" ]]; then
        return
    fi

    if [[ -e "${directory}" ]]; then
        echo "Error: ${directory} exists but is not a Git checkout." >&2
        exit 1
    fi

    mkdir -p -- "$(dirname -- "${directory}")"
    if [[ "${version}" == "latest" ]]; then
        git clone --depth 1 "${repository}" "${directory}"
    else
        git clone --depth 1 --branch "${version}" "${repository}" "${directory}"
    fi
}

if [[ ! -f "${INPUT_FILE}" ]]; then
    echo "Error: VSS input file not found: ${INPUT_FILE}" >&2
    exit 1
fi

for candidate in python3.13 python3.12 python3.11 python3; do
    if command -v "${candidate}" >/dev/null 2>&1 \
        && "${candidate}" -c 'import sys; raise SystemExit(sys.version_info < (3, 11))'; then
        PYTHON_BIN="${candidate}"
        break
    fi
done
if [[ -z "${PYTHON_BIN}" ]]; then
    echo "Error: s2dm requires Python 3.11 or newer." >&2
    exit 1
fi

# Keep third-party clones out of the repository. Existing checkouts are reused
# unchanged so an offline rerun does not fetch or clone anything.
require_git_checkout "${VSS_TOOLS_DIR}" "https://github.com/COVESA/vss-tools.git" "${VSS_TOOLS_VERSION}"
require_git_checkout "${S2DM_DIR}" "https://github.com/COVESA/s2dm.git" "${S2DM_VERSION}"

"${PYTHON_BIN}" -m venv "${VENV_DIR}"
"${VENV_DIR}/bin/python" -m pip install --quiet --upgrade pip
"${VENV_DIR}/bin/python" -m pip install --quiet "${VSS_TOOLS_DIR}" "${S2DM_DIR}"

# vss-tools validates every referenced unit. The application schema intentionally
# contains only selected data points, so generate the small accompanying unit and
# quantity catalogs it needs instead of requiring a full VSS catalog checkout.
UNITS_FILE="${WORK_DIR}/units.yaml"
QUANTITIES_FILE="${WORK_DIR}/quantities.yaml"
NORMALIZED_INPUT_FILE="${WORK_DIR}/vss_data_points.normalized.yaml"
"${VENV_DIR}/bin/python" - "${INPUT_FILE}" "${NORMALIZED_INPUT_FILE}" "${UNITS_FILE}" "${QUANTITIES_FILE}" <<'PY'
from pathlib import Path
import re
import sys

import yaml

input_file, normalized_input_file, units_file, quantities_file = map(Path, sys.argv[1:])
specification = yaml.safe_load(input_file.read_text(encoding="utf-8")) or {}

# VSS requires every branch in the final tree to be explicit. Add descriptions
# for parent branches that are implicit in the selected application data points.
for path in list(specification):
    parts = path.split(".")
    for parent_length in range(1, len(parts)):
        parent = ".".join(parts[:parent_length])
        specification.setdefault(
            parent,
            {
                "description": f"Container branch for {path}.",
                "type": "branch",
            },
        )
normalized_input_file.write_text(yaml.safe_dump(specification, sort_keys=False), encoding="utf-8")

units = {}
quantities = {}
for signal in specification.values():
    if not isinstance(signal, dict) or not signal.get("unit"):
        continue
    unit = str(signal["unit"])
    quantity = "custom_" + re.sub(r"[^A-Za-z0-9_]", "_", unit).strip("_").lower()
    quantities.setdefault(quantity, {"definition": f"Quantity for VSS unit {unit}."})
    units.setdefault(
        unit,
        {
            "definition": f"VSS unit {unit}.",
            "unit": unit,
            "quantity": quantity,
            "allowed-datatypes": ["numeric"],
        },
    )

units_file.write_text(yaml.safe_dump(units, sort_keys=True), encoding="utf-8")
quantities_file.write_text(yaml.safe_dump(quantities, sort_keys=True), encoding="utf-8")
PY

GRAPHQL_DIR="${WORK_DIR}/graphql"
"${VENV_DIR}/bin/vspec" export s2dm \
    --vspec "${NORMALIZED_INPUT_FILE}" \
    --output "${GRAPHQL_DIR}" \
    --units "${UNITS_FILE}" \
    --quantities "${QUANTITIES_FILE}"

"${VENV_DIR}/bin/s2dm" export shacl \
    --schema "${GRAPHQL_DIR}/graphql.graphql" \
    --output "${OUTPUT_FILE}" \
    --shapes-namespace "https://covesa.org/vss/${MODEL_VERSION}/shapes#" \
    --shapes-namespace-prefix "vsssh" \
    --model-namespace "https://covesa.org/vss/${MODEL_VERSION}/model#" \
    --model-namespace-prefix "vss"

echo "Generated ${OUTPUT_FILE} (VEHICLE_MODEL_VERSION=${MODEL_VERSION})"
