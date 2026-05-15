from __future__ import annotations

from abc import ABC, abstractmethod
from collections.abc import Sequence

from gateway.models import BatteryMapping, SensorReading


class BmsAdapterError(RuntimeError):
    pass


class BmsAdapter(ABC):
    def __init__(self, mappings: Sequence[BatteryMapping]) -> None:
        self._mappings = list(mappings)

    @abstractmethod
    def read(self) -> list[SensorReading]:
        """Read one sample per configured battery mapping."""

    def close(self) -> None:
        """Release hardware resources if the adapter owns any."""

    @property
    def connected_sensor_count(self) -> int:
        return len(self._mappings)

