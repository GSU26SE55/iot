from __future__ import annotations

from collections.abc import Iterable

from gateway.models import CalibrationRule, SensorReading


_METRIC_FIELD = {
    "voltage": "voltage",
    "current": "current",
    "temperature": "temperature",
    "soc": "soc_percent",
    "socpercent": "soc_percent",
    "soh": "soh_percent",
    "sohpercent": "soh_percent",
}


def apply_calibrations(reading: SensorReading, rules: Iterable[CalibrationRule]) -> SensorReading:
    changes: dict[str, float] = {}
    for rule in rules:
        field = _METRIC_FIELD.get(rule.metric.strip().lower())
        if not field:
            continue
        value = getattr(reading, field)
        if value is None:
            continue
        changes[field] = float(value) * rule.scale_factor + rule.offset_value
    if not changes:
        return reading
    return reading.with_values(**changes)

