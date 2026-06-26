// ==================================================================
// Sprint 6 — S6-FW-03 (#65): Unit test cho canonical cross-source tags.
//
// Guard invariant để cross-source pairing (BMS vs IotGateway) không vỡ vì typo:
//   - 3 sensorSourceCode khác nhau, ≤ 20 chars (khớp SensorReading.sensorSourceCode[24])
//   - primary → SourceType::Bms ; redundant + external-temp → SourceType::IotGateway
//   - sourceType BMS ≠ IotGateway (điều kiện CrossSourceValidationService ghép cặp)
//
// Chạy: pio test -e native -f test_source_tags
// ==================================================================
#include <unity.h>

#include <cstring>

#include "core/source_tags.h"

using namespace core;

void test_codes_are_distinct() {
  TEST_ASSERT_NOT_EQUAL(0, strcmp(kSourceCodePrimary, kSourceCodeRedundant));
  TEST_ASSERT_NOT_EQUAL(0, strcmp(kSourceCodePrimary, kSourceCodeExternalTemp));
  TEST_ASSERT_NOT_EQUAL(0, strcmp(kSourceCodeRedundant, kSourceCodeExternalTemp));
}

void test_codes_fit_buffer() {
  // SensorReading.sensorSourceCode[24] → giới hạn ≤ 20 chars (chừa null + margin).
  TEST_ASSERT_LESS_OR_EQUAL(20, strlen(kSourceCodePrimary));
  TEST_ASSERT_LESS_OR_EQUAL(20, strlen(kSourceCodeRedundant));
  TEST_ASSERT_LESS_OR_EQUAL(20, strlen(kSourceCodeExternalTemp));
}

void test_codes_non_empty() {
  TEST_ASSERT_GREATER_THAN(0, strlen(kSourceCodePrimary));
  TEST_ASSERT_GREATER_THAN(0, strlen(kSourceCodeRedundant));
  TEST_ASSERT_GREATER_THAN(0, strlen(kSourceCodeExternalTemp));
}

void test_primary_is_bms() {
  TEST_ASSERT_EQUAL(static_cast<int>(SourceType::Bms),
                    static_cast<int>(kSourceTypePrimary));
}

void test_iot_sensors_are_iotgateway() {
  TEST_ASSERT_EQUAL(static_cast<int>(SourceType::IotGateway),
                    static_cast<int>(kSourceTypeRedundant));
  TEST_ASSERT_EQUAL(static_cast<int>(SourceType::IotGateway),
                    static_cast<int>(kSourceTypeExternalTemp));
}

// Điều kiện CỐT LÕI để cross-source pair: BMS phải KHÁC sourceType với sensor gateway.
void test_bms_differs_from_iotgateway() {
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(kSourceTypePrimary),
                        static_cast<int>(kSourceTypeRedundant));
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(kSourceTypePrimary),
                        static_cast<int>(kSourceTypeExternalTemp));
}

void setUp(void)    {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_codes_are_distinct);
  RUN_TEST(test_codes_fit_buffer);
  RUN_TEST(test_codes_non_empty);
  RUN_TEST(test_primary_is_bms);
  RUN_TEST(test_iot_sensors_are_iotgateway);
  RUN_TEST(test_bms_differs_from_iotgateway);
  return UNITY_END();
}
