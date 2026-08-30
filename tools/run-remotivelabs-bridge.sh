#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
bridge="$repo_root/examples/remotivelabs-feeder/rl-bridge.py"

: "${RL_BROKER_URL:?Set RL_BROKER_URL to the RemotiveLabs broker URL.}"
: "${VEHICLE_OBJECT_ID:?Set VEHICLE_OBJECT_ID for Information Layer output.}"

credential_args=()
if [[ -n "${RL_API_KEY:-}" ]]; then
  credential_args+=(--x_api_key "$RL_API_KEY")
elif [[ -n "${RL_ACCESS_TOKEN:-}" ]]; then
  credential_args+=(--access_token "$RL_ACCESS_TOKEN")
else
  echo "Set either RL_API_KEY or RL_ACCESS_TOKEN." >&2
  exit 2
fi

exec python3 "$bridge" \
  --url "$RL_BROKER_URL" \
  "${credential_args[@]}" \
  --output_mode information-layer \
  --id "$VEHICLE_OBJECT_ID" \
  --information_layer_url "${INFORMATION_LAYER_URL:-ws://127.0.0.1:8080}" \
  --signals "$@"
