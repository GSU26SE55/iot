// Giữ MQTT session sống qua những đoạn appTask phải chờ BMS/HTTPS/OTA đồng bộ.
//
// mqttTick() và các tác vụ mạng chạy tuần tự trong cùng appTask. Khi keepalive từng
// là 5 giây, Mosquitto production liên tục ghi "Client ESP-001 has exceeded timeout"
// dù thiết bị đã bắt tay TLS và xác thực thành công. Bài test này chặn cấu hình quá
// ngắn quay lại và buộc keepalive phải dài hơn timeout mạng đồng bộ lớn nhất.

#include <unity.h>

#include <cstdio>
#include <string>

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

namespace {

constexpr uint32_t kMinimumStableKeepaliveSeconds = 30U;
constexpr uint32_t kLongestSynchronousNetworkTimeoutMs = OTA_HTTP_TIMEOUT_MS;

std::string readFile(const char* path) {
  std::FILE* file = std::fopen(path, "rb");
  if (file == nullptr) return {};

  std::string contents;
  char buffer[4096];
  size_t count = 0;
  while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    contents.append(buffer, count);
  }
  std::fclose(file);
  return contents;
}

std::string readProjectSource(const char* relativePath) {
  std::string contents = readFile(relativePath);
  if (contents.empty()) {
    contents = readFile((std::string("firmware-esp32/") + relativePath).c_str());
  }
  return contents;
}

}  // namespace

void test_keepalive_is_not_wifi_jitter_sized() {
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(
      kMinimumStableKeepaliveSeconds,
      static_cast<uint32_t>(MQTT_KEEPALIVE_SEC));
}

void test_keepalive_outlasts_synchronous_network_timeout() {
  const uint32_t keepaliveMs =
      static_cast<uint32_t>(MQTT_KEEPALIVE_SEC) * 1000U;

  TEST_ASSERT_GREATER_THAN_UINT32(
      kLongestSynchronousNetworkTimeoutMs,
      keepaliveMs);
}

void test_production_keepalive_has_operational_margin() {
  TEST_ASSERT_EQUAL_UINT32(60U, static_cast<uint32_t>(MQTT_KEEPALIVE_SEC));
}

void test_mqtt_client_applies_the_guarded_keepalive() {
  const std::string source = readProjectSource("src/net/mqtt_client.cpp");

  TEST_ASSERT_FALSE_MESSAGE(
      source.empty(),
      "khong doc duoc mqtt_client.cpp - keepalive contract co the xanh gia");
  TEST_ASSERT_NOT_EQUAL_MESSAGE(
      std::string::npos,
      source.find("s_mqtt.setKeepAlive(MQTT_KEEPALIVE_SEC)"),
      "MQTT client phai ap dung MQTT_KEEPALIVE_SEC cho PubSubClient");
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_keepalive_is_not_wifi_jitter_sized);
  RUN_TEST(test_keepalive_outlasts_synchronous_network_timeout);
  RUN_TEST(test_production_keepalive_has_operational_margin);
  RUN_TEST(test_mqtt_client_applies_the_guarded_keepalive);
  return UNITY_END();
}
