"""GH-738 — kiểm phân loại HTTP response của simulator.

Lỗi gốc: simulator chỉ coi ``status_code == 200`` là thành công, trong khi
``POST /api/sensor-readings/batch`` trả **201 Created** cho lần ghi mới (chỉ trả 200 cho ca
trùng idempotent). Hệ quả: MỌI lần ghi mới bị log FAIL rồi đẩy vào hàng đợi; mà phần flush
chỉ chạy sau một lần gửi live thành công — nên hàng đợi không bao giờ vơi.

Dùng ``unittest`` của thư viện chuẩn, KHÔNG thêm phụ thuộc: repo này chưa có pytest, và bắt
CI cài thêm gói chỉ để chạy vài test là cái giá không đáng.

Chạy:  python3 -m unittest discover -s tools/simulator -p 'test_*.py'
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from esp32_simulator import Outcome, classify_response  # noqa: E402


class ClassifyResponseTests(unittest.TestCase):
    def test_201_created_is_success(self):
        """Ca của GH-738: đây chính là status mà backend trả cho lần ghi mới."""
        self.assertIs(classify_response(201), Outcome.SUCCESS)

    def test_200_ok_is_success(self):
        """Backend trả 200 cho ca trùng idempotent — cũng là thành công."""
        self.assertIs(classify_response(200), Outcome.SUCCESS)

    def test_every_2xx_is_success(self):
        # Chấp nhận cả dải 2xx thay vì liệt kê từng mã: hợp đồng có thể thêm 202/204 sau này
        # mà không ai nhớ quay lại sửa simulator.
        for code in range(200, 300):
            with self.subTest(code=code):
                self.assertIs(classify_response(code), Outcome.SUCCESS)

    def test_4xx_is_permanent(self):
        for code in (400, 401, 403, 404, 409, 415, 422):
            with self.subTest(code=code):
                self.assertIs(classify_response(code), Outcome.PERMANENT)

    def test_retryable_4xx_is_transient(self):
        """408/425/429 là 4xx nhưng thử lại CÓ tác dụng — không được bỏ."""
        for code in (408, 425, 429):
            with self.subTest(code=code):
                self.assertIs(classify_response(code), Outcome.TRANSIENT)

    def test_5xx_is_transient(self):
        for code in (500, 502, 503, 504):
            with self.subTest(code=code):
                self.assertIs(classify_response(code), Outcome.TRANSIENT)

    def test_unexpected_codes_default_to_transient(self):
        """Không rõ thì giữ lại chứ không vứt — mất dữ liệu tệ hơn gửi lại thừa."""
        for code in (0, 100, 302, 599, 999):
            with self.subTest(code=code):
                self.assertIsNot(classify_response(code), Outcome.SUCCESS)
        self.assertIs(classify_response(599), Outcome.TRANSIENT)

    def test_permanent_and_transient_are_distinct(self):
        # Ghim ý nghĩa: 4xx thường KHÔNG được coi là transient, nếu không một batch dữ liệu
        # sai nằm đầu hàng đợi sẽ chặn vĩnh viễn mọi bản ghi phía sau (lớp lỗi starvation).
        self.assertIsNot(classify_response(400), classify_response(500))


class RegressionGuardTests(unittest.TestCase):
    def test_source_no_longer_compares_status_to_200_directly(self):
        """Chặn việc quay lại thói quen cũ.

        So sánh thẳng ``status_code == 200`` ở bất kỳ đâu là tái hiện đúng GH-738. Mọi chỗ
        phải đi qua ``classify_response``.
        """
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "esp32_simulator.py")
        with open(path, encoding="utf-8") as fh:
            lines = fh.readlines()

        offenders = [
            (i + 1, ln.strip())
            for i, ln in enumerate(lines)
            if "status_code == 200" in ln and not ln.strip().startswith("#")
        ]
        self.assertEqual(
            offenders, [],
            "còn so sánh status_code == 200 trực tiếp — phải dùng classify_response()",
        )


if __name__ == "__main__":
    unittest.main()
