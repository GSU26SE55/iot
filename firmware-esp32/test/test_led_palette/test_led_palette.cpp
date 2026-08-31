// ==================================================================
// S3-FW-05 + IOT3-54 — LED palette (trạng thái → màu + kiểu nháy).
//
// Logic thuần — không phụ thuộc Arduino/neopixelWrite.
//
// `Online` và `Queued` cùng xanh đứng im: queue tự flush, không cần người dùng xử lý.
// Các trạng thái cần can thiệp (Setup / Recovery) vẫn có kiểu nháy riêng.
//
// Chạy: pio test -e native -f test_led_palette
// ==================================================================
#include <unity.h>

#include <cstdio>
#include <initializer_list>

#include "ui/led_palette.h"

namespace {

// KHÔNG dùng macro cho phép so sánh: tham số tên `b` sẽ bị preprocessor thay nhầm vào
// trường `.b` của struct.
bool sameColor(const ui::RgbColor& x, const ui::RgbColor& y) {
  return x.r == y.r && x.g == y.g && x.b == y.b;
}

struct Entry { ui::LedState state; const char* name; };

constexpr Entry kAllStates[] = {
  { ui::LedState::Off,           "Off" },
  { ui::LedState::Online,        "Online" },
  { ui::LedState::Queued,        "Queued" },
  { ui::LedState::Offline,       "Offline" },
  { ui::LedState::Provisioning,  "Provisioning" },
  { ui::LedState::Setup,         "Setup" },
  { ui::LedState::WifiSearching, "WifiSearching" },
  { ui::LedState::Recovery,      "Recovery" },
};
constexpr size_t kStateCount = sizeof(kAllStates) / sizeof(kAllStates[0]);

void test_off_is_black() {
  auto c = ui::paletteForState(ui::LedState::Off);
  TEST_ASSERT_EQUAL(0, c.r);
  TEST_ASSERT_EQUAL(0, c.g);
  TEST_ASSERT_EQUAL(0, c.b);
  TEST_ASSERT_EQUAL(ui::LedPattern::Solid, ui::patternForState(ui::LedState::Off));
}

void test_online_is_solid_green() {
  auto c = ui::paletteForState(ui::LedState::Online);
  TEST_ASSERT_EQUAL(0, c.r);
  TEST_ASSERT_GREATER_THAN(0, c.g);
  TEST_ASSERT_EQUAL(0, c.b);
  TEST_ASSERT_EQUAL(ui::LedPattern::Solid, ui::patternForState(ui::LedState::Online));
}

void test_queued_is_solid_green() {
  // Queue tự flush nên hiển thị giống trạng thái Online.
  auto c = ui::paletteForState(ui::LedState::Queued);
  TEST_ASSERT_EQUAL(0, c.r);
  TEST_ASSERT_GREATER_THAN(0, c.g);
  TEST_ASSERT_EQUAL(0, c.b);
  TEST_ASSERT_EQUAL(ui::LedPattern::Solid, ui::patternForState(ui::LedState::Queued));
}

void test_offline_is_red() {
  auto c = ui::paletteForState(ui::LedState::Offline);
  TEST_ASSERT_GREATER_THAN(0, c.r);
  TEST_ASSERT_EQUAL(0, c.g);
  TEST_ASSERT_EQUAL(0, c.b);
}

void test_provisioning_is_solid_purple() {
  auto c = ui::paletteForState(ui::LedState::Provisioning);
  TEST_ASSERT_GREATER_THAN(0, c.r);
  TEST_ASSERT_EQUAL(0, c.g);
  TEST_ASSERT_GREATER_THAN(0, c.b);
  TEST_ASSERT_EQUAL(ui::LedPattern::Solid, ui::patternForState(ui::LedState::Provisioning));
}

void test_setup_is_blinking_purple() {
  auto c = ui::paletteForState(ui::LedState::Setup);
  TEST_ASSERT_GREATER_THAN(0, c.r);
  TEST_ASSERT_EQUAL(0, c.g);
  TEST_ASSERT_GREATER_THAN(0, c.b);
  TEST_ASSERT_EQUAL(ui::LedPattern::Blink, ui::patternForState(ui::LedState::Setup));
}

void test_wifi_searching_is_solid_orange() {
  // Cam = đỏ mạnh + xanh lá NHẸ, không có xanh dương. Xanh lá phải THẤP HƠN đỏ, nếu bằng
  // nhau thì ra vàng và lẫn với bảng màu cũ.
  auto c = ui::paletteForState(ui::LedState::WifiSearching);
  TEST_ASSERT_GREATER_THAN(0, c.r);
  TEST_ASSERT_GREATER_THAN(0, c.g);
  TEST_ASSERT_TRUE(c.g < c.r);
  TEST_ASSERT_EQUAL(0, c.b);
  TEST_ASSERT_EQUAL(ui::LedPattern::Solid, ui::patternForState(ui::LedState::WifiSearching));
}

void test_recovery_alternates_purple_and_orange() {
  TEST_ASSERT_EQUAL(ui::LedPattern::Alternate, ui::patternForState(ui::LedState::Recovery));
  auto primary   = ui::paletteForState(ui::LedState::Recovery);
  auto secondary = ui::secondaryPaletteForState(ui::LedState::Recovery);
  TEST_ASSERT_FALSE(sameColor(primary, secondary));   // xen kẽ mà hai màu giống nhau = vô nghĩa
  // Chính = tím (có xanh dương), phụ = cam (không xanh dương).
  TEST_ASSERT_GREATER_THAN(0, primary.b);
  TEST_ASSERT_EQUAL(0, secondary.b);
}

void test_secondary_defaults_to_primary_for_non_alternating() {
  for (size_t i = 0; i < kStateCount; ++i) {
    const auto s = kAllStates[i].state;
    if (ui::patternForState(s) == ui::LedPattern::Alternate) continue;
    TEST_ASSERT_TRUE_MESSAGE(
        sameColor(ui::paletteForState(s), ui::secondaryPaletteForState(s)), kAllStates[i].name);
  }
}

void test_low_brightness_safe() {
  // Mọi kênh ≤ 32/255 — tránh chói mắt + tiết kiệm điện. Áp cho CẢ màu phụ.
  for (size_t i = 0; i < kStateCount; ++i) {
    for (auto c : { ui::paletteForState(kAllStates[i].state),
                    ui::secondaryPaletteForState(kAllStates[i].state) }) {
      TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(32, c.r, kAllStates[i].name);
      TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(32, c.g, kAllStates[i].name);
      TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(32, c.b, kAllStates[i].name);
    }
  }
}

void test_every_user_actionable_state_is_distinguishable() {
  // Online và Queued cố ý giống nhau; các trạng thái còn lại phải phân biệt được.
  for (size_t i = 0; i < kStateCount; ++i) {
    for (size_t j = i + 1; j < kStateCount; ++j) {
      const auto a = kAllStates[i].state;
      const auto b = kAllStates[j].state;
      if ((a == ui::LedState::Online && b == ui::LedState::Queued) ||
          (a == ui::LedState::Queued && b == ui::LedState::Online)) continue;
      const bool colorSame   = sameColor(ui::paletteForState(a), ui::paletteForState(b));
      const bool patternSame = ui::patternForState(a) == ui::patternForState(b);
      if (colorSame && patternSame) {
        char msg[96];
        std::snprintf(msg, sizeof(msg), "%s và %s trông giống hệt nhau",
                 kAllStates[i].name, kAllStates[j].name);
        TEST_FAIL_MESSAGE(msg);
      }
    }
  }
}

void test_blink_half_period_is_visible_but_not_frantic() {
  TEST_ASSERT_TRUE(ui::kBlinkHalfPeriodMs >= 200);   // nhanh hơn nữa là nhìn như đèn hỏng
  TEST_ASSERT_TRUE(ui::kBlinkHalfPeriodMs <= 1000);  // chậm hơn nữa là tưởng đèn tắt
}

}  // namespace

void setUp(void)    {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_off_is_black);
  RUN_TEST(test_online_is_solid_green);
  RUN_TEST(test_queued_is_solid_green);
  RUN_TEST(test_offline_is_red);
  RUN_TEST(test_provisioning_is_solid_purple);
  RUN_TEST(test_setup_is_blinking_purple);
  RUN_TEST(test_wifi_searching_is_solid_orange);
  RUN_TEST(test_recovery_alternates_purple_and_orange);
  RUN_TEST(test_secondary_defaults_to_primary_for_non_alternating);
  RUN_TEST(test_low_brightness_safe);
  RUN_TEST(test_every_user_actionable_state_is_distinguishable);
  RUN_TEST(test_blink_half_period_is_visible_but_not_frantic);
  return UNITY_END();
}
