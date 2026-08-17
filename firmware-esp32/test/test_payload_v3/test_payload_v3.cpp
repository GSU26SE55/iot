// ==================================================================
// Sprint 3 — S3-FW-04: Unit test cho buildProductionBatchPayload
//
// Chạy: pio test -e native -f test_payload_v3
// ==================================================================
#include <unity.h>
#include <ArduinoJson.h>
#include <string.h>

#include "core/payload.h"
#include "core/reading.h"

namespace {

core::SensorReading makeBmsPrimary(const char* serial, float v, float i, float t, float soc) {
  core::SensorReading r{};
  strncpy(r.serial, serial, sizeof(r.serial) - 1);
  strncpy(r.sensorSourceCode, "primary", sizeof(r.sensorSourceCode) - 1);
  r.sourceType       = core::SourceType::Bms;
  r.voltage          = v; r.current = i; r.temperature = t; r.socPercent = soc;
  r.cycleCount       = 120;
  r.sohPercent       = 94.2f; r.hasSoh = true;
  r.chargingState    = core::ChargingState::Discharging; r.hasChargingState = true;
  r.hasBmsError      = false;
  return r;
}

core::SensorReading makeIotRedundant(const char* serial, float v, float i, float t, float soc) {
  core::SensorReading r{};
  strncpy(r.serial, serial, sizeof(r.serial) - 1);
  strncpy(r.sensorSourceCode, "redundant", sizeof(r.sensorSourceCode) - 1);
  r.sourceType       = core::SourceType::IotGateway;
  r.voltage          = v; r.current = i; r.temperature = t; r.socPercent = soc;
  r.cycleCount       = 120;
  r.hasSoh           = false;
  r.hasChargingState = false;
  r.hasBmsError      = false;
  return r;
}

core::SensorReading makeIotExternalTemp(const char* serial, float v, float i, float t, float soc) {
  core::SensorReading r{};
  strncpy(r.serial, serial, sizeof(r.serial) - 1);
  strncpy(r.sensorSourceCode, "external-temp", sizeof(r.sensorSourceCode) - 1);
  r.sourceType       = core::SourceType::IotGateway;
  r.voltage          = v; r.current = i; r.temperature = t; r.socPercent = soc;
  r.cycleCount       = 120;
  r.hasSoh           = false;
  r.hasChargingState = false;
  r.hasBmsError      = false;
  return r;
}

}  // namespace

// Test 1: production payload top-level + items[] structure
void test_v3_top_level() {
  auto r = makeBmsPrimary("BAT-2026-001", 12.6f, -5.2f, 35.4f, 78.5f);

  char buf[2048];
  size_t n = core::buildProductionBatchPayload(&r, 1, "2026-06-14T08:15:30Z",
                                               "GW-ESP32-001", buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);

  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, buf));
  TEST_ASSERT_TRUE(doc["items"].is<JsonArray>());
  TEST_ASSERT_EQUAL(1, doc["items"].as<JsonArray>().size());
}

// Test 2: production item fields — Sprint 3 specific
void test_v3_production_fields() {
  auto r = makeBmsPrimary("BAT-2026-001", 12.6f, -5.2f, 35.4f, 78.5f);

  char buf[2048];
  size_t n = core::buildProductionBatchPayload(&r, 1, "2026-06-14T08:15:30Z",
                                               "GW-ESP32-001", buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);

  JsonDocument doc;
  deserializeJson(doc, buf);
  JsonObject it = doc["items"][0];

  // Sprint 3: batteryAssetSerial (priority over batteryAssetId)
  TEST_ASSERT_EQUAL_STRING("BAT-2026-001", it["batteryAssetSerial"]);
  TEST_ASSERT_FALSE(it["batteryAssetId"].is<const char*>());

  // Time + deviceTimestamp (clock skew check #IoT2-15)
  // Timestamp được vá ms = index item (fix đụng PK (Time, BatteryAssetId) backend).
  TEST_ASSERT_EQUAL_STRING("2026-06-14T08:15:30.000Z", it["time"]);
  TEST_ASSERT_EQUAL_STRING("2026-06-14T08:15:30.000Z", it["deviceTimestamp"]);

  // Sensor values
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.6f, it["voltage"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.2f, it["current"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 35.4f, it["temperature"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 78.5f, it["socPercent"].as<float>());
  TEST_ASSERT_EQUAL(120, it["cycleCount"].as<int>());

  // Sprint 3 production fields
  TEST_ASSERT_EQUAL_STRING("primary", it["sensorSourceCode"]);
  TEST_ASSERT_EQUAL(1, it["sourceType"].as<int>());        // Bms
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 94.2f, it["sohPercent"].as<float>());
  TEST_ASSERT_EQUAL(3, it["chargingState"].as<int>());     // Discharging
}

// Test 3: optional fields KHÔNG xuất hiện khi flags = false
void test_v3_optional_fields_skipped() {
  auto r = makeIotRedundant("BAT-2026-001", 12.62f, -5.25f, 35.5f, 78.5f);

  char buf[2048];
  size_t n = core::buildProductionBatchPayload(&r, 1, "2026-06-14T08:15:30Z",
                                               "GW-ESP32-001", buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);

  JsonDocument doc;
  deserializeJson(doc, buf);
  JsonObject it = doc["items"][0];

  // IotGateway redundant — sourceType=2, sensorSourceCode="redundant"
  TEST_ASSERT_EQUAL_STRING("redundant", it["sensorSourceCode"]);
  TEST_ASSERT_EQUAL(2, it["sourceType"].as<int>());

  // KHÔNG có sohPercent/chargingState/bmsErrorCode (sensor không biết)
  TEST_ASSERT_FALSE(it["sohPercent"].is<float>());
  TEST_ASSERT_FALSE(it["chargingState"].is<int>());
  TEST_ASSERT_FALSE(it["bmsErrorCode"].is<const char*>());
}

// Test 4: cross-source — 2 reading cùng battery khác sourceType (S3-FW-04 AC)
void test_v3_cross_source_pair() {
  core::SensorReading rs[2] = {
    makeBmsPrimary("BAT-2026-001",  12.60f, -5.20f, 35.4f, 78.5f),
    makeIotRedundant("BAT-2026-001", 12.62f, -5.25f, 35.5f, 78.5f),
  };

  char buf[2048];
  size_t n = core::buildProductionBatchPayload(rs, 2, "2026-06-14T08:15:30Z",
                                               "GW-ESP32-001", buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);

  JsonDocument doc;
  deserializeJson(doc, buf);
  JsonArray items = doc["items"];
  TEST_ASSERT_EQUAL(2, items.size());

  // S3-FW-04 AC: "payload có ít nhất 2 reading cùng battery, khác sourceType"
  TEST_ASSERT_EQUAL_STRING("BAT-2026-001", items[0]["batteryAssetSerial"]);
  TEST_ASSERT_EQUAL_STRING("BAT-2026-001", items[1]["batteryAssetSerial"]);
  TEST_ASSERT_EQUAL(1, items[0]["sourceType"].as<int>());   // Bms
  TEST_ASSERT_EQUAL(2, items[1]["sourceType"].as<int>());   // IotGateway
  TEST_ASSERT_EQUAL_STRING("primary",   items[0]["sensorSourceCode"]);
  TEST_ASSERT_EQUAL_STRING("redundant", items[1]["sensorSourceCode"]);
}

// Test 5: bmsErrorCode trong overheat case
void test_v3_bms_error_code() {
  auto r = makeBmsPrimary("BAT-2026-001", 12.6f, -5.2f, 68.0f, 78.5f);
  strncpy(r.bmsErrorCode, "OVT-PROTECT", sizeof(r.bmsErrorCode) - 1);
  r.hasBmsError = true;

  char buf[2048];
  size_t n = core::buildProductionBatchPayload(&r, 1, "2026-06-14T08:15:30Z",
                                               "GW-ESP32-001", buf, sizeof(buf));
  JsonDocument doc;
  deserializeJson(doc, buf);
  TEST_ASSERT_EQUAL_STRING("OVT-PROTECT", doc["items"][0]["bmsErrorCode"]);
}

// Test 6: 3-source mapping đầy đủ (BMS + INA226 + DS18B20 per MO §52.9)
void test_v3_three_sources_per_battery() {
  core::SensorReading rs[3] = {
    makeBmsPrimary    ("BAT-2026-001", 12.60f, -5.20f, 35.4f, 78.5f),
    makeIotRedundant  ("BAT-2026-001", 12.62f, -5.25f, 35.5f, 78.5f),
    makeIotExternalTemp("BAT-2026-001", 12.60f, -5.20f, 35.2f, 78.5f),
  };

  char buf[4096];
  size_t n = core::buildProductionBatchPayload(rs, 3, "2026-06-14T08:15:30Z",
                                               "GW-ESP32-001", buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);

  JsonDocument doc;
  deserializeJson(doc, buf);
  JsonArray items = doc["items"];
  TEST_ASSERT_EQUAL(3, items.size());

  // Source mapping per MO §52.9
  TEST_ASSERT_EQUAL_STRING("primary",       items[0]["sensorSourceCode"]);
  TEST_ASSERT_EQUAL(1, items[0]["sourceType"].as<int>());   // Bms

  TEST_ASSERT_EQUAL_STRING("redundant",     items[1]["sensorSourceCode"]);
  TEST_ASSERT_EQUAL(2, items[1]["sourceType"].as<int>());   // IotGateway

  TEST_ASSERT_EQUAL_STRING("external-temp", items[2]["sensorSourceCode"]);
  TEST_ASSERT_EQUAL(2, items[2]["sourceType"].as<int>());   // IotGateway

  // 3 reading cùng battery — cross-source S6 §1.6.6 đủ điều kiện
  TEST_ASSERT_EQUAL_STRING("BAT-2026-001", items[0]["batteryAssetSerial"]);
  TEST_ASSERT_EQUAL_STRING("BAT-2026-001", items[1]["batteryAssetSerial"]);
  TEST_ASSERT_EQUAL_STRING("BAT-2026-001", items[2]["batteryAssetSerial"]);
}

// Test 7: bmsErrorCode đúng 64 chars (biên trên — spec ≤ 64)
void test_v3_bms_error_code_64_chars_boundary() {
  auto r = makeBmsPrimary("BAT-2026-001", 12.6f, -5.2f, 68.0f, 78.5f);
  // Tạo string đúng 64 ký tự
  const char* err64 = "OVT-PROTECT-CELL01-OVT-PROTECT-CELL02-OVT-PROTECT-CELL03-XXXXYYY";   // 64 chars
  TEST_ASSERT_EQUAL(64, static_cast<int>(strlen(err64)));
  strncpy(r.bmsErrorCode, err64, sizeof(r.bmsErrorCode) - 1);
  r.bmsErrorCode[sizeof(r.bmsErrorCode) - 1] = '\0';
  r.hasBmsError = true;

  char buf[2048];
  size_t n = core::buildProductionBatchPayload(&r, 1, "2026-06-14T08:15:30Z",
                                               "GW-ESP32-001", buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);

  JsonDocument doc;
  deserializeJson(doc, buf);
  const char* output = doc["items"][0]["bmsErrorCode"];
  TEST_ASSERT_EQUAL_STRING(err64, output);
  TEST_ASSERT_EQUAL(64, static_cast<int>(strlen(output)));
}

// Test 8: buffer too small returns 0
void test_v3_buffer_overflow() {
  auto r = makeBmsPrimary("BAT-2026-001", 12.6f, -5.2f, 35.4f, 78.5f);
  char tiny[16];
  size_t n = core::buildProductionBatchPayload(&r, 1, "2026-06-14T08:15:30Z",
                                               "GW-ESP32-001", tiny, sizeof(tiny));
  TEST_ASSERT_EQUAL(0, n);
}

void setUp(void)    {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_v3_top_level);
  RUN_TEST(test_v3_production_fields);
  RUN_TEST(test_v3_optional_fields_skipped);
  RUN_TEST(test_v3_cross_source_pair);
  RUN_TEST(test_v3_bms_error_code);
  RUN_TEST(test_v3_three_sources_per_battery);
  RUN_TEST(test_v3_bms_error_code_64_chars_boundary);
  RUN_TEST(test_v3_buffer_overflow);
  return UNITY_END();
}
