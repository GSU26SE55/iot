import json
import tempfile
import unittest
from pathlib import Path

from gateway.config import ConfigError, load_config


class ConfigTests(unittest.TestCase):
    def test_load_valid_legacy_config(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.json"
            path.write_text(
                json.dumps(
                    {
                        "backendBaseUrl": "http://localhost:4001",
                        "apiKey": "key",
                        "deviceCode": "GW-001",
                        "ingestMode": "legacy",
                        "batteryMappings": [
                            {
                                "batteryAssetSerial": "BAT-001",
                                "batteryAssetId": "00000000-0000-0000-0000-000000000001",
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            config = load_config(path)

        self.assertEqual(config.device_code, "GW-001")
        self.assertEqual(config.ingest_mode, "legacy")
        self.assertEqual(config.battery_mappings[0].battery_asset_serial, "BAT-001")

    def test_legacy_requires_battery_asset_id(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.json"
            path.write_text(
                json.dumps(
                    {
                        "backendBaseUrl": "http://localhost:4001",
                        "apiKey": "key",
                        "deviceCode": "GW-001",
                        "ingestMode": "legacy",
                        "batteryMappings": [{"batteryAssetSerial": "BAT-001"}],
                    }
                ),
                encoding="utf-8",
            )

            with self.assertRaises(ConfigError):
                load_config(path)


if __name__ == "__main__":
    unittest.main()

