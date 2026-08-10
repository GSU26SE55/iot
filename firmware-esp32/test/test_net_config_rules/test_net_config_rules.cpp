// ==================================================================
// IOT3-36/37/89 — luật cấu hình mạng runtime (WiFi + MQTT).
//
// Ba nhóm cam kết được chốt ở đây:
//   1. SSID WiFi ĐƯỢC PHÉP chứa khoảng trắng — khác hẳn apiKey/deviceCode. Nhầm sang
//      `validateIdentityField` sẽ chặn đúng những mạng phổ biến nhất ("My Home WiFi").
//   2. `deriveTopicPrefix` phải cho ra ĐÚNG chuỗi mà backend
//      `MqttBrokerEndpointProvider.TopicPrefixFor()` sinh ra: "solar/" + Trim().ToLowerInvariant().
//      Lệch một chữ hoa là mất cả uplink lẫn downlink mà KHÔNG bên nào báo lỗi.
//   3. `mqttConfigUsable` chỉ true khi đủ CẢ BỐN trường — thiếu một cái là connect thất bại
//      im lặng mỗi 5 giây.
// ==================================================================
#include <unity.h>

#include <cstring>

#include "core/net_config_rules.h"

namespace {

// ---------------------------------------------------------------- WiFi

void test_wifi_ssid_allows_space() {
  // Đây là lý do tồn tại của cả file net_config_rules.h.
  TEST_ASSERT_EQUAL(core::WifiFieldError::Ok,
                    core::validateWifiField("My Home WiFi", core::kMaxWifiSsidChars, false));
  TEST_ASSERT_EQUAL(core::WifiFieldError::Ok,
                    core::validateWifiField("Nha Anh Nam 2.4G", core::kMaxWifiSsidChars, false));
  TEST_ASSERT_TRUE(core::wifiConfigUsable("My Home WiFi"));
}

void test_wifi_ssid_rejects_empty_and_control_chars() {
  TEST_ASSERT_EQUAL(core::WifiFieldError::Empty,
                    core::validateWifiField("", core::kMaxWifiSsidChars, false));
  TEST_ASSERT_EQUAL(core::WifiFieldError::Empty,
                    core::validateWifiField(nullptr, core::kMaxWifiSsidChars, false));
  // CR/LF lọt vào là hỏng cả `WiFi.begin()` lẫn trang cấu hình HTML.
  TEST_ASSERT_EQUAL(core::WifiFieldError::InvalidChar,
                    core::validateWifiField("wifi\nname", core::kMaxWifiSsidChars, false));
  TEST_ASSERT_EQUAL(core::WifiFieldError::InvalidChar,
                    core::validateWifiField("wifi\ttab", core::kMaxWifiSsidChars, false));
  // 0x7F là DEL — vẫn là ký tự điều khiển.
  TEST_ASSERT_EQUAL(core::WifiFieldError::InvalidChar,
                    core::validateWifiField("wifi\x7F", core::kMaxWifiSsidChars, false));
}

void test_wifi_password_may_be_empty_but_ssid_may_not() {
  // Mạng mở: mật khẩu rỗng là HỢP LỆ.
  TEST_ASSERT_EQUAL(core::WifiFieldError::Ok,
                    core::validateWifiField("", core::kMaxWifiPassChars, true));
  TEST_ASSERT_EQUAL(core::WifiFieldError::Empty,
                    core::validateWifiField("", core::kMaxWifiSsidChars, false));
  // SSID rỗng ⇒ chưa cấu hình.
  TEST_ASSERT_FALSE(core::wifiConfigUsable(""));
  TEST_ASSERT_FALSE(core::wifiConfigUsable(nullptr));
}

void test_wifi_length_limits_match_802_11() {
  char ssid32[33];  memset(ssid32, 'a', 32);  ssid32[32] = '\0';
  char ssid33[34];  memset(ssid33, 'a', 33);  ssid33[33] = '\0';
  TEST_ASSERT_EQUAL(core::WifiFieldError::Ok,
                    core::validateWifiField(ssid32, core::kMaxWifiSsidChars, false));
  TEST_ASSERT_EQUAL(core::WifiFieldError::TooLong,
                    core::validateWifiField(ssid33, core::kMaxWifiSsidChars, false));

  char pass63[64];  memset(pass63, 'b', 63);  pass63[63] = '\0';
  char pass64[65];  memset(pass64, 'b', 64);  pass64[64] = '\0';
  TEST_ASSERT_EQUAL(core::WifiFieldError::Ok,
                    core::validateWifiField(pass63, core::kMaxWifiPassChars, true));
  TEST_ASSERT_EQUAL(core::WifiFieldError::TooLong,
                    core::validateWifiField(pass64, core::kMaxWifiPassChars, true));
}

// ---------------------------------------------------------------- MQTT

void test_mqtt_port_range() {
  TEST_ASSERT_FALSE(core::mqttPortUsable(0));       // 0 = "chưa đặt", không phải cổng
  TEST_ASSERT_FALSE(core::mqttPortUsable(-1));
  TEST_ASSERT_FALSE(core::mqttPortUsable(65536));
  TEST_ASSERT_TRUE (core::mqttPortUsable(1));
  TEST_ASSERT_TRUE (core::mqttPortUsable(1883));
  TEST_ASSERT_TRUE (core::mqttPortUsable(8883));
  TEST_ASSERT_TRUE (core::mqttPortUsable(65535));
}

void test_mqtt_config_needs_all_four_fields() {
  TEST_ASSERT_TRUE(core::mqttConfigUsable("broker.local", 8883, "gw-esp32-001", "s3cret"));

  TEST_ASSERT_FALSE(core::mqttConfigUsable("",             8883, "u", "p"));
  TEST_ASSERT_FALSE(core::mqttConfigUsable(nullptr,        8883, "u", "p"));
  TEST_ASSERT_FALSE(core::mqttConfigUsable("broker.local", 0,    "u", "p"));
  TEST_ASSERT_FALSE(core::mqttConfigUsable("broker.local", 8883, "",  "p"));
  TEST_ASSERT_FALSE(core::mqttConfigUsable("broker.local", 8883, "u", ""));
  TEST_ASSERT_FALSE(core::mqttConfigUsable("broker.local", 8883, "u", nullptr));
}

void test_mdns_hostname_and_query_label() {
  TEST_ASSERT_TRUE(core::isMdnsHostname("DESKTOP-CARUVEK.local"));
  TEST_ASSERT_TRUE(core::isMdnsHostname("solar-api.LOCAL"));
  TEST_ASSERT_FALSE(core::isMdnsHostname("192.168.1.6"));
  TEST_ASSERT_FALSE(core::isMdnsHostname("api.example.com"));
  TEST_ASSERT_FALSE(core::isMdnsHostname(".local"));

  char label[64];
  TEST_ASSERT_EQUAL(15u,
                    core::mdnsQueryLabel("DESKTOP-CARUVEK.local", label, sizeof(label)));
  TEST_ASSERT_EQUAL_STRING("DESKTOP-CARUVEK", label);
  TEST_ASSERT_EQUAL(0u, core::mdnsQueryLabel("api.example.com", label, sizeof(label)));
  TEST_ASSERT_EQUAL_STRING("", label);

  char tooSmall[5];
  TEST_ASSERT_EQUAL(0u,
                    core::mdnsQueryLabel("DESKTOP-CARUVEK.local", tooSmall, sizeof(tooSmall)));
}

// ------------------------------------------------- tiền tố topic

void test_topic_prefix_matches_backend_convention() {
  char buf[core::kMaxMqttPrefixChars + 1];

  // Backend lưu DeviceCode CHỮ HOA, ACL khớp theo username CHỮ THƯỜNG.
  TEST_ASSERT_TRUE(core::deriveTopicPrefix("GW-ESP32-001", buf, sizeof(buf)) > 0);
  TEST_ASSERT_EQUAL_STRING("solar/gw-esp32-001", buf);

  // Đã chữ thường thì giữ nguyên.
  TEST_ASSERT_TRUE(core::deriveTopicPrefix("gw-esp32-001", buf, sizeof(buf)) > 0);
  TEST_ASSERT_EQUAL_STRING("solar/gw-esp32-001", buf);

  // Trim khoảng trắng hai đầu — khớp `Trim()` của backend.
  TEST_ASSERT_TRUE(core::deriveTopicPrefix("  GW-ESP32-001  ", buf, sizeof(buf)) > 0);
  TEST_ASSERT_EQUAL_STRING("solar/gw-esp32-001", buf);

  TEST_ASSERT_TRUE(core::deriveTopicPrefix("\tGw-Esp32-001\t", buf, sizeof(buf)) > 0);
  TEST_ASSERT_EQUAL_STRING("solar/gw-esp32-001", buf);
}

void test_topic_prefix_returns_empty_when_unusable() {
  char buf[core::kMaxMqttPrefixChars + 1];

  // Mã rỗng ⇒ chỉ có "solar/" — vô dụng, phải trả 0 chứ không trả tiền tố cụt.
  TEST_ASSERT_EQUAL(0u, core::deriveTopicPrefix("", buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("", buf);
  TEST_ASSERT_EQUAL(0u, core::deriveTopicPrefix("   ", buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("", buf);
  TEST_ASSERT_EQUAL(0u, core::deriveTopicPrefix(nullptr, buf, sizeof(buf)));

  // Buffer bé hơn cả "solar/" ⇒ 0, không ghi bậy.
  char tiny[4];
  TEST_ASSERT_EQUAL(0u, core::deriveTopicPrefix("GW-1", tiny, sizeof(tiny)));
  TEST_ASSERT_EQUAL_STRING("", tiny);
}

void test_topic_prefix_never_truncates() {
  // Cắt cụt nguy hiểm hơn trả rỗng: "solar/gw-esp32-0" trông vẫn như một topic hợp lệ,
  // nhưng broker sẽ chặn theo ACL mà không nói vì sao.
  char small[12];   // đủ "solar/" (6) + 5 ký tự
  TEST_ASSERT_EQUAL(0u, core::deriveTopicPrefix("gw-esp32-001", small, sizeof(small)));
  TEST_ASSERT_EQUAL_STRING("", small);

  char exact[12];   // "solar/" + "abcde" + '\0' = 12
  TEST_ASSERT_EQUAL(11u, core::deriveTopicPrefix("ABCDE", exact, sizeof(exact)));
  TEST_ASSERT_EQUAL_STRING("solar/abcde", exact);
}

}  // namespace

void setUp()    {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_wifi_ssid_allows_space);
  RUN_TEST(test_wifi_ssid_rejects_empty_and_control_chars);
  RUN_TEST(test_wifi_password_may_be_empty_but_ssid_may_not);
  RUN_TEST(test_wifi_length_limits_match_802_11);
  RUN_TEST(test_mqtt_port_range);
  RUN_TEST(test_mqtt_config_needs_all_four_fields);
  RUN_TEST(test_mdns_hostname_and_query_label);
  RUN_TEST(test_topic_prefix_matches_backend_convention);
  RUN_TEST(test_topic_prefix_returns_empty_when_unusable);
  RUN_TEST(test_topic_prefix_never_truncates);
  return UNITY_END();
}
