// ==================================================================
// Sprint 3 — S3-FW-03: Exponential backoff (base 2s, max 5 min, jitter ±20%)
//
// Spec NI §1 #2 — "Retry exponential backoff (base 2s, max 5 phút, jitter ±20%)".
// AC: Tắt backend → log thấy backoff tăng dần; bật lại → flush hết.
//
// Algorithm:
//   attempt 0 → no wait (first attempt)
//   attempt 1 → wait 2s ± 20%
//   attempt 2 → wait 4s ± 20%
//   attempt 3 → wait 8s ± 20%
//   ...
//   attempt N → wait min(2^N · 1000ms, 300_000ms) ± 20%
//
// Jitter giúp tránh thundering herd khi nhiều device cùng retry sau backend up.
// ==================================================================
#pragma once
#include <cstdint>

namespace net {

constexpr uint32_t kBackoffBaseMs    = 2000UL;     // 2s
constexpr uint32_t kBackoffMaxMs     = 300000UL;   // 5 phút
constexpr float    kBackoffJitterPct = 0.20f;      // ±20%

// Sprint 3 fix: phân biệt transient vs permanent failure.
//
// - Transient (retry với backoff): network error / 5xx / 408 timeout / 429 rate limit
// - Permanent (drop khỏi queue + log warning): 4xx khác (400 validation, 401 auth,
//   403 scope, 404 route, 409 state conflict)
//
// Lý do: 4xx data sai → retry không bao giờ thành công → infinite loop +
//        queue đầy → drop oldest valid data → loss.
inline bool isTransientFailure(int httpCode) {
  if (httpCode <= 0) return true;                  // network err / timeout / DNS fail
  if (httpCode >= 500) return true;                // server error 5xx
  if (httpCode == 408 || httpCode == 429) return true;  // timeout / rate limit
  return false;                                     // 4xx khác = permanent
}

class Backoff {
 public:
  Backoff();

  // Sau mỗi attempt thất bại, gọi `recordFailure()` để tăng counter.
  // Trả về số ms nên đợi trước attempt tiếp theo.
  uint32_t recordFailure();

  // Reset về attempt 0 — gọi khi 1 batch POST thành công.
  void reset();

  // Trả về số ms còn lại cho đến khi được phép retry.
  // Caller dùng: `if (millis() >= nextRetryMs) attempt();`.
  uint32_t nextRetryAt() const { return _nextRetryMs; }

  uint32_t attemptCount() const { return _attempt; }

  // Helper test — set seed cho jitter (default = millis()).
  static void setRandomSeed(uint32_t seed);

 private:
  uint32_t _attempt;
  uint32_t _nextRetryMs;
};

}  // namespace net
