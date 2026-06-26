// ==================================================================
// Sprint 7 — S7-FW-01 (#73): Unit test cho OTA decision logic.
//
// Pure logic (so version + so SHA-256 hex) — test native không cần ESP32.
// Chạy: pio test -e native -f test_ota_decision
// ==================================================================
#include <unity.h>

#include "ota/ota_decision.h"

using namespace ota;

// 64-hex helper (SHA-256 mẫu).
static const char* kShaLower = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
static const char* kShaUpper = "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789";
static const char* kShaDiff  = "ffffff0123456789abcdef0123456789abcdef0123456789abcdef0123456789";

// ---- otaShouldUpdate ----

void test_no_update_when_hasUpdate_false() {
  TEST_ASSERT_FALSE(otaShouldUpdate(false, "1.0.0", "2.0.0"));
}

void test_no_update_when_target_empty() {
  TEST_ASSERT_FALSE(otaShouldUpdate(true, "1.0.0", ""));
  TEST_ASSERT_FALSE(otaShouldUpdate(true, "1.0.0", nullptr));
}

void test_no_update_when_same_version() {
  TEST_ASSERT_FALSE(otaShouldUpdate(true, "1.2.3-sprint7", "1.2.3-sprint7"));
}

void test_update_when_version_differs() {
  TEST_ASSERT_TRUE(otaShouldUpdate(true, "1.0.0", "1.0.1"));
}

void test_update_when_current_null() {
  // current null (chưa biết version) + target non-empty → coi như khác → update
  TEST_ASSERT_TRUE(otaShouldUpdate(true, nullptr, "1.0.0"));
}

void test_compare_is_exact_string_not_semver() {
  // Backend so Ordinal — "1.0" khác "1.0.0" → update (đúng semantics backend).
  TEST_ASSERT_TRUE(otaShouldUpdate(true, "1.0", "1.0.0"));
}

// ---- otaSha256Equal ----

void test_sha_equal_same_case() {
  TEST_ASSERT_TRUE(otaSha256Equal(kShaLower, kShaLower));
}

void test_sha_equal_case_insensitive() {
  TEST_ASSERT_TRUE(otaSha256Equal(kShaLower, kShaUpper));
  TEST_ASSERT_TRUE(otaSha256Equal(kShaUpper, kShaLower));
}

void test_sha_differs() {
  TEST_ASSERT_FALSE(otaSha256Equal(kShaLower, kShaDiff));
}

void test_sha_rejects_wrong_length() {
  TEST_ASSERT_FALSE(otaSha256Equal("abc", kShaLower));        // quá ngắn
  TEST_ASSERT_FALSE(otaSha256Equal(kShaLower, "deadbeef"));   // quá ngắn
}

void test_sha_rejects_null() {
  TEST_ASSERT_FALSE(otaSha256Equal(nullptr, kShaLower));
  TEST_ASSERT_FALSE(otaSha256Equal(kShaLower, nullptr));
}

void setUp(void)    {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_update_when_hasUpdate_false);
  RUN_TEST(test_no_update_when_target_empty);
  RUN_TEST(test_no_update_when_same_version);
  RUN_TEST(test_update_when_version_differs);
  RUN_TEST(test_update_when_current_null);
  RUN_TEST(test_compare_is_exact_string_not_semver);
  RUN_TEST(test_sha_equal_same_case);
  RUN_TEST(test_sha_equal_case_insensitive);
  RUN_TEST(test_sha_differs);
  RUN_TEST(test_sha_rejects_wrong_length);
  RUN_TEST(test_sha_rejects_null);
  return UNITY_END();
}
