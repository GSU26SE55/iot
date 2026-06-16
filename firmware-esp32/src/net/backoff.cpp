// ==================================================================
// Sprint 3 — S3-FW-03: Exponential backoff implementation
// ==================================================================
#include "net/backoff.h"

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
  #include <Arduino.h>
  static inline uint32_t platformMillis() { return millis(); }
#else
  #include <chrono>
  static inline uint32_t platformMillis() {
    using namespace std::chrono;
    static auto start = steady_clock::now();
    auto now = steady_clock::now();
    return static_cast<uint32_t>(duration_cast<milliseconds>(now - start).count());
  }
#endif

#include <cstdlib>

namespace net {

namespace {
// Linear congruential RNG để jitter — deterministic seed, KHÔNG dùng cho crypto.
uint32_t s_rngState = 0x12345678;

float nextJitter() {
  s_rngState = s_rngState * 1103515245u + 12345u;
  uint32_t bits = (s_rngState >> 16) & 0x7FFF;
  // Map [0, 32767] → [-1.0, +1.0]
  return (static_cast<float>(bits) / 16383.5f) - 1.0f;
}
}  // namespace

Backoff::Backoff() : _attempt(0), _nextRetryMs(0) {}

uint32_t Backoff::recordFailure() {
  _attempt++;

  // Compute base delay: 2^attempt * baseMs, clamped to max
  uint32_t baseDelay = kBackoffBaseMs;
  for (uint32_t i = 1; i < _attempt && baseDelay < kBackoffMaxMs; ++i) {
    if (baseDelay > kBackoffMaxMs / 2) {
      baseDelay = kBackoffMaxMs;
      break;
    }
    baseDelay *= 2;
  }
  if (baseDelay > kBackoffMaxMs) baseDelay = kBackoffMaxMs;

  // Jitter ±20%
  float jitter = nextJitter() * kBackoffJitterPct;
  int32_t delta = static_cast<int32_t>(static_cast<float>(baseDelay) * jitter);
  uint32_t finalDelay = static_cast<uint32_t>(
      static_cast<int32_t>(baseDelay) + delta);

  // Final clamp — vẫn không vượt max sau jitter
  if (finalDelay > kBackoffMaxMs) finalDelay = kBackoffMaxMs;
  // Min sanity — không < 100ms cho dù jitter âm
  if (finalDelay < 100UL) finalDelay = 100UL;

  _nextRetryMs = platformMillis() + finalDelay;
  return finalDelay;
}

void Backoff::reset() {
  _attempt     = 0;
  _nextRetryMs = 0;
}

void Backoff::setRandomSeed(uint32_t seed) {
  s_rngState = seed;
}

}  // namespace net
