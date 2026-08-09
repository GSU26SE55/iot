// GH-736 — kiểm phép tính "sự cố xảy ra cách đây bao lâu".
//
// Bối cảnh: MQ-2 và cảm biến rò nước nay lấy mẫu CẢ KHI mất mạng. Sự cố có thể được phát
// hiện lúc offline nhưng chỉ gửi được hàng giờ sau. Nếu lúc gửi mới lấy giờ hiện tại thì
// backend ghi sai thời điểm sự cố — hỏng cả điều tra lẫn việc đối chiếu với telemetry.
//
// Phép trừ này phải đúng cả khi `millis()` TRÀN SỐ sau ~49,7 ngày. Thiết bị nằm ngoài hiện
// trường và chạy liên tục nhiều tháng, nên tràn số là chuyện chắc chắn xảy ra chứ không
// phải trường hợp lý thuyết.

#include <unity.h>

#include <cstdint>

#include "core/elapsed.h"

namespace {
constexpr uint32_t kU32Max = 0xFFFFFFFFU;
}

void test_zero_when_same_instant() {
  TEST_ASSERT_EQUAL_UINT32(0, core::elapsedMs(1000, 1000));
  TEST_ASSERT_EQUAL_UINT32(0, core::elapsedSeconds(1000, 1000));
}

void test_normal_forward_progress() {
  TEST_ASSERT_EQUAL_UINT32(5000, core::elapsedMs(1000, 6000));
  TEST_ASSERT_EQUAL_UINT32(5, core::elapsedSeconds(1000, 6000));
}

void test_seconds_round_down() {
  // 1999ms = 1 giây (làm tròn xuống) — không được làm tròn lên thành 2.
  TEST_ASSERT_EQUAL_UINT32(1, core::elapsedSeconds(0, 1999));
  TEST_ASSERT_EQUAL_UINT32(0, core::elapsedSeconds(0, 999));
}

void test_survives_millis_rollover() {
  // Đây là ca cốt lõi: bắt đầu ngay TRƯỚC khi tràn, kết thúc ngay SAU.
  // Phép trừ không dấu cho kết quả đúng; nếu ai đó đổi sang kiểu có dấu hoặc so sánh
  // `now >= start` thì test này đỏ.
  const uint32_t start = kU32Max - 4;   // còn 5ms nữa thì tràn
  const uint32_t now   = 10;            // đã qua mốc tràn 11ms
  TEST_ASSERT_EQUAL_UINT32(15, core::elapsedMs(start, now));
}

void test_rollover_with_long_outage() {
  // Mất mạng 2 giờ vắt qua mốc tràn: một nửa trước, một nửa sau.
  //
  // Lấy mốc bắt đầu bằng `0 - x` (trừ vòng từ 0) chứ KHÔNG phải `kU32Max - x`:
  // kU32Max là 2^32-1, nên `kU32Max - x` nằm trước mốc tràn đúng x+1 tick, và kết quả
  // lệch 1ms. Sai lệch 1ms thì vô hại, nhưng viết `0 - x` diễn đạt đúng ý "lùi x mili-giây
  // trước khi tràn" và không cần ai phải đi giải thích con số lẻ.
  const uint32_t twoHoursMs = 2U * 60U * 60U * 1000U;
  const uint32_t half  = twoHoursMs / 2;
  const uint32_t start = 0U - half;   // = 2^32 - half
  const uint32_t now   = half;

  TEST_ASSERT_EQUAL_UINT32(twoHoursMs, core::elapsedMs(start, now));
  TEST_ASSERT_EQUAL_UINT32(7200, core::elapsedSeconds(start, now));
}

void test_rollover_boundary_is_exact_to_the_millisecond() {
  // Ghim luôn cái lệch 1 tick nói trên, để lần sau ai đọc test cũng thấy nó là CÓ CHỦ Ý
  // chứ không phải bug: từ kU32Max (giá trị lớn nhất) tới 0 là đúng 1 mili-giây.
  TEST_ASSERT_EQUAL_UINT32(1, core::elapsedMs(kU32Max, 0));
  TEST_ASSERT_EQUAL_UINT32(2, core::elapsedMs(kU32Max, 1));
}

void test_full_period_is_zero_not_negative() {
  // Đúng một chu kỳ tràn: kết quả 0, KHÔNG được ra số âm/khổng lồ.
  TEST_ASSERT_EQUAL_UINT32(0, core::elapsedMs(123456, 123456));
}

void test_long_offline_hours_are_representable() {
  // 12 giờ offline — vẫn phải ra đúng số giây, không tràn kiểu trả về.
  const uint32_t twelveHoursMs = 12U * 60U * 60U * 1000U;
  TEST_ASSERT_EQUAL_UINT32(43200, core::elapsedSeconds(0, twelveHoursMs));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_zero_when_same_instant);
  RUN_TEST(test_normal_forward_progress);
  RUN_TEST(test_seconds_round_down);
  RUN_TEST(test_survives_millis_rollover);
  RUN_TEST(test_rollover_with_long_outage);
  RUN_TEST(test_rollover_boundary_is_exact_to_the_millisecond);
  RUN_TEST(test_full_period_is_zero_not_negative);
  RUN_TEST(test_long_offline_hours_are_representable);
  return UNITY_END();
}
