from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any

from gateway.models import BatteryMapping, CalibrationRule, GatewayConfig


class ConfigError(ValueError):
    pass


def load_config(path: str | Path) -> GatewayConfig:
    config_path = Path(path)
    if not config_path.exists():
        raise ConfigError(f"Config file not found: {config_path}")

    with config_path.open("r", encoding="utf-8") as file:
        data = json.load(file)

    data = _apply_env_overrides(data)
    mappings = [BatteryMapping.from_dict(item) for item in data.get("batteryMappings", [])]
    calibrations = [CalibrationRule.from_dict(item) for item in data.get("calibrations", [])]

    config = GatewayConfig(
        backend_base_url=str(data.get("backendBaseUrl", "")).strip(),
        api_key=str(data.get("apiKey", "")).strip(),
        device_code=str(data.get("deviceCode", "")).strip(),
        ingest_mode=str(data.get("ingestMode", "legacy")).strip().lower(),  # type: ignore[arg-type]
        adapter=str(data.get("adapter", "mock")).strip().lower(),  # type: ignore[arg-type]
        polling_interval_sec=int(data.get("pollingIntervalSec", 30)),
        heartbeat_interval_sec=int(data.get("heartbeatIntervalSec", 60)),
        batch_size=int(data.get("batchSize", 20)),
        request_timeout_sec=int(data.get("requestTimeoutSec", 10)),
        clock_skew_max_sec=int(data.get("clockSkewMaxSec", 300)),
        queue_db_path=str(data.get("queueDbPath", "data/gateway_queue.sqlite3")),
        dry_run=bool(data.get("dryRun", False)),
        log_level=str(data.get("logLevel", "INFO")).upper(),
        model=str(data.get("model", "Simulator")),
        firmware_version=str(data.get("firmwareVersion", "0.1.0")),
        mac_address=_optional_text(data.get("macAddress")),
        battery_mappings=mappings,
        calibration_rules=calibrations,
        hardware=dict(data.get("hardware", {})),
        mock=dict(data.get("mock", {})),
    )
    validate_config(config)
    return config


def validate_config(config: GatewayConfig) -> None:
    if config.ingest_mode not in {"legacy", "production"}:
        raise ConfigError("ingestMode must be either 'legacy' or 'production'.")
    if config.adapter not in {"mock", "modbus", "canbus"}:
        raise ConfigError("adapter must be one of: mock, modbus, canbus.")
    if not config.backend_base_url:
        raise ConfigError("backendBaseUrl is required.")
    if not config.dry_run and not config.backend_base_url.startswith(("http://", "https://")):
        raise ConfigError("backendBaseUrl must start with http:// or https://.")
    if not config.api_key:
        raise ConfigError("apiKey is required. Use env GATEWAY_API_KEY for secrets.")
    if not config.device_code:
        raise ConfigError("deviceCode is required.")
    if config.polling_interval_sec <= 0:
        raise ConfigError("pollingIntervalSec must be greater than 0.")
    if config.heartbeat_interval_sec <= 0:
        raise ConfigError("heartbeatIntervalSec must be greater than 0.")
    if config.batch_size <= 0:
        raise ConfigError("batchSize must be greater than 0.")
    if config.request_timeout_sec <= 0:
        raise ConfigError("requestTimeoutSec must be greater than 0.")
    if config.clock_skew_max_sec <= 0:
        raise ConfigError("clockSkewMaxSec must be greater than 0.")
    if not config.battery_mappings:
        raise ConfigError("At least one battery mapping is required.")

    for index, mapping in enumerate(config.battery_mappings):
        if not mapping.battery_asset_serial:
            raise ConfigError(f"batteryMappings[{index}].batteryAssetSerial is required.")
        if config.ingest_mode == "legacy" and not mapping.battery_asset_id:
            raise ConfigError(
                f"batteryMappings[{index}].batteryAssetId is required when ingestMode='legacy'."
            )
        if len(mapping.sensor_source_code) > 20:
            raise ConfigError(f"batteryMappings[{index}].sensorSourceCode must be <= 20 chars.")


def _apply_env_overrides(data: dict[str, Any]) -> dict[str, Any]:
    result = dict(data)
    env_map = {
        "GATEWAY_BACKEND_URL": "backendBaseUrl",
        "GATEWAY_API_KEY": "apiKey",
        "GATEWAY_DEVICE_CODE": "deviceCode",
        "GATEWAY_INGEST_MODE": "ingestMode",
        "GATEWAY_ADAPTER": "adapter",
        "GATEWAY_LOG_LEVEL": "logLevel",
        "GATEWAY_QUEUE_DB": "queueDbPath",
    }
    for env_name, key in env_map.items():
        value = os.getenv(env_name)
        if value:
            result[key] = value

    if os.getenv("GATEWAY_DRY_RUN"):
        result["dryRun"] = os.getenv("GATEWAY_DRY_RUN", "").lower() in {"1", "true", "yes"}

    return result


def _optional_text(value: Any) -> str | None:
    if value is None:
        return None
    text = str(value).strip()
    return text or None

