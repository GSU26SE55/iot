// GH-740 — xem reading_filter.h.

#include "core/reading_filter.h"

#include <cstring>

namespace core {

namespace {

bool isPublished(const char* serial,
                 const char* const* publishedSerials, size_t publishedCount) {
  // Serial rỗng ⇒ coi như CHƯA gửi: thà gửi thừa còn hơn làm mất bản ghi.
  if (serial == nullptr || serial[0] == '\0') return false;
  if (publishedSerials == nullptr) return false;

  for (size_t i = 0; i < publishedCount; ++i) {
    const char* p = publishedSerials[i];
    if (p != nullptr && std::strcmp(p, serial) == 0) return true;
  }
  return false;
}

}  // namespace

size_t filterOutPublished(const SensorReading* in, size_t inCount,
                          const char* const* publishedSerials, size_t publishedCount,
                          SensorReading* out, size_t outCap) {
  if (in == nullptr || out == nullptr || outCap == 0) return 0;

  size_t n = 0;
  for (size_t i = 0; i < inCount && n < outCap; ++i) {
    if (isPublished(in[i].serial, publishedSerials, publishedCount)) continue;
    out[n++] = in[i];
  }
  return n;
}

}  // namespace core
