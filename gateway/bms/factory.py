from __future__ import annotations

from gateway.bms.base import BmsAdapter
from gateway.bms.canbus import CanBusBmsAdapter
from gateway.bms.mock import MockBmsAdapter
from gateway.bms.modbus import ModbusBmsAdapter
from gateway.models import GatewayConfig


def create_bms_adapter(config: GatewayConfig) -> BmsAdapter:
    if config.adapter == "mock":
        return MockBmsAdapter(
            config.battery_mappings,
            scenario=str(config.mock.get("scenario", "normal")),
            seed=_optional_int(config.mock.get("seed")),
        )
    if config.adapter == "modbus":
        return ModbusBmsAdapter(config.battery_mappings, dict(config.hardware.get("modbus", {})))
    if config.adapter == "canbus":
        return CanBusBmsAdapter(config.battery_mappings, dict(config.hardware.get("canbus", {})))
    raise ValueError(f"Unsupported adapter: {config.adapter}")


def _optional_int(value: object) -> int | None:
    return None if value is None else int(value)

