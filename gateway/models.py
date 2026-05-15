from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any, Literal


IngestMode = Literal["legacy", "production"]
AdapterType = Literal["mock", "modbus", "canbus"]


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def to_iso_utc(value: datetime) -> str:
    if value.tzinfo is None:
        value = value.replace(tzinfo=timezone.utc)
    return value.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")


@dataclass(frozen=True)
class BatteryMapping:
    battery_asset_serial: str
    battery_asset_id: str | None = None
    sensor_source_code: str = "primary"

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "BatteryMapping":
        return cls(
            battery_asset_serial=str(data.get("batteryAssetSerial") or data.get("serial") or "").strip(),
            battery_asset_id=_optional_str(data.get("batteryAssetId")),
            sensor_source_code=str(data.get("sensorSourceCode") or data.get("source") or "primary").strip(),
        )


@dataclass(frozen=True)
class CalibrationRule:
    metric: str
    offset_value: float = 0.0
    scale_factor: float = 1.0

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "CalibrationRule":
        return cls(
            metric=str(data["metric"]).strip(),
            offset_value=float(data.get("offsetValue", data.get("offset", 0.0))),
            scale_factor=float(data.get("scaleFactor", data.get("scale", 1.0))),
        )


@dataclass(frozen=True)
class SensorReading:
    battery_asset_serial: str
    time: datetime
    voltage: float
    current: float
    temperature: float
    soc_percent: float
    battery_asset_id: str | None = None
    cycle_count: int | None = None
    soh_percent: float | None = None
    charging_state: int | None = None
    bms_error_code: str | None = None
    sensor_source_code: str = "primary"

    def to_legacy_dict(self) -> dict[str, Any]:
        if not self.battery_asset_id:
            raise ValueError("Legacy ingest requires battery_asset_id.")

        payload: dict[str, Any] = {
            "batteryAssetId": self.battery_asset_id,
            "time": to_iso_utc(self.time),
            "voltage": round(self.voltage, 2),
            "current": round(self.current, 2),
            "temperature": round(self.temperature, 2),
            "socPercent": round(self.soc_percent, 2),
            "sourceDeviceId": self.sensor_source_code,
        }
        _add_optional(payload, "cycleCount", self.cycle_count)
        _add_optional(payload, "sohPercent", _round_optional(self.soh_percent, 2))
        _add_optional(payload, "chargingState", self.charging_state)
        return payload

    def to_production_dict(self) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "batteryAssetSerial": self.battery_asset_serial,
            "time": to_iso_utc(self.time),
            "voltage": round(self.voltage, 2),
            "current": round(self.current, 2),
            "temperature": round(self.temperature, 2),
            "socPercent": round(self.soc_percent, 2),
            "sensorSourceCode": self.sensor_source_code,
        }
        _add_optional(payload, "cycleCount", self.cycle_count)
        _add_optional(payload, "sohPercent", _round_optional(self.soh_percent, 2))
        _add_optional(payload, "chargingState", self.charging_state)
        _add_optional(payload, "bmsErrorCode", self.bms_error_code)
        return payload

    def with_values(self, **changes: Any) -> "SensorReading":
        data = {
            "battery_asset_serial": self.battery_asset_serial,
            "time": self.time,
            "voltage": self.voltage,
            "current": self.current,
            "temperature": self.temperature,
            "soc_percent": self.soc_percent,
            "battery_asset_id": self.battery_asset_id,
            "cycle_count": self.cycle_count,
            "soh_percent": self.soh_percent,
            "charging_state": self.charging_state,
            "bms_error_code": self.bms_error_code,
            "sensor_source_code": self.sensor_source_code,
        }
        data.update(changes)
        return SensorReading(**data)


@dataclass(frozen=True)
class Heartbeat:
    timestamp: datetime
    connected_sensor_count: int
    local_queue_depth: int
    cpu: float | None = None
    memory_usage_mb: int | None = None
    disk_free_mb: int | None = None
    temperature: float | None = None
    signal_strength_dbm: int | None = None

    def to_dict(self) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "timestamp": to_iso_utc(self.timestamp),
            "connectedSensorCount": self.connected_sensor_count,
            "localQueueDepth": self.local_queue_depth,
        }
        _add_optional(payload, "cpu", _round_optional(self.cpu, 2))
        _add_optional(payload, "memoryUsageMb", self.memory_usage_mb)
        _add_optional(payload, "diskFreeMb", self.disk_free_mb)
        _add_optional(payload, "temperature", _round_optional(self.temperature, 2))
        _add_optional(payload, "signalStrengthDbm", self.signal_strength_dbm)
        return payload


@dataclass(frozen=True)
class GatewayConfig:
    backend_base_url: str
    api_key: str
    device_code: str
    ingest_mode: IngestMode = "legacy"
    adapter: AdapterType = "mock"
    polling_interval_sec: int = 30
    heartbeat_interval_sec: int = 60
    batch_size: int = 20
    request_timeout_sec: int = 10
    clock_skew_max_sec: int = 300
    queue_db_path: str = "data/gateway_queue.sqlite3"
    dry_run: bool = False
    log_level: str = "INFO"
    model: str = "Simulator"
    firmware_version: str = "0.1.0"
    mac_address: str | None = None
    battery_mappings: list[BatteryMapping] = field(default_factory=list)
    calibration_rules: list[CalibrationRule] = field(default_factory=list)
    hardware: dict[str, Any] = field(default_factory=dict)
    mock: dict[str, Any] = field(default_factory=dict)

    @property
    def normalized_backend_url(self) -> str:
        return self.backend_base_url.rstrip("/")


@dataclass(frozen=True)
class ApiResult:
    status_code: int
    body: str

    @property
    def is_success(self) -> bool:
        return 200 <= self.status_code < 300


def _optional_str(value: Any) -> str | None:
    if value is None:
        return None
    text = str(value).strip()
    return text or None


def _round_optional(value: float | None, digits: int) -> float | None:
    if value is None:
        return None
    return round(float(value), digits)


def _add_optional(payload: dict[str, Any], key: str, value: Any) -> None:
    if value is not None:
        payload[key] = value

