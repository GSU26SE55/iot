// ==================================================================
// Sprint 3 — S3-FW-01: Unit test cho queue FIFO logic
//
// AC tasksprint S3-FW-01: "Unit test: enqueue 200 → 201 đẩy ra cái cũ nhất"
//
// Test này verify thuật toán quản lý queue (find oldest, drop oldest, FIFO order).
// Phần LittleFS-glue chỉ test được trên ESP32 (file I/O không có trên native).
// Pure algorithm logic ở `src/queue/queue_index.h` — test được trên native.
//
// Chạy: pio test -e native -f test_queue
// ==================================================================
#include <unity.h>
#include <algorithm>
#include <string>
#include <vector>

#include "queue/queue_index.h"      // pure helpers + kMaxQueuedBatches

namespace {

// In-memory simulated queue dùng cho test thuật toán.
// Same algorithm as LittleFS-backed local_queue.cpp.
class SimulatedQueue {
 public:
  bool enqueue(uint32_t desiredEpoch, const std::string& body) {
    // Drop oldest nếu đầy (same logic local_queue.cpp::queueEnqueue)
    if (queue::shouldDropOldest(_epochs.size(), queue::kMaxQueuedBatches)) {
      uint32_t oldest = queue::findOldestEpoch(_epochs.data(), _epochs.size());
      auto it = std::find(_epochs.begin(), _epochs.end(), oldest);
      if (it != _epochs.end()) {
        _bodies.erase(_bodies.begin() + (it - _epochs.begin()));
        _epochs.erase(it);
      }
    }
    // Resolve collision (epoch trùng → bump +1)
    uint32_t e = queue::resolveCollision(desiredEpoch, _epochs.data(), _epochs.size());
    _epochs.push_back(e);
    _bodies.push_back(body);
    return true;
  }

  bool peekOldest(uint32_t* outEpoch, std::string* outBody) {
    if (_epochs.empty()) return false;
    uint32_t oldest = queue::findOldestEpoch(_epochs.data(), _epochs.size());
    auto it = std::find(_epochs.begin(), _epochs.end(), oldest);
    if (it == _epochs.end()) return false;
    *outEpoch = *it;
    *outBody  = _bodies[it - _epochs.begin()];
    return true;
  }

  bool deleteEpoch(uint32_t epoch) {
    auto it = std::find(_epochs.begin(), _epochs.end(), epoch);
    if (it == _epochs.end()) return false;
    _bodies.erase(_bodies.begin() + (it - _epochs.begin()));
    _epochs.erase(it);
    return true;
  }

  size_t size() const { return _epochs.size(); }
  bool contains(uint32_t epoch) const {
    return std::find(_epochs.begin(), _epochs.end(), epoch) != _epochs.end();
  }

 private:
  std::vector<uint32_t>    _epochs;
  std::vector<std::string> _bodies;
};

}  // namespace

// ─── Tests pure logic helpers ───────────────────────────────────────

void test_find_oldest_empty() {
  TEST_ASSERT_EQUAL(0u, queue::findOldestEpoch(nullptr, 0));
  uint32_t empty[1] = {};
  TEST_ASSERT_EQUAL(0u, queue::findOldestEpoch(empty, 0));
}

void test_find_oldest_single() {
  uint32_t e[] = {42};
  TEST_ASSERT_EQUAL(42u, queue::findOldestEpoch(e, 1));
}

void test_find_oldest_many() {
  uint32_t e[] = {1000, 500, 2000, 100, 1500};
  TEST_ASSERT_EQUAL(100u, queue::findOldestEpoch(e, 5));
}

void test_find_newest_many() {
  uint32_t e[] = {1000, 500, 2000, 100, 1500};
  TEST_ASSERT_EQUAL(2000u, queue::findNewestEpoch(e, 5));
}

void test_should_drop_at_max() {
  TEST_ASSERT_FALSE(queue::shouldDropOldest(0,   200));
  TEST_ASSERT_FALSE(queue::shouldDropOldest(199, 200));
  TEST_ASSERT_TRUE (queue::shouldDropOldest(200, 200));
  TEST_ASSERT_TRUE (queue::shouldDropOldest(500, 200));
}

void test_resolve_collision_no_conflict() {
  uint32_t existing[] = {100, 200, 300};
  TEST_ASSERT_EQUAL(50u,  queue::resolveCollision(50,  existing, 3));
  TEST_ASSERT_EQUAL(150u, queue::resolveCollision(150, existing, 3));
}

void test_resolve_collision_bump() {
  uint32_t existing[] = {100, 200, 300};
  TEST_ASSERT_EQUAL(101u, queue::resolveCollision(100, existing, 3));
  TEST_ASSERT_EQUAL(201u, queue::resolveCollision(200, existing, 3));
}

void test_resolve_collision_multiple_bumps() {
  // Epoch 100, 101, 102 đều có → mới enqueue 100 → bump tới 103
  uint32_t existing[] = {100, 101, 102, 200};
  TEST_ASSERT_EQUAL(103u, queue::resolveCollision(100, existing, 4));
}

// ─── Tests core S3-FW-01 AC ─────────────────────────────────────────

void test_enqueue_within_capacity() {
  SimulatedQueue q;
  for (int i = 1; i <= 10; ++i) {
    q.enqueue(static_cast<uint32_t>(1000 + i), "batch" + std::to_string(i));
  }
  TEST_ASSERT_EQUAL(10, static_cast<int>(q.size()));
}

// ★ AC chính của S3-FW-01: enqueue 200 → 201 đẩy ra cái cũ nhất
void test_enqueue_200_then_201st_drops_oldest() {
  SimulatedQueue q;

  // Enqueue 200 batches, epoch 1000..1199
  for (uint32_t i = 0; i < 200; ++i) {
    q.enqueue(1000 + i, "batch" + std::to_string(i));
  }
  TEST_ASSERT_EQUAL(200, static_cast<int>(q.size()));
  TEST_ASSERT_TRUE(q.contains(1000));     // oldest still in
  TEST_ASSERT_TRUE(q.contains(1199));     // newest in

  // Enqueue 201st (epoch 1200) → expect drop oldest (1000)
  q.enqueue(1200, "batch200");
  TEST_ASSERT_EQUAL(200, static_cast<int>(q.size()));   // size stays at max
  TEST_ASSERT_FALSE(q.contains(1000));    // 1000 dropped!
  TEST_ASSERT_TRUE (q.contains(1001));    // 1001 now oldest
  TEST_ASSERT_TRUE (q.contains(1200));    // 1200 added

  // Enqueue 202nd → drop 1001
  q.enqueue(1201, "batch201");
  TEST_ASSERT_EQUAL(200, static_cast<int>(q.size()));
  TEST_ASSERT_FALSE(q.contains(1001));
  TEST_ASSERT_TRUE (q.contains(1002));
  TEST_ASSERT_TRUE (q.contains(1201));
}

void test_peek_oldest_returns_smallest_epoch() {
  SimulatedQueue q;
  q.enqueue(2000, "b1");
  q.enqueue(1500, "b2");
  q.enqueue(2500, "b3");

  uint32_t e = 0;
  std::string body;
  TEST_ASSERT_TRUE(q.peekOldest(&e, &body));
  TEST_ASSERT_EQUAL(1500u, e);
  TEST_ASSERT_EQUAL_STRING("b2", body.c_str());
}

void test_delete_oldest_advances() {
  SimulatedQueue q;
  q.enqueue(1000, "b1");
  q.enqueue(2000, "b2");
  q.enqueue(3000, "b3");

  TEST_ASSERT_TRUE(q.deleteEpoch(1000));
  TEST_ASSERT_EQUAL(2, static_cast<int>(q.size()));
  uint32_t e = 0;
  std::string body;
  q.peekOldest(&e, &body);
  TEST_ASSERT_EQUAL(2000u, e);
}

void test_fifo_order_preserved_through_drops() {
  SimulatedQueue q;
  // Fill 200, then push 50 more → expect oldest 50 dropped
  for (uint32_t i = 0; i < 250; ++i) {
    q.enqueue(10000 + i, "");
  }
  TEST_ASSERT_EQUAL(200, static_cast<int>(q.size()));
  // 10000..10049 dropped
  for (uint32_t i = 0; i < 50; ++i) {
    TEST_ASSERT_FALSE(q.contains(10000 + i));
  }
  // 10050..10249 still present
  for (uint32_t i = 50; i < 250; ++i) {
    TEST_ASSERT_TRUE(q.contains(10000 + i));
  }
}

void test_kMaxQueuedBatches_is_200() {
  // Sanity: spec yêu cầu max 200
  TEST_ASSERT_EQUAL(200, static_cast<int>(queue::kMaxQueuedBatches));
}

void setUp(void)    {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  // Pure helpers
  RUN_TEST(test_find_oldest_empty);
  RUN_TEST(test_find_oldest_single);
  RUN_TEST(test_find_oldest_many);
  RUN_TEST(test_find_newest_many);
  RUN_TEST(test_should_drop_at_max);
  RUN_TEST(test_resolve_collision_no_conflict);
  RUN_TEST(test_resolve_collision_bump);
  RUN_TEST(test_resolve_collision_multiple_bumps);
  // AC tests
  RUN_TEST(test_enqueue_within_capacity);
  RUN_TEST(test_enqueue_200_then_201st_drops_oldest);   // ★ S3-FW-01 AC chính
  RUN_TEST(test_peek_oldest_returns_smallest_epoch);
  RUN_TEST(test_delete_oldest_advances);
  RUN_TEST(test_fifo_order_preserved_through_drops);
  RUN_TEST(test_kMaxQueuedBatches_is_200);
  return UNITY_END();
}
