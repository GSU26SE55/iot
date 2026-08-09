// ==================================================================
// IOT3-44/89 — luật quyết định xin lại credential MQTT.
//
// Hai thứ dễ làm sai nhất, và cả hai đều được chốt ở đây:
//   1. NGƯỠNG — đi sớm quá thì một trục trặc thoáng qua cũng khiến thiết bị vứt cờ provision.
//   2. HẠ NHIỆT quanh điểm QUAY VÒNG của millis() — viết `now > last + cooldown` thì tại
//      thời điểm quay vòng (~49,7 ngày) re-provision bị khoá cứng suốt 49 ngày kế tiếp,
//      một lỗi không ai bắt được bằng cách chạy thử.
// ==================================================================
#include <unity.h>

#include "core/reprovision_policy.h"

namespace {

constexpr uint32_t kCooldown = core::kReprovisionCooldownMs;   // 15 phút

void test_below_threshold_does_nothing() {
  for (uint32_t streak = 0; streak < 5; ++streak) {
    TEST_ASSERT_FALSE(core::shouldReprovisionOnAuthFailure(
        streak, 5, /*now=*/100000, /*last=*/0, /*ever=*/false));
  }
}

void test_first_time_ignores_cooldown() {
  // Chưa từng re-provision ⇒ đủ ngưỡng là đi ngay, kể cả khi đồng hồ vừa mới chạy.
  TEST_ASSERT_TRUE(core::shouldReprovisionOnAuthFailure(
      5, 5, /*now=*/0, /*last=*/0, /*ever=*/false));
  TEST_ASSERT_TRUE(core::shouldReprovisionOnAuthFailure(
      99, 5, /*now=*/1234, /*last=*/0, /*ever=*/false));
}

void test_cooldown_blocks_then_releases() {
  const uint32_t last = 1'000'000;
  TEST_ASSERT_FALSE(core::shouldReprovisionOnAuthFailure(
      5, 5, last, last, /*ever=*/true));
  TEST_ASSERT_FALSE(core::shouldReprovisionOnAuthFailure(
      5, 5, last + kCooldown - 1, last, /*ever=*/true));
  // Đúng biên là được đi.
  TEST_ASSERT_TRUE(core::shouldReprovisionOnAuthFailure(
      5, 5, last + kCooldown, last, /*ever=*/true));
  TEST_ASSERT_TRUE(core::shouldReprovisionOnAuthFailure(
      5, 5, last + kCooldown + 1, last, /*ever=*/true));
}

void test_cooldown_survives_millis_rollover() {
  // millis() sắp quay vòng: last ở cuối dải, now đã vòng về đầu.
  const uint32_t last = 0xFFFFFFFFu - 60'000u;        // còn 60 s là quay vòng
  const uint32_t justAfterWrap = 60'000u;             // tổng cách last đúng 120 s

  // 120 s < 15 phút ⇒ vẫn phải chặn.
  TEST_ASSERT_FALSE(core::shouldReprovisionOnAuthFailure(
      5, 5, justAfterWrap, last, /*ever=*/true));

  // Qua đủ 15 phút tính từ last (đã vòng qua 0) ⇒ cho đi.
  const uint32_t afterCooldown = last + kCooldown;    // tự quấn vòng theo uint32
  TEST_ASSERT_TRUE(core::shouldReprovisionOnAuthFailure(
      5, 5, afterCooldown, last, /*ever=*/true));
}

void test_zero_or_negative_threshold_never_fires() {
  // Ngưỡng vô nghĩa phải làm luật ĐỨNG YÊN, không phải bắn liên tục.
  TEST_ASSERT_FALSE(core::shouldReprovisionOnAuthFailure(
      100, 0, /*now=*/999999, /*last=*/0, /*ever=*/false));
  TEST_ASSERT_FALSE(core::shouldReprovisionOnAuthFailure(
      100, -1, /*now=*/999999, /*last=*/0, /*ever=*/false));
}

void test_cooldown_is_fifteen_minutes() {
  TEST_ASSERT_EQUAL_UINT32(15UL * 60UL * 1000UL, core::kReprovisionCooldownMs);
}

}  // namespace

void setUp()    {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_below_threshold_does_nothing);
  RUN_TEST(test_first_time_ignores_cooldown);
  RUN_TEST(test_cooldown_blocks_then_releases);
  RUN_TEST(test_cooldown_survives_millis_rollover);
  RUN_TEST(test_zero_or_negative_threshold_never_fires);
  RUN_TEST(test_cooldown_is_fifteen_minutes);
  return UNITY_END();
}
