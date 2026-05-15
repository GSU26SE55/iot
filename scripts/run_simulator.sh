#!/usr/bin/env sh
set -eu

cd "$(dirname "$0")/.."
python -m gateway.main --config config/config.example.json --loop

