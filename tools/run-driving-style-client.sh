#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
image_name="${KL_IMAGE_NAME:-driving-style-client}"

docker build --tag "$image_name" \
  --file "$repo_root/examples/driving-style/launch/knowledge-layer/Dockerfile" \
  "$repo_root"

exec docker run --rm --network host \
  -e VEHICLE_OBJECT_ID="${VEHICLE_OBJECT_ID:-VIN_1805}" \
  -e RDF4J_HOST="${RDF4J_HOST:-127.0.0.1}" \
  -e RDF4J_PORT="${RDF4J_PORT:-12110}" \
  "$image_name"
