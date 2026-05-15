from __future__ import annotations

import math
import random
from datetime import timezone

from gateway.bms.base import BmsAdapter
from gateway.models import BatteryMapping, SensorReading, utc_now


class MockBmsAdapter(BmsAdapter):
    """Deterministic-ish BMS simulator for backend and mobile/web demo."""

    def __init__(
        self,
        mappings: list[BatteryMapping],
        *,
        scenario: str = "normal",
        seed: int | None = None,
    ) -> None:
        super().__init__(mappings)
        self._scenario = scenario
        self._random = random.Random(seed)
        self._tick = 0
        self._state = {
            mapping.battery_asset_serial: {
                "soc": self._random.uniform(65, 90),
                "soh": self._random.uniform(88, 98),
                "cycle": self._random.randint(80, 220),
            }
            for mapping in self._mappings
        }

    def read(self) -> list[SensorReading]:
        self._tick += 1
        readings: list[SensorReading] = []
        now = utc_now().astimezone(timezone.utc)

        for index, mapping in enumerate(self._mappings):
            state = self._state[mapping.battery_asset_serial]
            phase = (self._tick + index * 3) / 12
            charging = math.sin(phase) >= 0
            base_current = self._random.uniform(2.0, 8.0)
            current = base_current if charging else -base_current
            state["soc"] = _clamp(state["soc"] + (0.18 if charging else -0.22), 5, 98)

            voltage = 12.4 + (state["soc"] - 50) * 0.012 + self._random.uniform(-0.05, 0.05)
            temperature = 29.0 + math.sin(phase) * 2.5 + self._random.uniform(-0.7, 0.7)
            soh = state["soh"]
            bms_error_code = None

            if self._scenario == "overheat":
                temperature = 48.0 + self._random.uniform(0.5, 4.0)
                bms_error_code = "MOCK_OVERHEAT"
            elif self._scenario == "low_soc":
                state["soc"] = _clamp(12.0 - self._tick * 0.25, 3, 15)
                current = -abs(current)
            elif self._scenario == "soh_degradation":
                state["soh"] = _clamp(state["soh"] - 0.15, 65, 100)
                soh = state["soh"]
            elif self._scenario == "mixed" and self._tick % 10 == 0:
                temperature = 50.0
                current = -18.0
                bms_error_code = "MOCK_MIXED_ANOMALY"

            charging_state = 2 if current > 0 else 3
            readings.append(
                SensorReading(
                    battery_asset_serial=mapping.battery_asset_serial,
                    battery_asset_id=mapping.battery_asset_id,
                    time=now,
                    voltage=voltage,
                    current=current,
                    temperature=temperature,
                    soc_percent=state["soc"],
                    cycle_count=int(state["cycle"]),
                    soh_percent=soh,
                    charging_state=charging_state,
                    bms_error_code=bms_error_code,
                    sensor_source_code=mapping.sensor_source_code,
                )
            )

        return readings


def _clamp(value: float, minimum: float, maximum: float) -> float:
    return max(minimum, min(maximum, value))

