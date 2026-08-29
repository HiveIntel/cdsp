#!/usr/bin/env bash
set -euo pipefail

launcher_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(git -C "$launcher_dir" rev-parse --show-toplevel)"
branch="${KL_SOURCE_REF:-stream_reasoner_service_integration}"
image_name="${KL_IMAGE_NAME:-driving-style-kl-wip}"
container_name="${KL_CONTAINER_NAME:-driving-style-kl-wip}"
vehicle_object_id="${VEHICLE_OBJECT_ID:-VIN_1805}"
il_port="${PORT_WEBSOCKET_SERVER:-8080}"
container_il_host="${KL_HOST_WEBSOCKET_SERVER:-host.docker.internal}"
stage_dir="$(mktemp -d /tmp/driving-style-kl.XXXXXX)"

cleanup() {
  rm -rf "$stage_dir"
}
trap cleanup EXIT

"$launcher_dir/launch_information_layer.sh"

if ! git -C "$repo_root" rev-parse --verify --quiet "${branch}^{commit}" >/dev/null; then
  echo "Knowledge Layer source ref does not exist: $branch" >&2
  exit 2
fi

git -C "$repo_root" archive "$branch" \
  cdsp/knowledge-layer/CMakeLists.txt \
  cdsp/knowledge-layer/Dockerfile \
  cdsp/knowledge-layer/common \
  cdsp/knowledge-layer/common-test-resources \
  cdsp/knowledge-layer/connector \
  cdsp/knowledge-layer/symbolic-reasoner \
  cdsp/knowledge-layer/third_party \
  | tar -x -C "$stage_dir"

mkdir -p "$stage_dir/model" "$stage_dir/solutions"
cp -a "$repo_root/examples/driving-style/model/." "$stage_dir/model/"
cp -a "$repo_root/examples/driving-style/solutions/." "$stage_dir/solutions/"
python3 -m json.tool "$stage_dir/model/driving_style_reasoning.json" >/dev/null

docker build --progress=quiet \
  --tag "$image_name" \
  --file "$launcher_dir/knowledge-layer/Dockerfile" \
  "$stage_dir"

if docker ps --format '{{.Names}}' | grep -Fxq "$container_name"; then
  echo "Knowledge Layer container '$container_name' is already running; leaving it unchanged."
  exit 0
fi

docker run -d --rm \
  --name "$container_name" \
  --add-host host.docker.internal:host-gateway \
  -e HOST_WEBSOCKET_SERVER="$container_il_host" \
  -e PORT_WEBSOCKET_SERVER="$il_port" \
  -e VEHICLE_OBJECT_ID="$vehicle_object_id" \
  "$image_name"

echo "Knowledge Layer started as '$container_name' using model/driving_style_reasoning.json."
