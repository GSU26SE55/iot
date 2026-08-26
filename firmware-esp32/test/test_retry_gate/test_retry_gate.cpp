// GH-741 — kiểm cổng "đã tới lúc thử gửi lại chưa".
//
// Lỗi gốc: cảm biến an toàn giữ pending rồi gọi lại reporter ở MỖI tick (MQ-2 1s, rò nước
// 0,5s). Backend trả 403 vì API key thiếu scope ⇒ ~180 request/phút, vô hạn, không tự khỏi.
//
// Cổng này quyết định "được thử chưa". Phép so mốc thời gian phải chịu được tràn `millis()`
// sau 49,7 ngày — nếu viết `now >= nextAllowed` thì sau khi tràn, cổng KHOÁ CỨNG suốt gần
// 50 ngày, và một sự cố khí/rò nước sẽ không bao giờ được báo. Đó là lý do có test tràn số
// ở đây dù logic trông tầm thường.

#include <unity.h>

#include <cstdint>

#include "core/retry_gate.h"

namespace {
constexpr uint32_t kU32Max = 0xFFFFFFFFU;
}

void test_not_pending_never_attempts() {
  TEST_ASSERT_FALSE(core::shouldAttemptReport(false, 10'000, 0));
  TEST_ASSERT_FALSE(core::shouldAttemptReport(false, 0, 0));
}

void test_attempts_when_deadline_reached() {
  TEST_ASSERT_TRUE(core::shouldAttemptReport(true, 5'000, 5'000));
  TEST_ASSERT_TRUE(core::shouldAttemptReport(true, 5'001, 5'000));
}

void test_waits_before_deadline() {
  TEST_ASSERT_FALSE(core::shouldAttemptReport(true, 4'999, 5'000));
}

void test_fresh_pending_with_zero_deadline_attempts_immediately() {
  // Sự cố MỚI đặt nextAllowed = now ⇒ phải báo ngay, không chờ backoff của lần trước.
  TEST_ASSERT_TRUE(core::shouldAttemptReport(true, 123'456, 123'456));
  TEST_ASSERT_TRUE(core::shouldAttemptReport(true, 123'456, 0));
}

void test_survives_millis_rollover_deadline_before_wrap() {
  // Hạn đặt ngay TRƯỚC mốc tràn, hiện tại đã qua mốc ⇒ PHẢI cho thử.
  // Viết `now >= nextAllowed` thì đây trả false và cổng khoá ~49,7 ngày.
  const uint32_t nextAllowed = kU32Max - 100;
  const uint32_t now = 50;   // đã vòng qua 0
  TEST_ASSERT_TRUE(core::shouldAttemptReport(true, now, nextAllowed));
}

void test_survives_rollover_when_still_waiting() {
  // Cùng tình huống tràn nhưng CHƯA tới hạn ⇒ vẫn phải đợi.
  const uint32_t now = kU32Max - 100;
  const uint32_t nextAllowed = 50;   // hạn nằm sau mốc tràn
  TEST_ASSERT_FALSE(core::shouldAttemptReport(true, now, nextAllowed));
}

void test_long_backoff_across_wrap_is_respected_then_released() {
  // 5 phút (backoff tối đa) vắt qua mốc tràn.
  const uint32_t fiveMinMs = 5U * 60U * 1000U;
  const uint32_t start = 0U - (fiveMinMs / 2);        // trước mốc tràn
  const uint32_t nextAllowed = start + fiveMinMs;     // vòng qua 0

  TEST_ASSERT_FALSE(core::shouldAttemptReport(true, start + 1, nextAllowed));
  TEST_ASSERT_FALSE(core::shouldAttemptReport(true, nextAllowed - 1, nextAllowed));
  TEST_ASSERT_TRUE(core::shouldAttemptReport(true, nextAllowed, nextAllowed));
  TEST_ASSERT_TRUE(core::shouldAttemptReport(true, nextAllowed + 1, nextAllowed));
}

void test_pending_flag_dominates() {
  // Hết hạn nhưng không còn pending (đã gửi xong hoặc đã bỏ vì lỗi vĩnh viễn) ⇒ không thử.
  TEST_ASSERT_FALSE(core::shouldAttemptReport(false, 999'999, 0));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_not_pending_never_attempts);
  RUN_TEST(test_attempts_when_deadline_reached);
  RUN_TEST(test_waits_before_deadline);
  RUN_TEST(test_fresh_pending_with_zero_deadline_attempts_immediately);
  RUN_TEST(test_survives_millis_rollover_deadline_before_wrap);
  RUN_TEST(test_survives_rollover_when_still_waiting);
  RUN_TEST(test_long_backoff_across_wrap_is_respected_then_released);
  RUN_TEST(test_pending_flag_dominates);
  return UNITY_END();
}
