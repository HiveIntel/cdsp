#!/usr/bin/env bash

set -euo pipefail

UI_HOST="${UI_HOST:-0.0.0.0}"
UI_PORT="${UI_PORT:-8501}"

exec streamlit run app.py --server.address "$UI_HOST" --server.port "$UI_PORT"
