#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
reasoner_dir="$repo_root/reasoner-rdf4j"
port="${RDF4J_PORT:-12110}"

if [[ ! -f "$reasoner_dir/pom.xml" ]]; then
  echo "RDF4J service was not extracted to $reasoner_dir" >&2
  exit 2
fi

cd "$reasoner_dir"
mvn --batch-mode --no-transfer-progress package -DskipTests
exec java -jar target/cdsp-reasoner-rdf4j.jar --server.port="$port"
