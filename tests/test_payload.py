from datetime import datetime, timezone
import unittest

from gateway.api.client import GatewayApiClient
from gateway.models import BatteryMapping, GatewayConfig, SensorReading


class PayloadTests(unittest.TestCase):
    def test_legacy_payload_uses_items(self) -> None:
        config = GatewayConfig(
            backend_base_url="http://localhost:4001",
            api_key="key",
            device_code="GW-001",
            ingest_mode="legacy",
            battery_mappings=[BatteryMapping("BAT-001", "00000000-0000-0000-0000-000000000001")],
        )
        client = GatewayApiClient(config)
        payload = client.build_sensor_batch_payload([_reading()])

        self.assertIn("items", payload)
        self.assertEqual(payload["items"][0]["batteryAssetId"], "00000000-0000-0000-0000-000000000001")

    def test_production_payload_uses_readings_and_serial(self) -> None:
        config = GatewayConfig(
            backend_base_url="http://localhost:4001",
            api_key="key",
            device_code="GW-001",
            ingest_mode="production",
            battery_mappings=[BatteryMapping("BAT-001")],
        )
        client = GatewayApiClient(config)
        payload = client.build_sensor_batch_payload([_reading(battery_asset_id=None)])

        self.assertIn("deviceTimestamp", payload)
        self.assertEqual(payload["readings"][0]["batteryAssetSerial"], "BAT-001")
        self.assertEqual(payload["readings"][0]["sensorSourceCode"], "primary")


def _reading(battery_asset_id: str | None = "00000000-0000-0000-0000-000000000001") -> SensorReading:
    return SensorReading(
        battery_asset_serial="BAT-001",
        battery_asset_id=battery_asset_id,
        time=datetime(2026, 5, 15, 10, 0, tzinfo=timezone.utc),
        voltage=12.6,
        current=-5.2,
        temperature=35.4,
        soc_percent=78.5,
        cycle_count=120,
        soh_percent=94.2,
        charging_state=3,
        sensor_source_code="primary",
    )


if __name__ == "__main__":
    unittest.main()

