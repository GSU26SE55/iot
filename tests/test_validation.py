from datetime import datetime, timezone
import unittest

from gateway.calibration import apply_calibrations
from gateway.models import CalibrationRule, SensorReading
from gateway.validation import ReadingValidationError, validate_reading


class ValidationTests(unittest.TestCase):
    def test_valid_reading_passes(self) -> None:
        validate_reading(_reading())

    def test_outlier_voltage_fails(self) -> None:
        with self.assertRaises(ReadingValidationError):
            validate_reading(_reading(voltage=1200))

    def test_calibration_applies_offset_and_scale(self) -> None:
        calibrated = apply_calibrations(
            _reading(voltage=10.0, temperature=30.0),
            [
                CalibrationRule(metric="voltage", scale_factor=1.1, offset_value=0.2),
                CalibrationRule(metric="temperature", scale_factor=1.0, offset_value=-1.5),
            ],
        )

        self.assertAlmostEqual(calibrated.voltage, 11.2)
        self.assertAlmostEqual(calibrated.temperature, 28.5)


def _reading(**changes: object) -> SensorReading:
    data = {
        "battery_asset_serial": "BAT-001",
        "battery_asset_id": "00000000-0000-0000-0000-000000000001",
        "time": datetime(2026, 5, 15, 10, 0, tzinfo=timezone.utc),
        "voltage": 12.6,
        "current": -5.2,
        "temperature": 35.4,
        "soc_percent": 78.5,
        "cycle_count": 120,
        "soh_percent": 94.2,
        "charging_state": 3,
        "sensor_source_code": "primary",
    }
    data.update(changes)
    return SensorReading(**data)


if __name__ == "__main__":
    unittest.main()

