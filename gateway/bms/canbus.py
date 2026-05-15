from __future__ import annotations

from datetime import timezone
from typing import Any

from gateway.bms.base import BmsAdapter, BmsAdapterError
from gateway.models import BatteryMapping, SensorReading, utc_now


class CanBusBmsAdapter(BmsAdapter):
    """Generic CAN adapter configured by frame id and byte offsets."""

    def __init__(self, mappings: list[BatteryMapping], options: dict[str, Any]) -> None:
        super().__init__(mappings)
        if len(mappings) != 1:
            raise BmsAdapterError("Generic CAN adapter currently supports one battery mapping per gateway.")

        try:
            import can  # type: ignore
        except ImportError as exc:
            raise BmsAdapterError("python-can is required for adapter='canbus'. Install requirements-hardware.txt.") from exc

        self._can = can
        self._mapping = mappings[0]
        self._frames = dict(options.get("frames", {}))
        if not self._frames:
            raise BmsAdapterError("hardware.canbus.frames is required.")

        self._timeout_sec = float(options.get("timeoutSec", 2.0))
        self._bus = can.interface.Bus(
            channel=str(options.get("channel", "can0")),
            bustype=str(options.get("bustype", "socketcan")),
            bitrate=int(options.get("bitrate", 500000)),
        )

    def read(self) -> list[SensorReading]:
        values: dict[str, float] = {}
        deadline_frames = len(self._frames) * 4
        for _ in range(deadline_frames):
            message = self._bus.recv(timeout=self._timeout_sec)
            if message is None:
                break
            frame_config = self._frames.get(hex(message.arbitration_id).lower()) or self._frames.get(str(message.arbitration_id))
            if not frame_config:
                continue
            for metric, spec in frame_config.items():
                values[metric] = _decode_signal(bytes(message.data), spec)
            if _has_required(values):
                break

        if not _has_required(values):
            raise BmsAdapterError("CAN read did not collect required metrics: voltage/current/temperature/socPercent.")

        return [
            SensorReading(
                battery_asset_serial=self._mapping.battery_asset_serial,
                battery_asset_id=self._mapping.battery_asset_id,
                time=utc_now().astimezone(timezone.utc),
                voltage=float(values["voltage"]),
                current=float(values["current"]),
                temperature=float(values["temperature"]),
                soc_percent=float(values["socPercent"]),
                cycle_count=_optional_int(values.get("cycleCount")),
                soh_percent=_optional_float(values.get("sohPercent")),
                charging_state=_optional_int(values.get("chargingState")),
                bms_error_code=None,
                sensor_source_code=self._mapping.sensor_source_code,
            )
        ]

    def close(self) -> None:
        self._bus.shutdown()


def _decode_signal(data: bytes, spec: dict[str, Any]) -> float:
    start = int(spec.get("start", 0))
    length = int(spec.get("length", 2))
    endian = str(spec.get("endian", "little"))
    signed = bool(spec.get("signed", False))
    raw_bytes = data[start : start + length]
    raw = int.from_bytes(raw_bytes, byteorder=endian, signed=signed)
    return raw * float(spec.get("scale", 1.0)) + float(spec.get("offset", 0.0))


def _has_required(values: dict[str, float]) -> bool:
    return all(metric in values for metric in ("voltage", "current", "temperature", "socPercent"))


def _optional_float(value: Any) -> float | None:
    return None if value is None else float(value)


def _optional_int(value: Any) -> int | None:
    return None if value is None else int(value)

