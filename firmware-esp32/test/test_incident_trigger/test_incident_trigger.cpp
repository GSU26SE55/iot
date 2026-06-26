// ==================================================================
// Sprint 6 — S6-FW-01/02 (#63/#64): Unit test cho IncidentTrigger.
//
// Pure logic (edge + cooldown) dùng chung MQ-2 (smoke) + water leak. Test native
// không cần ESP32.
//
// Chạy: pio test -e native -f test_incident_trigger
// ==================================================================
#include <unity.h>

#include "sensor/incident_trigger.h"

using sensor::IncidentTrigger;

constexpr uint32_t kCooldown = 5000;  // 5s cho dễ đọc

// Lần đọc đầu đã active (vd nước có sẵn lúc boot) → coi là cạnh lên → fire.
void test_first_read_active_fires() {
  IncidentTrigger t(kCooldown);
  TEST_ASSERT_TRUE(t.update(true, 0));
  TEST_ASSERT_TRUE(t.hasFired());
}

// Cạnh lên inactive→active fire ĐÚNG 1 lần; giữ active không lặp.
void test_rising_edge_fires_once() {
  IncidentTrigger t(kCooldown);
  TEST_ASSERT_FALSE(t.update(false, 0));   // bình thường
  TEST_ASSERT_TRUE(t.update(true, 100));    // cạnh lên → fire
  TEST_ASSERT_FALSE(t.update(true, 200));   // vẫn active → không lặp
  TEST_ASSERT_FALSE(t.update(true, 300));
}

// Active giữ HIGH liên tục: KHÔNG re-fire kể cả khi đã quá cooldown (vì không có
// cạnh lên mới — chỉ rising edge mới fire).
void test_sustained_high_no_refire_past_cooldown() {
  IncidentTrigger t(kCooldown);
  TEST_ASSERT_TRUE(t.update(true, 100));
  TEST_ASSERT_FALSE(t.update(true, 100 + kCooldown + 1));
  TEST_ASSERT_FALSE(t.update(true, 100 + 2 * kCooldown));
}

// low→high→low→high TRONG cooldown → suppress (chống chattering).
void test_rearm_within_cooldown_suppressed() {
  IncidentTrigger t(kCooldown);
  TEST_ASSERT_TRUE(t.update(true, 100));    // fire (lastFire=100)
  TEST_ASSERT_FALSE(t.update(false, 200));  // xuống low
  TEST_ASSERT_FALSE(t.update(true, 300));   // lên lại trong cooldown → suppress
}

// low→high sau khi đã qua cooldown → re-fire cho phép.
void test_rearm_after_cooldown_fires() {
  IncidentTrigger t(kCooldown);
  TEST_ASSERT_TRUE(t.update(true, 100));               // fire
  TEST_ASSERT_FALSE(t.update(false, 200));             // low
  TEST_ASSERT_TRUE(t.update(true, 100 + kCooldown + 1)); // qua cooldown → fire lại
}

// isActive() phản ánh mẫu cuối cùng.
void test_is_active_tracks_last_sample() {
  IncidentTrigger t(kCooldown);
  t.update(false, 0);
  TEST_ASSERT_FALSE(t.isActive());
  t.update(true, 100);
  TEST_ASSERT_TRUE(t.isActive());
  t.update(false, 200);
  TEST_ASSERT_FALSE(t.isActive());
}

// Cooldown=0 → mỗi cạnh lên đều fire (không suppress).
void test_zero_cooldown_every_rising_edge_fires() {
  IncidentTrigger t(0);
  TEST_ASSERT_TRUE(t.update(true, 100));
  TEST_ASSERT_FALSE(t.update(false, 200));
  TEST_ASSERT_TRUE(t.update(true, 201));
  TEST_ASSERT_FALSE(t.update(false, 202));
  TEST_ASSERT_TRUE(t.update(true, 203));
}

void setUp(void)    {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_read_active_fires);
  RUN_TEST(test_rising_edge_fires_once);
  RUN_TEST(test_sustained_high_no_refire_past_cooldown);
  RUN_TEST(test_rearm_within_cooldown_suppressed);
  RUN_TEST(test_rearm_after_cooldown_fires);
  RUN_TEST(test_is_active_tracks_last_sample);
  RUN_TEST(test_zero_cooldown_every_rising_edge_fires);
  return UNITY_END();
}
