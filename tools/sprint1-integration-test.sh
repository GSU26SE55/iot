#!/usr/bin/env bash
# ==================================================================
# Sprint 1 — Integration test end-to-end
#
# 1. Start mock-backend (HTTP plain port 17200)
# 2. Compile test/integration/test_payload_against_mock_backend.cpp
#    với src/core/payload.cpp
# 3. Run binary → assert 200 + 422
# 4. Tear down
#
# Yêu cầu: clang++ / g++, libcurl (macOS có sẵn), python3
# ==================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FW="$ROOT/firmware-esp32"
PORT=17200
LOG="$(mktemp -t mock-backend.XXXXXX.log)"

cleanup() {
  if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  rm -f /tmp/sprint1-itest
}
trap cleanup EXIT

echo "==[1/4]== Start mock-backend on port $PORT (log: $LOG)"
python3 "$ROOT/tools/mock-backend/mock_backend.py" --no-tls --port "$PORT" \
  > "$LOG" 2>&1 &
SERVER_PID=$!

# Wait for server up
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

echo "==[2/4]== Compile integration test"
# ArduinoJson v7 đã được PlatformIO cài sẵn cho env native.
JSON_INC="$FW/.pio/libdeps/native/ArduinoJson/src"
if [[ ! -d "$JSON_INC" ]]; then
  echo "    Cài ArduinoJson qua: pio test -e native -f test_payload (chạy 1 lần để PIO pull dep)"
  ( cd "$FW" && pio test -e native -f test_payload >/dev/null 2>&1 || true )
fi
[[ -d "$JSON_INC" ]] || { echo "    FAIL: ArduinoJson include không tồn tại tại $JSON_INC"; exit 1; }

clang++ -std=gnu++17 -O0 -g \
  -I "$FW/include" \
  -I "$FW/src" \
  -I "$JSON_INC" \
  "$FW/src/core/payload.cpp" \
  "$FW/test/integration/test_payload_against_mock_backend.cpp" \
  -lcurl \
  -o /tmp/sprint1-itest
echo "    binary built: /tmp/sprint1-itest"

echo "==[3/4]== Run integration test"
set +e
/tmp/sprint1-itest "http://localhost:$PORT/api/sensor-readings/batch"
RC=$?
set -e

echo "==[4/4]== Mock backend log tail:"
tail -10 "$LOG" | sed 's/^/    /'

if (( RC == 0 )); then
  echo
  echo "✅ Sprint 1 integration test PASS (200 OK + 400 listErrors, NI §7.4 legacy)"
else
  echo
  echo "❌ Sprint 1 integration test FAIL (rc=$RC)"
  exit "$RC"
fi
