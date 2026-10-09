#!/usr/bin/env bash
set -euo pipefail

launcher_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(git -C "$launcher_dir" rev-parse --show-toplevel)"

"$launcher_dir/launch_information_layer.sh"

exec "$repo_root/examples/driving-style/tools/run-driving-style-client.sh"
