// ==================================================================
// Sprint 1 — S1-FW-05: Unit test cho core::buildLegacyBatchPayload
//
// Chạy:  pio test -e native -f test_payload
//
// Test framework: Unity (built-in PlatformIO).
// Mục tiêu: chứng minh payload đúng schema legacy mà không cần flash ESP32.
// ==================================================================
#include <unity.h>
#include <ArduinoJson.h>
#include <string.h>

#include "core/payload.h"
#include "bms/mock_bms.h"

namespace {

// Helper — fill reading mẫu để test deterministic.
core::SensorReading makeReading(const char* guid,
                               const char* serial,
                               float v, float i, float t, float soc, uint16_t cyc) {
  core::SensorReading r{};
  strncpy(r.batteryAssetId, guid, sizeof(r.batteryAssetId) - 1);
  strncpy(r.serial,        serial, sizeof(r.serial) - 1);
  r.voltage     = v;
  r.current     = i;
  r.temperature = t;
  r.socPercent  = soc;
  r.cycleCount  = cyc;
  return r;
}

}  // namespace

// ---- Test 1: payload top-level CHỈ có items[] (khớp api-battery.md) ----
void test_payload_top_level_fields() {
  auto r = makeReading("11111111-1111-4111-8111-000000000001",
                       "BAT-MOCK-001", 12.62f, -3.21f, 27.4f, 81.5f, 100);

  char buf[1024];
  size_t n = core::buildLegacyBatchPayload(&r, 1, "2026-06-13T08:15:42Z",
                                           "GW-ESP32-MVP-001", buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_LESS_THAN(sizeof(buf), n);

  JsonDocument doc;
  auto err = deserializeJson(doc, buf);
  TEST_ASSERT_FALSE(err);

  // KHÔNG được có wrapper fields (Sprint 3 mới thêm).
  TEST_ASSERT_FALSE(doc["deviceCode"].is<const char*>());
  TEST_ASSERT_FALSE(doc["deviceTimestamp"].is<const char*>());
  TEST_ASSERT_TRUE(doc["items"].is<JsonArray>());
  TEST_ASSERT_EQUAL(1, doc["items"].as<JsonArray>().size());
}

// ---- Test 2: Item field shape khớp legacy contract ----
void test_payload_item_fields() {
  auto r = makeReading("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee",
                       "BAT-MOCK-002", 13.10f, 0.5f, 30.1f, 75.2f, 250);

  char buf[1024];
  size_t n = core::buildLegacyBatchPayload(&r, 1, "2026-06-13T09:00:00Z",
                                           "GW-ESP32-MVP-001", buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);

  JsonDocument doc;
  deserializeJson(doc, buf);
  JsonObject it = doc["items"][0];

  TEST_ASSERT_EQUAL_STRING("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee",
                           it["batteryAssetId"]);
  TEST_ASSERT_EQUAL_STRING("2026-06-13T09:00:00Z", it["time"]);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 13.10f, it["voltage"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.001f,  0.50f, it["current"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.10f, it["temperature"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 75.20f, it["socPercent"].as<float>());
  TEST_ASSERT_EQUAL(250, it["cycleCount"].as<uint16_t>());
  // Sprint 1: KHÔNG có sourceDeviceId (Sprint 3 sẽ thêm).
  TEST_ASSERT_FALSE(it["sourceDeviceId"].is<const char*>());
}

// ---- Test 3: batch nhiều reading giữ đúng thứ tự ----
void test_payload_multiple_readings() {
  core::SensorReading rs[3] = {
    makeReading("11111111-1111-4111-8111-000000000001", "BAT-MOCK-001",
                12.5f, -1.0f, 27.0f, 80.0f, 100),
    makeReading("11111111-1111-4111-8111-000000000002", "BAT-MOCK-002",
                12.7f, -2.0f, 28.0f, 82.0f, 110),
    makeReading("11111111-1111-4111-8111-000000000003", "BAT-MOCK-003",
                12.6f,  0.5f, 26.5f, 79.0f, 120),
  };

  char buf[2048];
  size_t n = core::buildLegacyBatchPayload(rs, 3, "2026-06-13T10:00:00Z",
                                           "GW-ESP32-MVP-001", buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);

  JsonDocument doc;
  deserializeJson(doc, buf);
  JsonArray items = doc["items"];
  TEST_ASSERT_EQUAL(3, items.size());

  TEST_ASSERT_EQUAL_STRING("11111111-1111-4111-8111-000000000001",
                           items[0]["batteryAssetId"]);
  TEST_ASSERT_EQUAL_STRING("11111111-1111-4111-8111-000000000002",
                           items[1]["batteryAssetId"]);
  TEST_ASSERT_EQUAL_STRING("11111111-1111-4111-8111-000000000003",
                           items[2]["batteryAssetId"]);
}

// ---- Test 4: buffer quá nhỏ → trả 0 ----
void test_payload_buffer_overflow_returns_zero() {
  auto r = makeReading("11111111-1111-4111-8111-000000000001",
                       "BAT-MOCK-001", 12.5f, -1.0f, 27.0f, 80.0f, 100);
  char tiny[16];
  size_t n = core::buildLegacyBatchPayload(&r, 1, "2026-06-13T10:00:00Z",
                                           "GW-ESP32-MVP-001", tiny, sizeof(tiny));
  TEST_ASSERT_EQUAL(0, n);
}

// ---- Test 5: input invalid → trả 0 ----
void test_payload_invalid_inputs() {
  auto r = makeReading("11111111-1111-4111-8111-000000000001",
                       "BAT-MOCK-001", 12.5f, -1.0f, 27.0f, 80.0f, 100);
  char buf[1024];

  TEST_ASSERT_EQUAL(0, core::buildLegacyBatchPayload(nullptr, 1, "ts", "dev", buf, sizeof(buf)));
  TEST_ASSERT_EQUAL(0, core::buildLegacyBatchPayload(&r, 0, "ts", "dev", buf, sizeof(buf)));
  TEST_ASSERT_EQUAL(0, core::buildLegacyBatchPayload(&r, 1, "",   "dev", buf, sizeof(buf)));
  TEST_ASSERT_EQUAL(0, core::buildLegacyBatchPayload(&r, 1, "ts", "",    buf, sizeof(buf)));
  TEST_ASSERT_EQUAL(0, core::buildLegacyBatchPayload(&r, 1, "ts", "dev", nullptr, 1024));
}

// ---- Test 6: payloadBufferSize() đủ cho 8 reading ----
void test_payload_buffer_size_sufficient() {
  core::SensorReading rs[bms::kMockMaxBatteries];
  for (size_t i = 0; i < bms::kMockMaxBatteries; ++i) {
    rs[i] = makeReading("11111111-1111-4111-8111-000000000000",
                        "BAT-MOCK-XXX", 12.5f, -1.0f, 27.0f, 80.0f, 100);
  }
  const size_t bufLen = core::payloadBufferSize(bms::kMockMaxBatteries);
  char* buf = new char[bufLen];
  size_t n = core::buildLegacyBatchPayload(rs, bms::kMockMaxBatteries,
                                           "2026-06-13T10:00:00Z",
                                           "GW-ESP32-MVP-001", buf, bufLen);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_LESS_THAN(bufLen, n);
  delete[] buf;
}

void setUp(void)    {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_payload_top_level_fields);
  RUN_TEST(test_payload_item_fields);
  RUN_TEST(test_payload_multiple_readings);
  RUN_TEST(test_payload_buffer_overflow_returns_zero);
  RUN_TEST(test_payload_invalid_inputs);
  RUN_TEST(test_payload_buffer_size_sufficient);
  return UNITY_END();
}
