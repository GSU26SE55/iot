#!/usr/bin/env python3
"""
Sprint 1 — Mock backend cho firmware-esp32

Mục đích: cho phép test ESP32 (hoặc bất kỳ HTTP client nào) gửi batch ingest
khi backend BatteryService thật chưa sẵn sàng. Phục vụ Sprint 1 verify
"backend nhận 200" mà không cần dotnet/EF/docker.

Endpoints mô phỏng:
  POST /api/sensor-readings/batch       — Sprint 1 (legacy ingest)
  POST /api/iot-devices/provision       — stub Sprint 2 (trả poll interval)
  POST /api/iot-devices/heartbeat       — stub Sprint 2
  GET  /                                 — dashboard text (counters)
  GET  /healthz                          — liveness
  GET  /readings?limit=20                — xem N batch gần nhất (debug)

Tính năng:
  - Validate JSON: required field, kiểu dữ liệu, voltage range, ISO8601 timestamp
  - In ra payload (color-coded), counter per device, tổng readings
  - HTTPS với self-signed cert (auto sinh nếu chưa có) — match `setInsecure()` ESP32
  - HTTP plain cho ai không cần TLS (--no-tls)
  - Có flag --fail-rate để giả lập transient error (test retry — Sprint 3)

Yêu cầu: Python 3.10+, stdlib only (không cần Flask).

Chạy:
  python3 mock_backend.py                            # HTTPS port 7200, self-signed
  python3 mock_backend.py --port 8080 --no-tls       # HTTP cho test nhanh
  python3 mock_backend.py --fail-rate 0.3            # 30% request trả 500 (Sprint 3)
"""
from __future__ import annotations

import argparse
import json
import os
import random
import re
import ssl
import subprocess
import sys
import threading
import time
from collections import deque
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

# ---------------- ANSI color (chỉ bật khi stdout là TTY) ----------------
_TTY = sys.stdout.isatty()
def _c(code: str, s: str) -> str:
    return f"\x1b[{code}m{s}\x1b[0m" if _TTY else s
GREEN  = lambda s: _c("32", s)
RED    = lambda s: _c("31", s)
YELLOW = lambda s: _c("33", s)
CYAN   = lambda s: _c("36", s)
GRAY   = lambda s: _c("90", s)
BOLD   = lambda s: _c("1",  s)


# ---------------- State (in-memory) ----------------
class State:
    def __init__(self, history_size: int = 100):
        self.lock = threading.Lock()
        self.start_ts = time.time()
        self.batch_count = 0
        self.reading_count = 0
        self.fail_count = 0
        self.by_device: dict[str, dict] = {}     # deviceCode → {batches, readings, lastSeen}
        self.by_battery: dict[str, dict] = {}    # batteryAssetId/Serial → {count, lastVoltage, ...}
        self.recent_batches: deque = deque(maxlen=history_size)
        # Sprint 3 (S3-FW-02 / #IoT2-16): idempotency dedup.
        # Key = (device_code, idempotency_key) → cached response dict + insert_ts.
        # TTL 24h — mock không cleanup; in-memory đủ cho dev session.
        self.idempotency_cache: dict[tuple[str, str], dict] = {}
        self.idempotency_hits = 0

    def get_idempotent_response(self, device_code: str, key: str) -> dict | None:
        """Sprint 3: nếu (device, key) đã có response → trả lại + count hit."""
        if not key:
            return None
        with self.lock:
            entry = self.idempotency_cache.get((device_code, key))
            if entry is not None:
                self.idempotency_hits += 1
                return entry["response"]
        return None

    def store_idempotent_response(self, device_code: str, key: str, response: dict):
        if not key:
            return
        with self.lock:
            self.idempotency_cache[(device_code, key)] = {
                "response": response,
                "ts": time.time(),
            }

    def record(self, device_code: str, payload: dict, num_readings: int):
        now = time.time()
        with self.lock:
            self.batch_count += 1
            self.reading_count += num_readings
            d = self.by_device.setdefault(device_code, {"batches": 0, "readings": 0, "lastSeen": 0})
            d["batches"] += 1
            d["readings"] += num_readings
            d["lastSeen"] = now

            for r in payload.get("items", []):
                # Sprint 3: ưu tiên batteryAssetSerial, fallback batteryAssetId
                bid = r.get("batteryAssetSerial") or r.get("batteryAssetId", "?")
                b = self.by_battery.setdefault(bid, {
                    "count": 0, "lastVoltage": None, "lastCurrent": None,
                    "lastTemp": None, "lastSoc": None, "lastSeenIso": None,
                })
                b["count"] += 1
                b["lastVoltage"] = r.get("voltage")
                b["lastCurrent"] = r.get("current")
                b["lastTemp"]    = r.get("temperature")
                b["lastSoc"]     = r.get("socPercent")
                b["lastSeenIso"] = r.get("time")

            self.recent_batches.appendleft({
                "ts": now,
                "deviceCode": device_code,
                "readings": num_readings,
                "payload": payload,
            })

    def record_fail(self):
        with self.lock:
            self.fail_count += 1


# ---------------- Validation ----------------
ISO_RE = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?Z$")

UUID_RE = re.compile(
    r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$", re.IGNORECASE
)
BATCH_LIMIT = 1000   # api-battery.md: tối đa 1000 readings/request

def validate_batch(body: dict) -> tuple[bool, list[dict]]:
    """Validate cả Sprint 1 legacy và Sprint 3 production schema.

    Sprint 3 (S3-FW-04): items[] có thể chứa:
      - batteryAssetSerial (preferred) HOẶC batteryAssetId (legacy fallback)
      - sourceType (1=Bms, 2=IotGateway, 3=External)
      - sensorSourceCode (≤ 20 chars)
      - sohPercent, chargingState (1-5), bmsErrorCode (≤ 64 chars)
      - deviceTimestamp (cho clock skew check #IoT2-15)
    """
    errs: list[dict] = []

    if not isinstance(body, dict):
        return False, [{"field": "$", "detail": "root must be object"}]

    items = body.get("items")
    if items is None:
        errs.append({"field": "items", "detail": "required"})
    elif not isinstance(items, list):
        errs.append({"field": "items", "detail": "must be array"})
    elif len(items) == 0:
        errs.append({"field": "items", "detail": "empty"})
    elif len(items) > BATCH_LIMIT:
        errs.append({"field": "items", "detail": f"exceeds limit {BATCH_LIMIT}"})
    else:
        for i, it in enumerate(items):
            prefix = f"items[{i}]"
            if not isinstance(it, dict):
                errs.append({"field": prefix, "detail": "not object"}); continue

            # Sprint 3: chấp nhận batteryAssetSerial HOẶC batteryAssetId
            bid = it.get("batteryAssetId")
            bsr = it.get("batteryAssetSerial")
            if bsr is None and bid is None:
                errs.append({"field": f"{prefix}.batteryAssetId",
                             "detail": "PHẢI có batteryAssetSerial hoặc batteryAssetId"})
            elif bsr is not None:
                if not isinstance(bsr, str) or len(bsr) > 64:
                    errs.append({"field": f"{prefix}.batteryAssetSerial",
                                 "detail": "must be string ≤ 64 chars"})
            else:
                if not isinstance(bid, str) or not UUID_RE.match(bid):
                    errs.append({"field": f"{prefix}.batteryAssetId", "detail": "invalid UUID"})

            t = it.get("time")
            if t is None or not isinstance(t, str) or not ISO_RE.match(t):
                errs.append({"field": f"{prefix}.time", "detail": "invalid ISO8601"})

            # Sprint 3 (#IoT2-15): deviceTimestamp optional nhưng nếu có phải ISO8601
            dts = it.get("deviceTimestamp")
            if dts is not None and (not isinstance(dts, str) or not ISO_RE.match(dts)):
                errs.append({"field": f"{prefix}.deviceTimestamp", "detail": "invalid ISO8601"})

            for k in ("voltage", "current", "temperature", "socPercent"):
                v = it.get(k)
                if v is None:
                    errs.append({"field": f"{prefix}.{k}", "detail": "required"})
                elif not isinstance(v, (int, float)):
                    errs.append({"field": f"{prefix}.{k}", "detail": "must be number"})

            # NI §7.4 validation: voltage 0..1000, temp -50..150, soc 0..100
            v = it.get("voltage")
            if isinstance(v, (int, float)) and not (0 <= v <= 1000):
                errs.append({"field": f"{prefix}.voltage",
                             "detail": f"out of range [0, 1000] (got {v})"})

            t = it.get("temperature")
            if isinstance(t, (int, float)) and not (-50 <= t <= 150):
                errs.append({"field": f"{prefix}.temperature",
                             "detail": f"out of range [-50, 150] (got {t})"})

            s = it.get("socPercent")
            if isinstance(s, (int, float)) and not (0 <= s <= 100):
                errs.append({"field": f"{prefix}.socPercent",
                             "detail": f"out of range [0, 100] (got {s})"})

            c = it.get("cycleCount")
            if c is not None:
                if not isinstance(c, int) or isinstance(c, bool):
                    errs.append({"field": f"{prefix}.cycleCount", "detail": "must be int"})
                elif c < 0:
                    errs.append({"field": f"{prefix}.cycleCount", "detail": "must be >= 0"})

            # Sprint 3 — optional production fields
            soh = it.get("sohPercent")
            if soh is not None and (not isinstance(soh, (int, float)) or not (0 <= soh <= 100)):
                errs.append({"field": f"{prefix}.sohPercent", "detail": "must be number 0..100"})

            cs = it.get("chargingState")
            if cs is not None and (not isinstance(cs, int) or cs < 1 or cs > 5):
                errs.append({"field": f"{prefix}.chargingState",
                             "detail": "must be int 1..5 (Idle/Charging/Discharging/Float/Bypass)"})

            bec = it.get("bmsErrorCode")
            if bec is not None and (not isinstance(bec, str) or len(bec) > 64):
                errs.append({"field": f"{prefix}.bmsErrorCode", "detail": "must be string ≤ 64 chars"})

            ssc = it.get("sensorSourceCode")
            if ssc is not None and (not isinstance(ssc, str) or len(ssc) > 20):
                errs.append({"field": f"{prefix}.sensorSourceCode", "detail": "must be string ≤ 20 chars"})

            st = it.get("sourceType")
            if st is not None and (not isinstance(st, int) or st < 1 or st > 3):
                errs.append({"field": f"{prefix}.sourceType",
                             "detail": "must be int 1..3 (Bms/IotGateway/External)"})

    return (not errs), errs

# Alias backward-compat cho code cũ
validate_legacy_batch = validate_batch


# ---------------- Handler ----------------
class MockHandler(BaseHTTPRequestHandler):
    state: State = None  # type: ignore — set bởi server factory
    fail_rate: float = 0.0
    expected_api_key: str | None = None
    require_api_key: bool = False

    server_version = "MockBackend/1.0"

    def log_message(self, format, *args):
        # Tắt log mặc định của BaseHTTPRequestHandler để dùng log đẹp riêng.
        pass

    # ---- helpers ----
    def _send_json(self, code: int, body: dict, extra_headers: dict | None = None):
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        for k, v in (extra_headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def _read_body(self) -> bytes:
        n = int(self.headers.get("Content-Length", "0") or "0")
        return self.rfile.read(n) if n > 0 else b""

    def _print_request(self, status_color, code: int, label: str, extra: str = ""):
        ts = datetime.now().strftime("%H:%M:%S")
        dev = self.headers.get("X-Device-Code", "?")
        print(f"{GRAY(ts)} {status_color(f'{code}')} {BOLD(self.command)} {self.path} "
              f"device={CYAN(dev)} {label} {extra}")

    # ---- routes ----
    def do_GET(self):
        if self.path == "/healthz":
            self._send_json(200, {"status": "ok", "uptime": time.time() - self.state.start_ts})
            return
        if self.path.startswith("/readings"):
            self._dashboard_readings()
            return
        if self.path == "/" or self.path.startswith("/?"):
            self._dashboard_root()
            return
        self._send_json(404, {"error": "not found"})

    def do_POST(self):
        # Inject fail-rate (test retry Sprint 3 — không dùng trong S1)
        if self.fail_rate > 0 and random.random() < self.fail_rate:
            self.state.record_fail()
            self._print_request(RED, 500, "injected-fail")
            self._send_json(500, {"isSuccess": False, "message": "injected failure"})
            return

        # Optional API key check
        if self.require_api_key:
            got = self.headers.get("X-Api-Key", "")
            if not got or (self.expected_api_key and got != self.expected_api_key):
                self._print_request(RED, 401, "missing/wrong X-Api-Key")
                self._send_json(401, {"isSuccess": False, "message": "missing or invalid X-Api-Key"})
                return

        if self.path == "/api/sensor-readings/batch":
            self._handle_batch()
        elif self.path == "/api/iot-devices/provision":
            self._handle_provision()
        elif self.path == "/api/iot-devices/heartbeat":
            self._handle_heartbeat()
        else:
            self._send_json(404, {"error": f"no route for POST {self.path}"})

    # ---- handlers ----
    def _handle_batch(self):
        raw = self._read_body()
        try:
            body = json.loads(raw)
        except json.JSONDecodeError as e:
            self._print_request(RED, 400, f"bad json: {e}")
            self._send_json(400, {"isSuccess": False, "message": f"bad json: {e}"})
            return

        device_code = self.headers.get("X-Device-Code", "?")
        idemp_key   = self.headers.get("Idempotency-Key", "")

        # Sprint 3 (#IoT2-16): idempotency dedup — kiểm tra TRƯỚC khi validate
        # (backend chấp nhận retry same key → trả lại response cũ).
        if idemp_key:
            cached = self.state.get_idempotent_response(device_code, idemp_key)
            if cached is not None:
                code = cached.get("__statusCode", 200)
                self._print_request(CYAN, code, "idempotent-replay",
                                    f"key={idemp_key[:8]}...")
                # Loại bỏ field internal trước khi gửi
                payload = {k: v for k, v in cached.items() if not k.startswith("__")}
                self._send_json(code, payload)
                return

        ok, errs = validate_batch(body)
        if not ok:
            first = errs[0]
            self._print_request(RED, 400, f"validation {len(errs)} errors",
                                f"first={first['field']}: {first['detail']}")
            self._send_json(400, {
                "isSuccess": False,
                "statusCode": 400,
                "message": "validation failed",
                "listErrors": errs,
            })
            return

        # Sprint 3 (#IoT2-15): clock skew check per item
        from datetime import datetime as _dt
        items = body["items"]
        now = _dt.now(timezone.utc)
        skew_errs = []
        for i, it in enumerate(items):
            dts = it.get("deviceTimestamp")
            if not dts:
                continue
            try:
                ts = _dt.fromisoformat(dts.replace("Z", "+00:00"))
                skew_sec = abs((ts - now).total_seconds())
                if skew_sec > 300:    # 5 phút
                    skew_errs.append({
                        "field": f"items[{i}].deviceTimestamp",
                        "detail": f"clock skew {int(skew_sec)}s > 300s (5 phút)",
                    })
            except ValueError:
                pass
        if skew_errs:
            self._print_request(RED, 400, "clock_drift",
                                f"items={len(skew_errs)} skewed")
            self._send_json(400, {
                "isSuccess": False,
                "statusCode": 400,
                "message": "clock drift",
                "listErrors": skew_errs,
            })
            return

        self.state.record(device_code, body, len(items))

        first = items[0]
        # Sprint 3: log source type breakdown nếu có
        sources = {it.get("sourceType", "?") for it in items}
        src_breakdown = "/".join(str(s) for s in sorted(sources)) if sources else ""
        extra = (f"items={len(items)} v={first.get('voltage'):.2f}V "
                 f"t={first.get('temperature'):.1f}°C soc={first.get('socPercent'):.1f}% "
                 f"src={src_breakdown}")
        # Backend api-battery.md returns 201 Created cho POST insert.
        # (S1-FW-06 spec viết "200" — nhưng backend code dùng 201; firmware check 2xx).
        self._print_request(GREEN, 201, "batch", extra)
        response = {
            "isSuccess": True,
            "statusCode": 201,
            "data": {
                "totalReceived": len(items),
                "inserted":      len(items),
                "skipped":       0,
            },
            "__statusCode": 201,
        }
        # Sprint 3: cache cho idempotency replay
        if idemp_key:
            self.state.store_idempotent_response(device_code, idemp_key, response)
        # Loại bỏ internal field khi gửi
        payload = {k: v for k, v in response.items() if not k.startswith("__")}
        self._send_json(201, payload)

    def _handle_provision(self):
        """Sprint 2 S2-FW-02 — schema khớp IotDeviceProvisionResultDto của backend."""
        raw = self._read_body()
        try:
            body = json.loads(raw) if raw else {}
        except json.JSONDecodeError:
            body = {}

        fw  = body.get("FirmwareVersion") or body.get("firmwareVersion") or "?"
        hw  = body.get("HardwareRevision") or body.get("hardwareRevision") or "?"
        ts  = body.get("DeviceTimestamp") or body.get("deviceTimestamp") or "?"
        device_code = self.headers.get("X-Device-Code", "GW-ESP32-MVP-001")

        self._print_request(YELLOW, 200, "provision",
                            f"fw={fw} hw={hw} ts={ts}")
        self._send_json(200, {
            "isSuccess": True,
            "statusCode": 200,
            "data": {
                "deviceId":   "11111111-1111-4111-8111-d00000000001",
                "deviceCode": device_code,
                "siteId":     "11111111-1111-1111-1111-111111111111",
                "heartbeatIntervalSeconds": 60,
                "pollingIntervalSeconds":   5,
                "ntpServer":  "time.google.com",
                "apiKeyScopes": 7,   # SensorIngest|DeviceProvision|DeviceHeartbeat
                "targetFirmwareVersion": None,
                "batteryMappings": [
                    {"batteryAssetSerial": "BAT-MOCK-001", "unitId": 1, "sensorSourceCode": "primary"},
                    {"batteryAssetSerial": "BAT-MOCK-002", "unitId": 2, "sensorSourceCode": "primary"},
                    {"batteryAssetSerial": "BAT-MOCK-003", "unitId": 3, "sensorSourceCode": "primary"},
                    {"batteryAssetSerial": "BAT-MOCK-004", "unitId": 4, "sensorSourceCode": "primary"},
                ],
                "supportedSensors": ["voltage", "current", "temperature", "soc"],
            },
        })

    def _handle_heartbeat(self):
        """Sprint 2 S2-FW-03 — schema khớp IotHeartbeatAckDto."""
        raw = self._read_body()
        try:
            body = json.loads(raw) if raw else {}
        except json.JSONDecodeError:
            body = {}

        device_code = self.headers.get("X-Device-Code", "?")
        temp = body.get("Temperature") or body.get("temperature")
        rssi = body.get("SignalStrengthDbm") or body.get("RssiDbm") or body.get("signalStrengthDbm")
        mem  = body.get("MemoryUsageMb") or body.get("memoryUsageMb")
        qd   = body.get("LocalQueueDepth") or body.get("QueuedReadingCount")
        ts   = body.get("DeviceTimestamp") or body.get("deviceTimestamp") or ""

        with self.state.lock:
            d = self.state.by_device.setdefault(device_code, {"batches": 0, "readings": 0, "lastSeen": 0})
            d["lastSeen"] = time.time()
            d["lastHeartbeat"] = {"temp": temp, "rssi": rssi, "memMb": mem, "queueDepth": qd, "ts": ts}

        extra = f"temp={temp}°C rssi={rssi}dBm mem={mem}MB queue={qd}"
        self._print_request(CYAN, 200, "heartbeat", extra)

        # ClockSkewSeconds = 0 nếu ts trùng server time (mock không enforce).
        self._send_json(200, {
            "isSuccess": True,
            "statusCode": 200,
            "data": {
                "serverTime": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
                "clockSkewSeconds": 0.0,
                "clockSkewWarning": False,
                "nextHeartbeatInSeconds": 60,
                "firmwareUpdateAvailable": False,
            },
        })

    # ---- dashboard ----
    def _dashboard_root(self):
        s = self.state
        uptime = time.time() - s.start_ts
        out = []
        out.append("=== mock-backend — dashboard ===")
        out.append(f"uptime: {uptime:.0f}s")
        out.append(f"batches: {s.batch_count}   readings: {s.reading_count}   fails: {s.fail_count}")
        out.append("")
        out.append("Devices:")
        if not s.by_device:
            out.append("  (none yet)")
        for code, d in sorted(s.by_device.items()):
            ago = time.time() - d["lastSeen"]
            out.append(f"  {code}: batches={d['batches']} readings={d['readings']} lastSeen={ago:.0f}s ago")
        out.append("")
        out.append("Batteries (latest snapshot):")
        if not s.by_battery:
            out.append("  (none yet)")
        for bid, b in sorted(s.by_battery.items()):
            out.append(f"  {bid[:12]}…  count={b['count']:4d}  V={b['lastVoltage']:.2f}  "
                       f"I={b['lastCurrent']:.2f}A  T={b['lastTemp']:.1f}°C  SOC={b['lastSoc']:.1f}%  "
                       f"@ {b['lastSeenIso']}")
        body = "\n".join(out) + "\n"
        data = body.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Refresh", "2")
        self.end_headers()
        self.wfile.write(data)

    def _dashboard_readings(self):
        # /readings?limit=20 — trả N batch gần nhất dạng JSON.
        m = re.search(r"limit=(\d+)", self.path)
        limit = int(m.group(1)) if m else 20
        with self.state.lock:
            batches = list(self.state.recent_batches)[:limit]
        self._send_json(200, {"count": len(batches), "batches": batches})


# ---------------- TLS cert auto-generation ----------------
DEFAULT_CERT_DIR = Path(__file__).parent / "certs"

def ensure_self_signed(cert_dir: Path) -> tuple[Path, Path]:
    cert_dir.mkdir(parents=True, exist_ok=True)
    crt = cert_dir / "server.crt"
    key = cert_dir / "server.key"
    if crt.exists() and key.exists():
        return crt, key

    print(GRAY(f"[cert] Sinh self-signed cert tại {cert_dir} (1 lần)..."))
    cmd = [
        "openssl", "req", "-x509", "-newkey", "rsa:2048",
        "-keyout", str(key), "-out", str(crt),
        "-days", "365", "-nodes",
        "-subj", "/CN=mock-backend",
        "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
    ]
    try:
        subprocess.run(cmd, check=True, capture_output=True)
    except FileNotFoundError:
        print(RED("[cert] openssl không có sẵn. Cài: brew install openssl"))
        sys.exit(2)
    except subprocess.CalledProcessError as e:
        print(RED(f"[cert] openssl fail: {e.stderr.decode(errors='ignore')}"))
        sys.exit(2)
    print(GRAY(f"[cert] OK ({crt.name}, {key.name})"))
    return crt, key


# ---------------- Main ----------------
def main():
    p = argparse.ArgumentParser(description="Mock backend cho firmware-esp32 Sprint 1")
    p.add_argument("--host", default="0.0.0.0",
                   help="Bind address (mặc định 0.0.0.0 cho LAN — ESP32 truy cập được)")
    p.add_argument("--port", type=int, default=7200,
                   help="Port (mặc định 7200 khớp BatteryService dev)")
    p.add_argument("--no-tls", action="store_true",
                   help="Chạy HTTP plain — bỏ self-signed cert")
    p.add_argument("--cert-dir", type=Path, default=DEFAULT_CERT_DIR,
                   help="Thư mục lưu self-signed cert")
    p.add_argument("--fail-rate", type=float, default=0.0,
                   help="0..1 — xác suất trả 500 (giả lập transient error)")
    p.add_argument("--require-api-key", action="store_true",
                   help="Bắt buộc header X-Api-Key (Sprint 2 mode)")
    p.add_argument("--api-key", default=None,
                   help="Khi --require-api-key: so khớp exact (nếu không set → chỉ cần non-empty)")
    args = p.parse_args()

    if not (0.0 <= args.fail_rate <= 1.0):
        print(RED("--fail-rate phải trong [0, 1]"), file=sys.stderr); sys.exit(2)

    state = State()
    MockHandler.state = state
    MockHandler.fail_rate = args.fail_rate
    MockHandler.require_api_key = args.require_api_key
    MockHandler.expected_api_key = args.api_key

    server = ThreadingHTTPServer((args.host, args.port), MockHandler)

    if not args.no_tls:
        crt, key = ensure_self_signed(args.cert_dir)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(certfile=str(crt), keyfile=str(key))
        server.socket = ctx.wrap_socket(server.socket, server_side=True)
        scheme = "https"
    else:
        scheme = "http"

    bind_disp = "0.0.0.0 (all LAN)" if args.host == "0.0.0.0" else args.host
    print(BOLD(GREEN(f"\n  mock-backend listening on {scheme}://{bind_disp}:{args.port}")))
    print(GRAY( f"  dashboard:  {scheme}://localhost:{args.port}/"))
    print(GRAY( f"  readings:   {scheme}://localhost:{args.port}/readings?limit=10"))
    print(GRAY( f"  healthz:    {scheme}://localhost:{args.port}/healthz"))
    if args.fail_rate > 0:
        print(YELLOW(f"  fail-rate:  {args.fail_rate:.0%}  (giả lập transient — Sprint 3 retry)"))
    if args.require_api_key:
        print(YELLOW(f"  api-key enforced: {'exact=' + args.api_key if args.api_key else 'any non-empty'}"))
    print(GRAY( "  Ctrl-C để dừng\n"))

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()
        print(BOLD(f"  Tổng kết: {state.batch_count} batch, {state.reading_count} reading, "
                   f"{state.fail_count} fail."))
        print(BOLD("  bye."))


if __name__ == "__main__":
    main()
