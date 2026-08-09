"""GH-739 — test HỢP ĐỒNG: payload simulator ↔ mock backend ↔ firmware thật.

Lỗi gốc: simulator gửi PascalCase (``{"Items": [{"Time": ...}]}``) trong khi firmware ESP32
(``core::buildProductionBatchPayload``) gửi camelCase (``{"items": [{"time": ...}]}``).
Mock backend kiểm nghiêm ngặt theo đặc tả nên trả 400 "items required"; backend ASP.NET thật
bind KHÔNG phân biệt hoa thường nên nuốt luôn — che mất chỗ lệch suốt thời gian dài.

Hệ quả sâu hơn con số 400: simulator sinh ra JSON KHÁC với thiết bị thật, nên nó không còn
dùng để bắt lỗi hợp đồng được nữa — đúng cái việc nó sinh ra để làm.

Test này gọi thẳng ``validate_batch()`` của mock (không cần dựng HTTP server) và đối chiếu
tên trường với danh sách camelCase mà firmware phát ra.
"""

import os
import sys
import unittest
from dataclasses import asdict

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
sys.path.insert(0, os.path.join(_HERE, "..", "mock-backend"))

from esp32_simulator import SensorReading  # noqa: E402
from mock_backend import validate_batch  # noqa: E402


def _batch(n: int = 3) -> dict:
    readings = [SensorReading.mock_for(f"BAT-{i:03d}", 1000.0 + i) for i in range(n)]
    return {"items": [asdict(r) for r in readings]}


class SimulatorMockContractTests(unittest.TestCase):
    def test_simulator_payload_passes_mock_validation(self):
        """Đây là ca của GH-739 — trên code cũ, mock trả lỗi 'items required'."""
        ok, errs = validate_batch(_batch())
        self.assertTrue(ok, f"mock backend từ chối payload của simulator: {errs}")
        self.assertEqual(errs, [])

    def test_root_key_is_lowercase_items(self):
        body = _batch()
        self.assertIn("items", body)
        self.assertNotIn("Items", body, "PascalCase 'Items' chính là lỗi GH-739")

    def test_every_field_matches_firmware_wire_names(self):
        """Tên trường phải TRÙNG chuỗi JSON firmware thật phát ra.

        Danh sách lấy từ ``firmware-esp32/src/core/payload.cpp``. Simulator được phép gửi
        TẬP CON (không phải cảm biến nào cũng có), nhưng KHÔNG được phép có trường lạ —
        trường lạ nghĩa là nó đang mô phỏng một thiết bị không tồn tại.
        """
        firmware_wire_fields = {
            "batteryAssetId",
            "batteryAssetSerial",
            "current",
            "cycleCount",
            "deviceTimestamp",
            "sensorSourceCode",
            "socPercent",
            "sourceType",
            "temperature",
            "time",
            "voltage",
        }
        item = _batch(1)["items"][0]
        unknown = set(item) - firmware_wire_fields
        self.assertEqual(
            unknown, set(),
            f"simulator gửi trường firmware không hề gửi: {sorted(unknown)}",
        )

    def test_no_pascal_case_keys_anywhere_in_payload(self):
        # Quét toàn bộ khoá: bất kỳ khoá nào bắt đầu bằng chữ hoa là tái hiện GH-739.
        body = _batch(2)
        offenders = [k for k in body if k[:1].isupper()]
        for item in body["items"]:
            offenders += [k for k in item if k[:1].isupper()]
        self.assertEqual(offenders, [], f"còn khoá PascalCase: {offenders}")

    def test_mock_still_rejects_the_old_pascal_case_payload(self):
        """Chốt ngược: bảo đảm test trên KHÔNG xanh giả.

        Nếu mock bỗng chấp nhận mọi thứ thì test đầu tiên vô nghĩa. Payload PascalCase cũ
        PHẢI vẫn bị từ chối.
        """
        legacy = {"Items": [{"Time": "2026-08-04T00:00:00Z", "BatteryAssetSerial": "BAT-001"}]}
        ok, errs = validate_batch(legacy)
        self.assertFalse(ok, "mock phải từ chối payload PascalCase cũ")
        self.assertTrue(
            any(e.get("field") == "items" for e in errs),
            f"phải báo lỗi ở trường 'items', nhận được: {errs}",
        )

    def test_timestamps_match_firmware_format_not_python_isoformat(self):
        """Lỗi thứ hai lộ ra khi viết test này (issue #739 không nêu).

        Firmware dùng ``strftime("%Y-%m-%dT%H:%M:%SZ")`` → ``...42Z``. Python
        ``datetime.isoformat()`` cho ``...42.789012+00:00`` — mock từ chối vì regex đòi hậu
        tố ``Z``. Cùng gốc với GH-739: simulator sinh JSON khác thiết bị thật.
        """
        import re

        firmware_shape = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
        item = _batch(1)["items"][0]
        for field in ("time", "deviceTimestamp"):
            with self.subTest(field=field):
                self.assertRegex(item[field], firmware_shape)
                self.assertNotIn("+00:00", item[field], "dạng offset là lỗi đã sửa")

    def test_batch_respects_mock_limits(self):
        ok, errs = validate_batch({"items": []})
        self.assertFalse(ok, "batch rỗng phải bị từ chối")
        self.assertTrue(any(e.get("field") == "items" for e in errs))


if __name__ == "__main__":
    unittest.main()
