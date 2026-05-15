from __future__ import annotations

import os
import shutil
from pathlib import Path

from gateway.models import Heartbeat, utc_now


def collect_heartbeat(*, connected_sensor_count: int, local_queue_depth: int) -> Heartbeat:
    return Heartbeat(
        timestamp=utc_now(),
        connected_sensor_count=connected_sensor_count,
        local_queue_depth=local_queue_depth,
        cpu=_cpu_percent(),
        memory_usage_mb=_memory_usage_mb(),
        disk_free_mb=_disk_free_mb(),
        temperature=_gateway_temperature_c(),
        signal_strength_dbm=_wifi_signal_dbm(),
    )


def _cpu_percent() -> float | None:
    try:
        import psutil  # type: ignore

        return float(psutil.cpu_percent(interval=None))
    except ImportError:
        pass

    try:
        load_1m = os.getloadavg()[0]
        cpu_count = os.cpu_count() or 1
        return min(100.0, max(0.0, (load_1m / cpu_count) * 100.0))
    except (AttributeError, OSError):
        return None


def _memory_usage_mb() -> int | None:
    try:
        import psutil  # type: ignore

        return int(psutil.virtual_memory().used / 1024 / 1024)
    except ImportError:
        pass

    meminfo = Path("/proc/meminfo")
    if not meminfo.exists():
        return None
    values: dict[str, int] = {}
    for line in meminfo.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[1].isdigit():
            values[parts[0].rstrip(":")] = int(parts[1])
    total = values.get("MemTotal")
    available = values.get("MemAvailable")
    if total is None or available is None:
        return None
    return int((total - available) / 1024)


def _disk_free_mb() -> int | None:
    try:
        return int(shutil.disk_usage("/").free / 1024 / 1024)
    except OSError:
        return None


def _gateway_temperature_c() -> float | None:
    thermal_path = Path("/sys/class/thermal/thermal_zone0/temp")
    if not thermal_path.exists():
        return None
    try:
        return int(thermal_path.read_text(encoding="utf-8").strip()) / 1000.0
    except (ValueError, OSError):
        return None


def _wifi_signal_dbm() -> int | None:
    wireless_path = Path("/proc/net/wireless")
    if not wireless_path.exists():
        return None
    lines = wireless_path.read_text(encoding="utf-8").splitlines()[2:]
    for line in lines:
        parts = line.split()
        if len(parts) >= 4:
            try:
                return int(float(parts[3].rstrip(".")))
            except ValueError:
                continue
    return None

