// ==================================================================
// Sprint 3 — S3-FW-02: Unit test cho idempotency key generator
// Chạy: pio test -e native -f test_idempotency
// ==================================================================
#include <unity.h>
#include <string.h>
#include <stdlib.h>

#include "core/idempotency_key.h"

void test_uuid_v4_format() {
  char buf[40];
  TEST_ASSERT_TRUE(core::generateIdempotencyKeyV4(buf, sizeof(buf)));
  // Length = 36
  TEST_ASSERT_EQUAL(36, static_cast<int>(strlen(buf)));
  // Hyphen positions: 8, 13, 18, 23
  TEST_ASSERT_EQUAL('-', buf[8]);
  TEST_ASSERT_EQUAL('-', buf[13]);
  TEST_ASSERT_EQUAL('-', buf[18]);
  TEST_ASSERT_EQUAL('-', buf[23]);
  // Version 4 char at position 14
  TEST_ASSERT_EQUAL('4', buf[14]);
  // Variant char at position 19 ∈ {8, 9, a, b}
  TEST_ASSERT_TRUE(buf[19] == '8' || buf[19] == '9' ||
                   buf[19] == 'a' || buf[19] == 'b');
}

void test_uuid_v4_uniqueness() {
  // Seed rand cho native
  srand(12345);
  char a[40], b[40];
  TEST_ASSERT_TRUE(core::generateIdempotencyKeyV4(a, sizeof(a)));
  TEST_ASSERT_TRUE(core::generateIdempotencyKeyV4(b, sizeof(b)));
  // Unity không có TEST_ASSERT_NOT_EQUAL_STRING — dùng strcmp != 0
  TEST_ASSERT_TRUE(strcmp(a, b) != 0);
}

void test_uuid_v4_buffer_too_small() {
  char tiny[10];
  TEST_ASSERT_FALSE(core::generateIdempotencyKeyV4(tiny, sizeof(tiny)));
}

void test_devcode_format() {
  char buf[128];
  TEST_ASSERT_TRUE(core::generateIdempotencyKeyDeviceCode(
      "GW-ESP32-MVP-001", 1718000000UL, 42UL, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("GW-ESP32-MVP-001-1718000000-42", buf);
}

void test_devcode_null_input() {
  char buf[128];
  TEST_ASSERT_FALSE(core::generateIdempotencyKeyDeviceCode(
      nullptr, 1, 1, buf, sizeof(buf)));
}

void setUp(void)    {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_uuid_v4_format);
  RUN_TEST(test_uuid_v4_uniqueness);
  RUN_TEST(test_uuid_v4_buffer_too_small);
  RUN_TEST(test_devcode_format);
  RUN_TEST(test_devcode_null_input);
  return UNITY_END();
}
