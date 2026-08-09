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
import enum
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

# GH-738 — KHÔNG sys.exit() ở tầng import.
# Thoát ngay lúc import làm cả module không import được, nên phần logic thuần (phân loại
# HTTP status) không thể viết test — mà đó đúng là chỗ đã sai. Lỗi thiếu `requests` được
# báo trong main(), hành vi khi chạy CLI không đổi.
try:
    import requests
except ImportError:  # pragma: no cover - phụ thuộc môi trường
    requests = None


DEFAULT_HEARTBEAT_INTERVAL_S = 60
DEFAULT_INGEST_INTERVAL_S = 15
DEFAULT_BATCH_SIZE = 6
DEFAULT_QUEUE_PATH = Path("./queue.jsonl")


def iso_utc_now() -> str:
    r"""GH-739 — mốc thời gian ĐÚNG dạng firmware thật phát ra.

    Firmware dùng ``strftime("%Y-%m-%dT%H:%M:%SZ")`` (xem ``net::isoNow``) → ``...42Z``.
    ``datetime.isoformat()`` của Python lại cho ``...42.789012+00:00`` — vừa có phần lẻ giây
    vừa dùng offset thay vì ``Z``. Mock backend kiểm bằng
    ``^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?Z$`` nên dạng offset bị từ chối.

    Đây là lỗi thứ hai lộ ra khi viết test hợp đồng cho GH-739 — cùng gốc: simulator sinh ra
    JSON khác với thiết bị thật.
    """
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


@dataclass
class SensorReading:
    """GH-739 — tên trường TRÙNG KHÍT chuỗi JSON mà firmware thật gửi (camelCase).

    Trước đây dataclass này dùng PascalCase (``Time``, ``BatteryAssetSerial``, ``Items``),
    trong khi firmware ESP32 (``core::buildProductionBatchPayload``) gửi camelCase
    (``time``, ``batteryAssetSerial``, ``items``). Simulator vì thế KHÔNG mô phỏng đúng
    thiết bị — nó chỉ "chạy được" với backend ASP.NET thật vì ASP.NET bind không phân biệt
    hoa thường, che mất chỗ lệch; còn mock backend (nghiêm ngặt, đúng như đặc tả) trả 400.

    Giữ tên trường Python trùng tên trên dây để ``asdict()`` sinh ra đúng payload, không
    cần một lớp ánh xạ nữa có thể trôi khỏi hợp đồng.
    """

    time: str
    batteryAssetSerial: str  # noqa: N815 - khớp tên trên dây
    voltage: float
    current: float
    temperature: float
    socPercent: float  # noqa: N815 - khớp tên trên dây
    deviceTimestamp: str  # noqa: N815 - khớp tên trên dây
    sourceType: int = 2  # noqa: N815 - khớp tên trên dây; 2 = IotGateway

    @staticmethod
    def mock_for(serial: str, t: float, base_v: float = 3.7) -> "SensorReading":
        voltage = round(base_v + 0.05 * math.sin(t / 30.0) + random.uniform(-0.01, 0.01), 3)
        current = round(random.uniform(-2.0, 2.0), 3)
        temperature = round(25.0 + 2.0 * math.sin(t / 120.0) + random.uniform(-0.5, 0.5), 2)
        soc = round(70.0 + 10.0 * math.sin(t / 600.0), 2)
        now_utc = iso_utc_now()
        return SensorReading(
            time=now_utc,
            batteryAssetSerial=serial,
            voltage=voltage,
            current=current,
            temperature=temperature,
            socPercent=max(0.0, min(100.0, soc)),
            deviceTimestamp=now_utc,
        )


class Outcome(enum.Enum):
    """GH-738 — kết quả một lần gửi, quyết định giữ hay bỏ bản ghi."""

    SUCCESS = "success"
    TRANSIENT = "transient"   # lỗi tạm thời → giữ trong hàng đợi, thử lại sau
    PERMANENT = "permanent"   # gửi lại cũng vô ích → bỏ, đừng chặn hàng đợi


# Mã lỗi 4xx nhưng THỬ LẠI CÓ TÁC DỤNG.
_RETRYABLE_4XX = frozenset({408, 425, 429})


def classify_response(status_code: int) -> Outcome:
    """Phân loại HTTP status theo hợp đồng ingest của backend.

    Vì sao không so ``== 200``: endpoint ``/api/sensor-readings/batch`` trả **201 Created**
    cho lần ghi mới và **200 OK** cho ca trùng idempotent. Simulator cũ chỉ nhận 200 nên MỌI
    lần ghi mới đều bị ghi log FAIL rồi đẩy vào hàng đợi — và vì phần flush chỉ chạy sau một
    lần gửi live thành công, hàng đợi không bao giờ vơi.

    Phân loại thay vì chỉ đúng/sai: 4xx là dữ liệu sai, gửi lại vẫn sai — giữ nó trong hàng
    đợi sẽ chặn vĩnh viễn mọi bản ghi phía sau (cùng lớp lỗi starvation với GH-725).
    """
    if 200 <= status_code < 300:
        return Outcome.SUCCESS
    if status_code in _RETRYABLE_4XX:
        return Outcome.TRANSIENT
    if 400 <= status_code < 500:
        return Outcome.PERMANENT
    return Outcome.TRANSIENT


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
            "DeviceTimestamp": iso_utc_now(),
        }
        return self.session.post(f"{self.base_url}/api/iot-devices/provision", json=body, timeout=10)

    def heartbeat(self, firmware_version: Optional[str], queued: int, uptime_s: int) -> requests.Response:
        body = {
            "FirmwareVersion": firmware_version,
            "RssiDbm": random.randint(-80, -40),
            "FreeMemoryPercent": round(random.uniform(30.0, 80.0), 2),
            "UptimeSeconds": uptime_s,
            "QueuedReadingCount": queued,
            "DeviceTimestamp": iso_utc_now(),
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
        body = {"items": [asdict(r) for r in readings]}
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

    # Kiểm SAU parse_args() để `--help` vẫn dùng được khi chưa cài requests.
    if requests is None:
        print("[ERR] Cần cài requests: pip install requests", file=sys.stderr)
        return 2
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
                if classify_response(r.status_code) is Outcome.SUCCESS:
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
            payload = {"items": [asdict(r) for r in readings]}
            idem_key = str(uuid.uuid4())

            sent = False
            try:
                r = client.ingest(readings, idem_key)
                outcome = classify_response(r.status_code)
                if outcome is Outcome.SUCCESS:
                    print(f"[ingest] OK {r.status_code} batch={len(readings)} resp={r.text[:120]}")
                    sent = True
                elif outcome is Outcome.PERMANENT:
                    # 4xx: dữ liệu sai, gửi lại vẫn sai → BỎ, không đẩy vào hàng đợi.
                    print(f"[ingest] DROPPED {r.status_code} (permanent) {r.text[:200]}")
                    sent = True          # coi như đã xử lý xong, không xếp hàng
                else:
                    print(f"[ingest] FAIL {r.status_code} (transient) {r.text[:200]}")
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
                            outcome = classify_response(r.status_code)
                            if outcome is Outcome.SUCCESS:
                                flushed += 1
                            elif outcome is Outcome.PERMANENT:
                                # Bỏ bản ghi hỏng thay vì dừng: một batch 4xx nằm đầu hàng
                                # đợi sẽ chặn vĩnh viễn toàn bộ phần phía sau.
                                print(f"[queue] DROPPED {r.status_code} (permanent)")
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
