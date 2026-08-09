// ==================================================================
// IOT3-51/90 — máy trạng thái WiFi ba chế độ.
//
// Cam kết đắt nhất được chốt ở đây: **KHÔNG mở AP khi mất mạng dưới 5 phút**. Router khách khởi
// động lại là chuyện thường ngày; mở AP mỗi lần như vậy sẽ khiến điện thoại của khách nhảy vào
// AP của thiết bị và mất mạng nhà — một lỗi rất khó truy vì nó chỉ xảy ra ở nhà khách.
//
// Cam kết thứ hai: chế độ Recovery VẪN giữ station. Bỏ station là mất khả năng tự lành, và sẽ
// phải cử người ra hiện trường cho một sự cố lẽ ra tự hết.
// ==================================================================
#include <unity.h>

#include "core/wifi_phase_policy.h"

namespace {

using core::WifiPhaseDecision;
constexpr uint32_t kFiveMin = core::kRecoveryAfterOfflineMsDefault;

void test_connected_wins_over_everything() {
  // Kể cả khi NVS trống (đang chạy bằng SSID compile-time) — rơi vào nhánh "chưa cấu hình"
  // ở đây sẽ khiến thiết bị mở AP và tự cắt kết nối đang tốt của chính nó.
  TEST_ASSERT_EQUAL(WifiPhaseDecision::Connected,
                    core::decideWifiPhase(/*hasCfg=*/true,  /*conn=*/true, 0));
  TEST_ASSERT_EQUAL(WifiPhaseDecision::Connected,
                    core::decideWifiPhase(/*hasCfg=*/false, /*conn=*/true, 0));
  TEST_ASSERT_EQUAL(WifiPhaseDecision::Connected,
                    core::decideWifiPhase(/*hasCfg=*/false, /*conn=*/true, 9'999'999));
}

void test_no_config_means_setup_portal() {
  TEST_ASSERT_EQUAL(WifiPhaseDecision::Unconfigured,
                    core::decideWifiPhase(false, false, 0));
  TEST_ASSERT_EQUAL(WifiPhaseDecision::Unconfigured,
                    core::decideWifiPhase(false, false, kFiveMin * 10));
}

void test_short_outage_does_not_open_ap() {
  // Đây là cam kết quan trọng nhất của bài test này.
  TEST_ASSERT_EQUAL(WifiPhaseDecision::Connecting, core::decideWifiPhase(true, false, 0));
  TEST_ASSERT_EQUAL(WifiPhaseDecision::Connecting, core::decideWifiPhase(true, false, 1000));
  TEST_ASSERT_EQUAL(WifiPhaseDecision::Connecting, core::decideWifiPhase(true, false, 60'000));
  TEST_ASSERT_EQUAL(WifiPhaseDecision::Connecting,
                    core::decideWifiPhase(true, false, kFiveMin - 1));
  TEST_ASSERT_FALSE(core::phaseWantsPortal(WifiPhaseDecision::Connecting));
}

void test_long_outage_enters_recovery_exactly_at_threshold() {
  TEST_ASSERT_EQUAL(WifiPhaseDecision::Recovery, core::decideWifiPhase(true, false, kFiveMin));
  TEST_ASSERT_EQUAL(WifiPhaseDecision::Recovery, core::decideWifiPhase(true, false, kFiveMin + 1));
  TEST_ASSERT_EQUAL(WifiPhaseDecision::Recovery,
                    core::decideWifiPhase(true, false, kFiveMin * 100));
}

void test_recovery_opens_portal_but_keeps_station() {
  TEST_ASSERT_TRUE(core::phaseWantsPortal(WifiPhaseDecision::Recovery));
  // Giữ station chính là điểm khác giữa "phục hồi" và "chuyển hẳn sang AP".
  TEST_ASSERT_TRUE(core::phaseKeepsStation(WifiPhaseDecision::Recovery));
}

void test_unconfigured_opens_portal_and_has_no_station_to_keep() {
  TEST_ASSERT_TRUE(core::phaseWantsPortal(WifiPhaseDecision::Unconfigured));
  TEST_ASSERT_FALSE(core::phaseKeepsStation(WifiPhaseDecision::Unconfigured));
}

void test_connected_never_wants_portal() {
  TEST_ASSERT_FALSE(core::phaseWantsPortal(WifiPhaseDecision::Connected));
  TEST_ASSERT_TRUE(core::phaseKeepsStation(WifiPhaseDecision::Connected));
}

void test_threshold_is_five_minutes() {
  TEST_ASSERT_EQUAL_UINT32(5UL * 60UL * 1000UL, core::kRecoveryAfterOfflineMsDefault);
}

void test_custom_threshold_is_honoured() {
  // Tham số riêng để test không phải chờ 5 phút giả lập — và để sau này chỉnh được nếu
  // hiện trường cho thấy 5 phút là chưa hợp.
  TEST_ASSERT_EQUAL(WifiPhaseDecision::Connecting,
                    core::decideWifiPhase(true, false, 9'000, /*recoveryAfterMs=*/10'000));
  TEST_ASSERT_EQUAL(WifiPhaseDecision::Recovery,
                    core::decideWifiPhase(true, false, 10'000, /*recoveryAfterMs=*/10'000));
}

}  // namespace

void setUp()    {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_connected_wins_over_everything);
  RUN_TEST(test_no_config_means_setup_portal);
  RUN_TEST(test_short_outage_does_not_open_ap);
  RUN_TEST(test_long_outage_enters_recovery_exactly_at_threshold);
  RUN_TEST(test_recovery_opens_portal_but_keeps_station);
  RUN_TEST(test_unconfigured_opens_portal_and_has_no_station_to_keep);
  RUN_TEST(test_connected_never_wants_portal);
  RUN_TEST(test_threshold_is_five_minutes);
  RUN_TEST(test_custom_threshold_is_honoured);
  return UNITY_END();
}
