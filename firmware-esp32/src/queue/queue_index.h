// ==================================================================
// Sprint 3 — S3-FW-01: Pure FIFO index logic (testable on native)
//
// Tách phần thuật toán "tìm oldest + drop khi đầy" ra header-only template
// để test trên native không cần LittleFS. Storage backend (LittleFS impl
// trong local_queue.cpp) chỉ gọi các helper này để quyết định hành động.
//
// Algorithm:
//   - Tập epoch trong queue lưu ở set/list sorted ascending
//   - oldest = minimum epoch
//   - shouldDropOldest = size >= maxSize
//   - resolveCollision = nếu epoch trùng, bump +1 đến khi unique
// ==================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace queue {

// Capacity tối đa của queue — spec S3-FW-01 "max 200 batch".
// Định nghĩa ở header này để pure unit test trên native truy cập được mà KHÔNG
// cần include local_queue.h (Arduino-only).
constexpr size_t kMaxQueuedBatches = 200;

// Tìm epoch nhỏ nhất (oldest) trong mảng. Trả 0 nếu mảng rỗng.
// `epochs` không cần sorted; hàm scan O(n).
inline uint32_t findOldestEpoch(const uint32_t* epochs, size_t count) {
  if (epochs == nullptr || count == 0) return 0;
  uint32_t oldest = UINT32_MAX;
  for (size_t i = 0; i < count; ++i) {
    if (epochs[i] < oldest) oldest = epochs[i];
  }
  return oldest == UINT32_MAX ? 0 : oldest;
}

// Tìm epoch lớn nhất (newest). Trả 0 nếu rỗng.
inline uint32_t findNewestEpoch(const uint32_t* epochs, size_t count) {
  if (epochs == nullptr || count == 0) return 0;
  uint32_t newest = 0;
  for (size_t i = 0; i < count; ++i) {
    if (epochs[i] > newest) newest = epochs[i];
  }
  return newest;
}

// Có nên drop oldest trước khi enqueue mới không?
inline bool shouldDropOldest(size_t currentSize, size_t maxSize) {
  return currentSize >= maxSize;
}

// Tìm epoch còn trống bắt đầu từ `desiredEpoch` (bump +1 nếu trùng).
// Trả epoch unique. Caller dùng kết quả này làm filename mới.
inline uint32_t resolveCollision(uint32_t desiredEpoch,
                                  const uint32_t* existingEpochs,
                                  size_t existingCount) {
  uint32_t e = desiredEpoch;
  bool collision = true;
  while (collision) {
    collision = false;
    for (size_t i = 0; i < existingCount; ++i) {
      if (existingEpochs[i] == e) { collision = true; e++; break; }
    }
  }
  return e;
}

}  // namespace queue
