// ==================================================================
// Sprint 3 — S3-FW-05: Unit test cho LED palette (state → RGB color)
//
// Pure logic — không phụ thuộc Arduino/neopixelWrite.
// Spec S3-FW-05: xanh = online, vàng = queue có data, đỏ = mất mạng.
// + Provisioning (Sprint 2 bonus) = tím, Off = đen.
//
// Chạy: pio test -e native -f test_led_palette
// ==================================================================
#include <unity.h>

#include "ui/led_palette.h"

void test_off_is_black() {
  auto c = ui::paletteForState(ui::LedState::Off);
  TEST_ASSERT_EQUAL(0, c.r);
  TEST_ASSERT_EQUAL(0, c.g);
  TEST_ASSERT_EQUAL(0, c.b);
}

void test_online_is_green() {
  auto c = ui::paletteForState(ui::LedState::Online);
  TEST_ASSERT_EQUAL(0, c.r);
  TEST_ASSERT_GREATER_THAN(0, c.g);     // green channel > 0
  TEST_ASSERT_EQUAL(0, c.b);
}

void test_queued_is_yellow() {
  // Yellow = red + green (no blue)
  auto c = ui::paletteForState(ui::LedState::Queued);
  TEST_ASSERT_GREATER_THAN(0, c.r);
  TEST_ASSERT_GREATER_THAN(0, c.g);
  TEST_ASSERT_EQUAL(0, c.b);
}

void test_offline_is_red() {
  auto c = ui::paletteForState(ui::LedState::Offline);
  TEST_ASSERT_GREATER_THAN(0, c.r);
  TEST_ASSERT_EQUAL(0, c.g);
  TEST_ASSERT_EQUAL(0, c.b);
}

void test_provisioning_is_purple() {
  // Purple = red + blue (no green)
  auto c = ui::paletteForState(ui::LedState::Provisioning);
  TEST_ASSERT_GREATER_THAN(0, c.r);
  TEST_ASSERT_EQUAL(0, c.g);
  TEST_ASSERT_GREATER_THAN(0, c.b);
}

void test_low_brightness_safe() {
  // Tất cả channel ≤ 32/255 — tránh chói mắt + tiết kiệm pin
  ui::LedState states[] = {
    ui::LedState::Off, ui::LedState::Online, ui::LedState::Queued,
    ui::LedState::Offline, ui::LedState::Provisioning,
  };
  for (auto s : states) {
    auto c = ui::paletteForState(s);
    TEST_ASSERT_LESS_OR_EQUAL(32, c.r);
    TEST_ASSERT_LESS_OR_EQUAL(32, c.g);
    TEST_ASSERT_LESS_OR_EQUAL(32, c.b);
  }
}

void test_all_states_distinguishable() {
  // 4 state có RGB color khác nhau (Off có thể giống Online nếu cả 2 = 0,0,0,
  // nhưng spec Off = 0,0,0 nên đảm bảo Online != Off)
  auto off    = ui::paletteForState(ui::LedState::Off);
  auto online = ui::paletteForState(ui::LedState::Online);
  auto queued = ui::paletteForState(ui::LedState::Queued);
  auto offl   = ui::paletteForState(ui::LedState::Offline);
  auto prov   = ui::paletteForState(ui::LedState::Provisioning);

  // Helper lambda (KHÔNG dùng macro vì param `b` đụng struct field `b` →
  // preprocessor substitute nhầm thành `online.online`).
  auto different = [](const ui::RgbColor& x, const ui::RgbColor& y) {
    return x.r != y.r || x.g != y.g || x.b != y.b;
  };
  TEST_ASSERT_TRUE(different(off,    online));
  TEST_ASSERT_TRUE(different(off,    queued));
  TEST_ASSERT_TRUE(different(off,    offl));
  TEST_ASSERT_TRUE(different(off,    prov));
  TEST_ASSERT_TRUE(different(online, queued));
  TEST_ASSERT_TRUE(different(online, offl));
  TEST_ASSERT_TRUE(different(online, prov));
  TEST_ASSERT_TRUE(different(queued, offl));
  TEST_ASSERT_TRUE(different(queued, prov));
  TEST_ASSERT_TRUE(different(offl,   prov));
}

void setUp(void)    {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_off_is_black);
  RUN_TEST(test_online_is_green);
  RUN_TEST(test_queued_is_yellow);
  RUN_TEST(test_offline_is_red);
  RUN_TEST(test_provisioning_is_purple);
  RUN_TEST(test_low_brightness_safe);
  RUN_TEST(test_all_states_distinguishable);
  return UNITY_END();
}
