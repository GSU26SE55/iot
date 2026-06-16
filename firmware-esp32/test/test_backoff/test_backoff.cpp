// ==================================================================
// Sprint 3 — S3-FW-03: Unit test cho exponential backoff
// Chạy: pio test -e native -f test_backoff
// ==================================================================
#include <unity.h>

#include "net/backoff.h"

void test_backoff_initial_zero() {
  net::Backoff b;
  TEST_ASSERT_EQUAL(0, static_cast<int>(b.attemptCount()));
  TEST_ASSERT_EQUAL(0, static_cast<int>(b.nextRetryAt()));
}

void test_backoff_exponential_growth() {
  net::Backoff::setRandomSeed(42);
  net::Backoff b;
  uint32_t d1 = b.recordFailure();
  uint32_t d2 = b.recordFailure();
  uint32_t d3 = b.recordFailure();

  // d1 ≈ 2000ms ±20% → [1600, 2400]
  TEST_ASSERT_GREATER_OR_EQUAL(1600u, d1);
  TEST_ASSERT_LESS_OR_EQUAL(2400u, d1);

  // d2 ≈ 4000ms ±20% → [3200, 4800]
  TEST_ASSERT_GREATER_OR_EQUAL(3200u, d2);
  TEST_ASSERT_LESS_OR_EQUAL(4800u, d2);

  // d3 ≈ 8000ms ±20% → [6400, 9600]
  TEST_ASSERT_GREATER_OR_EQUAL(6400u, d3);
  TEST_ASSERT_LESS_OR_EQUAL(9600u, d3);
}

void test_backoff_cap_at_max() {
  net::Backoff::setRandomSeed(42);
  net::Backoff b;
  // 8 attempts cumulatively → 2,4,8,16,32,64,128,256 (capped to 300s)
  for (int i = 0; i < 10; ++i) {
    b.recordFailure();
  }
  uint32_t d = b.recordFailure();
  // d ≤ 300000 (max 5 phút) ± 20%
  TEST_ASSERT_LESS_OR_EQUAL(net::kBackoffMaxMs, d);
  // d ≥ 240000 (5min - 20% jitter floor)
  TEST_ASSERT_GREATER_OR_EQUAL(240000u, d);
}

void test_backoff_reset() {
  net::Backoff::setRandomSeed(42);
  net::Backoff b;
  b.recordFailure();
  b.recordFailure();
  b.recordFailure();
  TEST_ASSERT_EQUAL(3, static_cast<int>(b.attemptCount()));
  b.reset();
  TEST_ASSERT_EQUAL(0, static_cast<int>(b.attemptCount()));
  TEST_ASSERT_EQUAL(0, static_cast<int>(b.nextRetryAt()));
}

void test_backoff_jitter_within_bounds() {
  // 100 lần attempt 1 → tất cả phải nằm trong [1600, 2400]
  net::Backoff::setRandomSeed(1);
  for (int i = 0; i < 100; ++i) {
    net::Backoff b;
    uint32_t d = b.recordFailure();
    TEST_ASSERT_GREATER_OR_EQUAL(1600u, d);
    TEST_ASSERT_LESS_OR_EQUAL(2400u, d);
  }
}

// Sprint 3 fix: phân biệt transient vs permanent failure
void test_transient_classification() {
  // Network err / timeout / DNS fail
  TEST_ASSERT_TRUE(net::isTransientFailure(-1));
  TEST_ASSERT_TRUE(net::isTransientFailure(0));

  // 5xx server errors
  TEST_ASSERT_TRUE(net::isTransientFailure(500));
  TEST_ASSERT_TRUE(net::isTransientFailure(502));
  TEST_ASSERT_TRUE(net::isTransientFailure(503));
  TEST_ASSERT_TRUE(net::isTransientFailure(599));

  // 408 timeout + 429 rate limit
  TEST_ASSERT_TRUE(net::isTransientFailure(408));
  TEST_ASSERT_TRUE(net::isTransientFailure(429));
}

void test_permanent_classification() {
  // 4xx data/auth/route errors → permanent (drop)
  TEST_ASSERT_FALSE(net::isTransientFailure(400));   // validation
  TEST_ASSERT_FALSE(net::isTransientFailure(401));   // auth
  TEST_ASSERT_FALSE(net::isTransientFailure(403));   // scope
  TEST_ASSERT_FALSE(net::isTransientFailure(404));   // route
  TEST_ASSERT_FALSE(net::isTransientFailure(409));   // state conflict
  TEST_ASSERT_FALSE(net::isTransientFailure(422));   // semantic err
}

void test_success_codes_are_not_failures() {
  // 2xx codes: caller phải check trước rồi mới gọi isTransientFailure cho fail.
  // Nhưng vẫn verify behavior — 2xx không phải transient (vì không phải fail).
  TEST_ASSERT_FALSE(net::isTransientFailure(200));
  TEST_ASSERT_FALSE(net::isTransientFailure(201));
  TEST_ASSERT_FALSE(net::isTransientFailure(204));
}

void setUp(void)    {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_backoff_initial_zero);
  RUN_TEST(test_backoff_exponential_growth);
  RUN_TEST(test_backoff_cap_at_max);
  RUN_TEST(test_backoff_reset);
  RUN_TEST(test_backoff_jitter_within_bounds);
  // Sprint 3 — transient/permanent classification
  RUN_TEST(test_transient_classification);
  RUN_TEST(test_permanent_classification);
  RUN_TEST(test_success_codes_are_not_failures);
  return UNITY_END();
}
