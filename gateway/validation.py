from __future__ import annotations

from datetime import datetime, timezone

from gateway.models import SensorReading


class ReadingValidationError(ValueError):
    pass


def validate_reading(reading: SensorReading, *, max_future_skew_sec: int = 300) -> None:
    now = datetime.now(timezone.utc)
    reading_time = reading.time
    if reading_time.tzinfo is None:
        reading_time = reading_time.replace(tzinfo=timezone.utc)

    if reading_time > now.replace(microsecond=0) and (reading_time - now).total_seconds() > max_future_skew_sec:
        raise ReadingValidationError("reading time is too far in the future")
    if reading.voltage < 0:
        raise ReadingValidationError("voltage must not be negative")
    if reading.voltage > 1000:
        raise ReadingValidationError("voltage is above hard safety limit")
    if reading.temperature < -50 or reading.temperature > 150:
        raise ReadingValidationError("temperature is outside hard safety limit")
    if reading.soc_percent < 0 or reading.soc_percent > 100:
        raise ReadingValidationError("socPercent must be in range 0-100")
    if reading.soh_percent is not None and (reading.soh_percent < 0 or reading.soh_percent > 100):
        raise ReadingValidationError("sohPercent must be in range 0-100")
    if reading.cycle_count is not None and reading.cycle_count < 0:
        raise ReadingValidationError("cycleCount must not be negative")
    if reading.charging_state is not None and reading.charging_state not in {1, 2, 3, 4, 5}:
        raise ReadingValidationError("chargingState must be one of 1..5")
    if reading.bms_error_code is not None and len(reading.bms_error_code) > 64:
        raise ReadingValidationError("bmsErrorCode must be <= 64 chars")
    if len(reading.sensor_source_code) > 20:
        raise ReadingValidationError("sensorSourceCode must be <= 20 chars")

