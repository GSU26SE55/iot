#!/usr/bin/env python3
"""
Sprint IoT-1 (#250) — ESP32 simulator for end-to-end testing.

Mô phỏng một ESP32 edge device đẩy heartbeat + sensor batch lên BatteryService.
Hỗ trợ:
  - Provision → heartbeat → ingest loop.
  - Local queue (file ./queue.jsonl) khi backend down → retry với Idempotency-Key.
  - Mock BMS data (sine voltage, random current/temperature) khi chưa có BMS thật.

Sử dụng:
  python3 esp32_simulator.py \\
      --base-url https://localhost:7200 \\
      --device-code ESP32-001 \\
      --api-key iotk_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx \\
      --battery-serials BAT-001,BAT-002

Biến môi trường thay thế CLI:
  IOT_BASE_URL, IOT_DEVICE_CODE, IOT_API_KEY, IOT_BATTERY_SERIALS

Yêu cầu: Python 3.10+, `requests` (pip install requests).
"""
from __future__ import annotations

import argparse
import json
import math
import os
import random
import signal
import sys
import time
import uuid
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import List, Optional

try:
    import requests
except ImportError:
    print("[ERR] Cần cài requests: pip install requests", file=sys.stderr)
    sys.exit(2)


DEFAULT_HEARTBEAT_INTERVAL_S = 60
DEFAULT_INGEST_INTERVAL_S = 15
DEFAULT_BATCH_SIZE = 6
DEFAULT_QUEUE_PATH = Path("./queue.jsonl")


@dataclass
class SensorReading:
    Time: str
    BatteryAssetSerial: str
    Voltage: float
    Current: float
    Temperature: float
    SocPercent: float
    DeviceTimestamp: str
    SourceType: int = 2  # IotGateway

    @staticmethod
    def mock_for(serial: str, t: float, base_v: float = 3.7) -> "SensorReading":
        voltage = round(base_v + 0.05 * math.sin(t / 30.0) + random.uniform(-0.01, 0.01), 3)
        current = round(random.uniform(-2.0, 2.0), 3)
        temperature = round(25.0 + 2.0 * math.sin(t / 120.0) + random.uniform(-0.5, 0.5), 2)
        soc = round(70.0 + 10.0 * math.sin(t / 600.0), 2)
        now_utc = datetime.now(timezone.utc).isoformat()
        return SensorReading(
            Time=now_utc,
            BatteryAssetSerial=serial,
            Voltage=voltage,
            Current=current,
            Temperature=temperature,
            SocPercent=max(0.0, min(100.0, soc)),
            DeviceTimestamp=now_utc,
        )


class LocalQueue:
    """Append-only JSONL queue, mỗi line là 1 batch ingest payload."""

    def __init__(self, path: Path):
        self.path = path
        self.path.parent.mkdir(parents=True, exist_ok=True)
        if not self.path.exists():
            self.path.touch()

    def append(self, payload: dict, idempotency_key: str) -> None:
        with self.path.open("a", encoding="utf-8") as f:
            f.write(json.dumps({"key": idempotency_key, "payload": payload}) + "\n")

    def drain(self):
        if not self.path.exists():
            return []
        with self.path.open("r", encoding="utf-8") as f:
            lines = [ln for ln in f.read().splitlines() if ln.strip()]
        return [json.loads(ln) for ln in lines]

    def remove_first(self, count: int) -> None:
        lines = []
        if self.path.exists():
            with self.path.open("r", encoding="utf-8") as f:
                lines = [ln for ln in f.read().splitlines() if ln.strip()]
        with self.path.open("w", encoding="utf-8") as f:
            for ln in lines[count:]:
                f.write(ln + "\n")

    def size(self) -> int:
        if not self.path.exists():
            return 0
        with self.path.open("r", encoding="utf-8") as f:
            return sum(1 for ln in f if ln.strip())


class IotClient:
    def __init__(self, base_url: str, device_code: str, api_key: str, verify_tls: bool = True):
        self.base_url = base_url.rstrip("/")
        self.device_code = device_code
        self.api_key = api_key
        self.session = requests.Session()
        self.session.verify = verify_tls
        self.session.headers.update({
            "X-Api-Key": api_key,
            "X-Device-Code": device_code,
            "User-Agent": "esp32-simulator/1.0",
        })

    def provision(self, firmware_version: str, hw_revision: Optional[str] = None) -> requests.Response:
        body = {
            "FirmwareVersion": firmware_version,
            "HardwareRevision": hw_revision,
            "DeviceTimestamp": datetime.now(timezone.utc).isoformat(),
        }
        return self.session.post(f"{self.base_url}/api/iot-devices/provision", json=body, timeout=10)

    def heartbeat(self, firmware_version: Optional[str], queued: int, uptime_s: int) -> requests.Response:
        body = {
            "FirmwareVersion": firmware_version,
            "RssiDbm": random.randint(-80, -40),
            "FreeMemoryPercent": round(random.uniform(30.0, 80.0), 2),
            "UptimeSeconds": uptime_s,
            "QueuedReadingCount": queued,
            "DeviceTimestamp": datetime.now(timezone.utc).isoformat(),
        }
        return self.session.post(f"{self.base_url}/api/iot-devices/heartbeat", json=body, timeout=10)

    def firmware_check(self, current_version: str) -> requests.Response:
        return self.session.get(
            f"{self.base_url}/api/iot-devices/firmware-check",
            params={"currentVersion": current_version},
            timeout=10,
        )

    def ingest(self, readings: List[SensorReading], idempotency_key: str) -> requests.Response:
        headers = {"Idempotency-Key": idempotency_key}
        body = {"Items": [asdict(r) for r in readings]}
        return self.session.post(
            f"{self.base_url}/api/sensor-readings/batch",
            json=body,
            headers=headers,
            timeout=15,
        )


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="ESP32 simulator — Sprint IoT-1 (#250)")
    p.add_argument("--base-url", default=os.getenv("IOT_BASE_URL", "https://localhost:7200"))
    p.add_argument("--device-code", default=os.getenv("IOT_DEVICE_CODE", "ESP32-SIM-001"))
    p.add_argument("--api-key", default=os.getenv("IOT_API_KEY", ""))
    p.add_argument("--firmware-version", default=os.getenv("IOT_FW_VERSION", "1.0.0-sim"))
    p.add_argument("--hardware-revision", default=os.getenv("IOT_HW_REVISION", "v1.0-sim"))
    p.add_argument("--battery-serials", default=os.getenv("IOT_BATTERY_SERIALS", "BAT-001"))
    p.add_argument("--heartbeat-interval", type=int, default=DEFAULT_HEARTBEAT_INTERVAL_S)
    p.add_argument("--ingest-interval", type=int, default=DEFAULT_INGEST_INTERVAL_S)
    p.add_argument("--batch-size", type=int, default=DEFAULT_BATCH_SIZE)
    p.add_argument("--queue-path", default=str(DEFAULT_QUEUE_PATH))
    p.add_argument("--insecure", action="store_true", help="Tắt TLS verify (dev only)")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    if not args.api_key:
        print("[ERR] Thiếu --api-key hoặc env IOT_API_KEY", file=sys.stderr)
        return 2

    serials = [s.strip() for s in args.battery_serials.split(",") if s.strip()]
    if not serials:
        print("[ERR] Cần --battery-serials có ít nhất 1 serial", file=sys.stderr)
        return 2

    client = IotClient(args.base_url, args.device_code, args.api_key, verify_tls=not args.insecure)
    queue = LocalQueue(Path(args.queue_path))

    print(f"[INFO] Simulator started — device={args.device_code} serials={serials} backend={args.base_url}")
    print(f"[INFO] Local queue: {args.queue_path} (size={queue.size()})")

    # Provision (best effort).
    try:
        r = client.provision(args.firmware_version, args.hardware_revision)
        print(f"[provision] {r.status_code} {r.text[:200]}")
    except requests.RequestException as ex:
        print(f"[provision] WARN: {ex}")

    stop = {"flag": False}
    signal.signal(signal.SIGINT, lambda *_: stop.update(flag=True))
    signal.signal(signal.SIGTERM, lambda *_: stop.update(flag=True))

    start_t = time.time()
    last_hb = 0.0
    last_ing = 0.0

    while not stop["flag"]:
        now = time.time()
        uptime = int(now - start_t)

        if now - last_hb >= args.heartbeat_interval:
            try:
                r = client.heartbeat(args.firmware_version, queue.size(), uptime)
                print(f"[heartbeat] {r.status_code} queued={queue.size()}")
                if r.status_code == 200:
                    try:
                        data = r.json().get("data", {})
                        if data.get("clockSkewWarning"):
                            print(f"[heartbeat] WARN clock skew {data.get('clockSkewSeconds'):.1f}s")
                    except ValueError:
                        pass
            except requests.RequestException as ex:
                print(f"[heartbeat] FAIL: {ex}")
            last_hb = now

        if now - last_ing >= args.ingest_interval:
            readings = [SensorReading.mock_for(random.choice(serials), now + i) for i in range(args.batch_size)]
            payload = {"Items": [asdict(r) for r in readings]}
            idem_key = str(uuid.uuid4())

            sent = False
            try:
                r = client.ingest(readings, idem_key)
                if r.status_code == 200:
                    print(f"[ingest] OK batch={len(readings)} resp={r.text[:120]}")
                    sent = True
                else:
                    print(f"[ingest] FAIL {r.status_code} {r.text[:200]}")
            except requests.RequestException as ex:
                print(f"[ingest] NETWORK FAIL: {ex}")

            if not sent:
                queue.append(payload, idem_key)
                print(f"[queue] saved → size={queue.size()}")
            else:
                drained = queue.drain()
                if drained:
                    print(f"[queue] flushing {len(drained)} pending batch(es)...")
                    flushed = 0
                    for item in drained:
                        try:
                            r = client.session.post(
                                f"{client.base_url}/api/sensor-readings/batch",
                                json=item["payload"],
                                headers={"Idempotency-Key": item["key"]},
                                timeout=15,
                            )
                            if r.status_code == 200:
                                flushed += 1
                            else:
                                break
                        except requests.RequestException:
                            break
                    queue.remove_first(flushed)
                    print(f"[queue] flushed={flushed}, remaining={queue.size()}")

            last_ing = now

        time.sleep(1)

    print("[INFO] Simulator stopped.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
