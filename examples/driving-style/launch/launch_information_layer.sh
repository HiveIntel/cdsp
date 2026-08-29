#!/usr/bin/env bash
set -euo pipefail

launcher_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
compose_file="$launcher_dir/information-layer/docker-compose.il.yml"
il_host="${HOST_WEBSOCKET_SERVER:-127.0.0.1}"
il_port="${PORT_WEBSOCKET_SERVER:-8080}"

if [[ ! "$il_host" =~ ^[A-Za-z0-9._-]+$ ]] || [[ ! "$il_port" =~ ^[0-9]+$ ]]; then
  echo "Invalid Information Layer host or port." >&2
  exit 2
fi

is_il_reachable() {
  timeout 1 bash -c 'exec 3<>"/dev/tcp/$1/$2"' _ "$il_host" "$il_port" 2>/dev/null
}

if is_il_reachable; then
  echo "Information Layer is already reachable at ${il_host}:${il_port}; leaving it unchanged."
  exit 0
fi

echo "Information Layer is not reachable at ${il_host}:${il_port}; starting the WIP IL stack."
docker compose -f "$compose_file" up -d

for _ in $(seq 1 60); do
  if is_il_reachable; then
    echo "Information Layer is reachable at ${il_host}:${il_port}."
    exit 0
  fi
  sleep 1
done

echo "Information Layer did not become reachable within 60 seconds." >&2
exit 1
