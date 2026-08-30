#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
generator="$repo_root/examples/driving-style/data-generator"

"$repo_root/examples/driving-style/launch/launch_information_layer.sh"
python3 "$generator/generate_stream.py"

echo "Start the RDF4J service in another terminal:"
echo "  $script_dir/run-rdf4j-reasoner.sh"
echo
echo "Then start the built driving_style_client with VEHICLE_OBJECT_ID=VIN_1805,"
echo "and replay the deterministic two-event scene:"
echo "  $generator/play_stream scene01"
