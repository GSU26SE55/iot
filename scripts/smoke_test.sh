#!/usr/bin/env sh
set -eu

cd "$(dirname "$0")/.."
python3 -m compileall gateway tests
python3 -m unittest discover -s tests
python3 -m gateway.main --config config/config.example.json --once --dry-run

