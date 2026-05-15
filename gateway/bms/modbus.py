from __future__ import annotations

from datetime import timezone
from typing import Any

from gateway.bms.base import BmsAdapter, BmsAdapterError
from gateway.models import BatteryMapping, SensorReading, utc_now


class ModbusBmsAdapter(BmsAdapter):
    """Generic RS485/Modbus adapter driven by a register map from config.

    This adapter intentionally does not hard-code one BMS vendor. Real BMS models
    must provide a register map in config, for example:

    {
      "hardware": {
        "modbus": {
          "port": "/dev/ttyUSB0",
          "baudrate": 9600,
          "unitId": 1,
          "registers": {
            "voltage": {"address": 0, "scale": 0.01},
            "current": {"address": 1, "scale": 0.01, "signed": true},
            "temperature": {"address": 2, "scale": 0.1, "offset": -40},
            "socPercent": {"address": 3, "scale": 1}
          }
        }
      }
    }
    """

    def __init__(self, mappings: list[BatteryMapping], options: dict[str, Any]) -> None:
        super().__init__(mappings)
        if len(mappings) != 1:
            raise BmsAdapterError("Generic Modbus adapter currently supports one battery mapping per gateway.")

        try:
            from pymodbus.client import ModbusSerialClient  # type: ignore
        except ImportError as exc:
            raise BmsAdapterError("pymodbus is required for adapter='modbus'. Install requirements-hardware.txt.") from exc

        self._mapping = mappings[0]
        self._unit_id = int(options.get("unitId", 1))
        self._registers = dict(options.get("registers", {}))
        if not self._registers:
            raise BmsAdapterError("hardware.modbus.registers is required.")

        self._client = ModbusSerialClient(
            port=str(options.get("port", "/dev/ttyUSB0")),
            baudrate=int(options.get("baudrate", 9600)),
            bytesize=int(options.get("bytesize", 8)),
            parity=str(options.get("parity", "N")),
            stopbits=int(options.get("stopbits", 1)),
            timeout=float(options.get("timeoutSec", 2.0)),
        )

    def read(self) -> list[SensorReading]:
        if not self._client.connect():
            raise BmsAdapterError("Could not connect to Modbus BMS.")

        values = {metric: self._read_metric(spec) for metric, spec in self._registers.items()}
        required = ["voltage", "current", "temperature", "socPercent"]
        missing = [metric for metric in required if metric not in values]
        if missing:
            raise BmsAdapterError(f"Modbus register map missing required metrics: {', '.join(missing)}")

        reading = SensorReading(
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
            bms_error_code=_optional_text(values.get("bmsErrorCode")),
            sensor_source_code=self._mapping.sensor_source_code,
        )
        return [reading]

    def close(self) -> None:
        self._client.close()

    def _read_metric(self, spec: dict[str, Any]) -> float | int | str:
        address = int(spec["address"])
        count = int(spec.get("count", 1))
        response = self._client.read_holding_registers(address=address, count=count, slave=self._unit_id)
        if response.isError():
            raise BmsAdapterError(f"Modbus read failed at address {address}")

        raw = int(response.registers[0])
        if bool(spec.get("signed", False)) and raw >= 32768:
            raw -= 65536
        value = raw * float(spec.get("scale", 1.0)) + float(spec.get("offset", 0.0))
        return value


def _optional_float(value: Any) -> float | None:
    return None if value is None else float(value)


def _optional_int(value: Any) -> int | None:
    return None if value is None else int(value)


def _optional_text(value: Any) -> str | None:
    return None if value is None else str(value)

