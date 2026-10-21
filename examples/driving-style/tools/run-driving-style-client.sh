#!/usr/bin/env bash
# Build the driving-style Knowledge Layer image and run driving_style_client.
#
# The build context contains only the git-tracked files of cdsp/knowledge-layer
# and examples/driving-style, taken from the working tree or, when KL_SOURCE_REF
# is set, from that git ref. The container uses the host network so the default
# Information Layer endpoint (127.0.0.1:8080) is reachable.
#
# Environment:
#   KL_SOURCE_REF          git ref to build instead of the working tree
#   KL_IMAGE               image tag (default: cdsp-driving-style-kl)
#   MODEL_CONFIG, VEHICLE_OBJECT_ID, HOST_WEBSOCKET_SERVER, PORT_WEBSOCKET_SERVER,
#   WEBSOCKET_TARGET       passed to the client when set
set -euo pipefail

tools_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(git -C "$tools_dir" rev-parse --show-toplevel)"
image="${KL_IMAGE:-cdsp-driving-style-kl}"
sources=(cdsp/knowledge-layer examples/driving-style)

if ! command -v docker >/dev/null 2>&1; then
    echo "docker is required to build and run the Knowledge Layer." >&2
    exit 1
fi

context="$(mktemp -d "${TMPDIR:-/tmp}/driving-style-kl.XXXXXX")"
trap 'rm -rf "$context"' EXIT

if [[ -n "${KL_SOURCE_REF:-}" ]]; then
    echo "[driving-style] Building Knowledge Layer from git ref ${KL_SOURCE_REF}"
    git -C "$repo_root" archive "$KL_SOURCE_REF" -- "${sources[@]}" | tar -x -C "$context"
else
    echo "[driving-style] Building Knowledge Layer from the working tree"
    # Tracked files only; deleted-but-tracked files are skipped.
    git -C "$repo_root" ls-files -z -- "${sources[@]}" |
        tar -C "$repo_root" --null --ignore-failed-read -T - -cf - 2>/dev/null |
        tar -x -C "$context"
fi

DOCKER_BUILDKIT=1 docker build \
    -f "$context/examples/driving-style/launch/knowledge-layer/Dockerfile" \
    -t "$image" \
    "$context"

env_args=()
for name in MODEL_CONFIG VEHICLE_OBJECT_ID HOST_WEBSOCKET_SERVER PORT_WEBSOCKET_SERVER WEBSOCKET_TARGET; do
    if [[ -n "${!name:-}" ]]; then
        env_args+=(-e "${name}=${!name}")
    fi
done

echo "[driving-style] Starting ${image}"
exec docker run --rm --init --network host "${env_args[@]}" "$image"
