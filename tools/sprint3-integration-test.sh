#!/usr/bin/env bash
# ==================================================================
# Sprint 3 — Integration test: production contract + idempotency + clock skew
# ==================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FW="$ROOT/firmware-esp32"
PORT=17203
LOG="$(mktemp -t mock-backend-s3.XXXXXX.log)"

cleanup() {
  if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  rm -f /tmp/sprint3-itest
}
trap cleanup EXIT

echo "==[1/4]== Start mock-backend on port $PORT (log: $LOG)"
python3 "$ROOT/tools/mock-backend/mock_backend.py" --no-tls --port "$PORT" \
  > "$LOG" 2>&1 &
SERVER_PID=$!

for i in {1..20}; do
  if curl -sf "http://localhost:$PORT/healthz" >/dev/null; then
    echo "    mock-backend up (pid=$SERVER_PID)"
    break
  fi
  sleep 0.2
  if (( i == 20 )); then
    echo "    FAIL: mock-backend did not start. Log:"; cat "$LOG"; exit 1
  fi
done

echo "==[2/4]== Compile Sprint 3 integration test"
JSON_INC="$FW/.pio/libdeps/native/ArduinoJson/src"
[[ -d "$JSON_INC" ]] || ( cd "$FW" && pio test -e native -f test_payload_v3 >/dev/null 2>&1 || true )
[[ -d "$JSON_INC" ]] || { echo "    FAIL: ArduinoJson include không có"; exit 1; }

clang++ -std=gnu++17 -O0 -g \
  -I "$FW/include" \
  -I "$FW/src" \
  -I "$JSON_INC" \
  "$FW/src/core/payload.cpp" \
  "$FW/src/core/idempotency_key.cpp" \
  "$FW/test/integration/test_sprint3_production.cpp" \
  -lcurl \
  -o /tmp/sprint3-itest
echo "    binary built"

echo "==[3/4]== Run integration test"
set +e
/tmp/sprint3-itest "http://localhost:$PORT/api/sensor-readings/batch"
RC=$?
set -e

echo "==[4/4]== Mock backend log tail:"
tail -15 "$LOG" | sed 's/^/    /'

if (( RC == 0 )); then
  echo
  echo "✅ Sprint 3 integration test PASS (production + idempotency + clock skew OK)"
else
  echo
  echo "❌ Sprint 3 integration test FAIL (rc=$RC)"
  exit "$RC"
fi
