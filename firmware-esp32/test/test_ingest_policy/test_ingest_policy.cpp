// GH-737 — kiểm quyết định của mỗi chu kỳ lấy mẫu.
//
// Lỗi gốc: mất Wi-Fi thì main loop chỉ tăng bộ đếm lỗi — không đọc BMS, không xếp hàng.
// Toàn bộ telemetry trong khoảng mất mạng biến mất, trái cam kết "không mất dữ liệu 5 phút".
// Hàng đợi chỉ được nạp từ nhánh lỗi tạm thời của ingestOnce(), mà nhánh đó đòi phải ONLINE
// trước đã — nên offline không bao giờ tới được.
//
// Bốn tổ hợp (wifi × clock) dưới đây phủ hết bảng chân trị. Trên code cũ, ca
// "mất wifi + có giờ" cho ra "bỏ qua" thay vì "xếp hàng" ⇒ test này ĐỎ.

#include <unity.h>

#include "core/ingest_policy.h"

void test_online_with_clock_posts() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(core::IngestAction::PostOnline),
                        static_cast<int>(core::ingestAction(true, true)));
}

void test_offline_with_clock_queues_instead_of_dropping() {
  // ĐÂY là ca của GH-737. Đồng hồ ESP32 vẫn chạy sau khi mất Wi-Fi (NTP chỉ ĐẶT giờ một
  // lần), nên bản ghi xếp hàng vẫn mang mốc thời gian THẬT chứ không phải giờ lúc đẩy bù.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(core::IngestAction::QueueOffline),
                        static_cast<int>(core::ingestAction(false, true)));
}

void test_no_clock_skips_even_when_online() {
  // Có mạng nhưng chưa từng sync NTP ⇒ không có mốc thời gian hợp lệ. Ghi bừa một mốc sai
  // còn tệ hơn bỏ qua: backend sẽ xếp bản ghi vào nhầm thời điểm.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(core::IngestAction::SkipNoClock),
                        static_cast<int>(core::ingestAction(true, false)));
}

void test_no_clock_no_wifi_skips() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(core::IngestAction::SkipNoClock),
                        static_cast<int>(core::ingestAction(false, false)));
}

void test_clock_is_the_deciding_factor_not_wifi() {
  // Ghim ý nghĩa: thiếu ĐỒNG HỒ mới là lý do bỏ qua; thiếu MẠNG chỉ đổi đích đến
  // (gửi thẳng → xếp hàng). Nếu ai đó đảo lại thứ tự hai điều kiện, test này đỏ.
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(core::IngestAction::SkipNoClock),
                        static_cast<int>(core::ingestAction(false, true)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(core::IngestAction::SkipNoClock),
                        static_cast<int>(core::ingestAction(true, false)));
}

void test_no_combination_is_left_unhandled() {
  // Mọi tổ hợp phải cho ra một trong ba hành động hợp lệ — không rơi vào giá trị lạ.
  const bool bools[] = {false, true};
  for (bool w : bools) {
    for (bool t : bools) {
      const int a = static_cast<int>(core::ingestAction(w, t));
      TEST_ASSERT_TRUE(a == static_cast<int>(core::IngestAction::PostOnline) ||
                       a == static_cast<int>(core::IngestAction::QueueOffline) ||
                       a == static_cast<int>(core::IngestAction::SkipNoClock));
    }
  }
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_online_with_clock_posts);
  RUN_TEST(test_offline_with_clock_queues_instead_of_dropping);
  RUN_TEST(test_no_clock_skips_even_when_online);
  RUN_TEST(test_no_clock_no_wifi_skips);
  RUN_TEST(test_clock_is_the_deciding_factor_not_wifi);
  RUN_TEST(test_no_combination_is_left_unhandled);
  return UNITY_END();
}
