// ==================================================================
// Sprint 3 — S3-FW-01: Local queue for offline batches
//
// Spec NI §9.1 + OV §B7: "Local queue (NVS/LittleFS) + retry exponential backoff".
// Tasksprint S3-FW-01: "buffer NVS (FIFO, key = epoch), max 200 batch".
//
// Implementation note (deviation from strict-NVS):
//   Spec mention NVS nhưng default NVS partition = 24KB. 200 batch × ~1KB/batch
//   = 200KB → KHÔNG fit NVS mặc định. NI §9.1 cho phép "NVS/LittleFS" — chọn
//   LittleFS để fit 200 batch trong 16MB flash partition.
//
//   Trade-off: LittleFS có overhead format lần đầu (~5-10s) nhưng one-time.
//              NVS có wear leveling tốt hơn LittleFS — acceptable cho Sprint 3
//              vì queue cycle ~5s/batch (write rare khi backend up).
//
// File layout:
//   /queue/<epoch>.json  — body JSON
//   /queue/<epoch>.idem  — idempotency key (one-line)
//   Filename = epoch sec zero-padded 10 digits → glob sorted = FIFO.
//
// AC: enqueue 200 → 201 → drop oldest. Unit test verify trên native (mock FS).
//
// Tham chiếu:
//   - tasksprint.md S3-FW-01
//   - NI §9.1 + OV §B7
// ==================================================================
#pragma once
#include <cstddef>
#include <cstdint>

#include "queue/queue_index.h"   // kMaxQueuedBatches + pure helpers

namespace queue {

// kMaxQueuedBatches đã định nghĩa ở queue_index.h (re-export qua include).

// Khởi tạo SD card (ưu tiên) hoặc LittleFS (fallback) + tạo /queue/.
// Không tự format khi mount lỗi để tránh xóa telemetry chưa gửi.
bool queueBegin();

// Đẩy 1 batch vào queue. Nếu >= kMaxQueuedBatches → xóa file cũ nhất rồi push.
// `epochSec` dùng làm filename (tránh collision khi multi-push trong cùng giây
// thì caller cần đảm bảo unique — Sprint 3 ingest 5s/batch nên epoch unique).
//
// Trả true nếu enqueue thành công.
bool queueEnqueue(uint32_t epochSec,
                  const char* body, size_t bodyLen,
                  const char* idempotencyKey);

// Đọc batch cũ nhất ra `outBody` / `outIdem` (không xóa). Trả false nếu queue rỗng.
// `outBody`/`outIdem` caller cấp; nếu buffer < actual size → truncate + return false.
// `outEpochSec` filled với epoch của batch đọc — caller dùng để gọi queueDelete().
bool queuePeekOldest(char* outBody, size_t outBodyLen, size_t* outBodyBytes,
                     char* outIdem, size_t outIdemLen,
                     uint32_t* outEpochSec);

// Xóa batch theo epoch — gọi sau khi flush thành công.
bool queueDelete(uint32_t epochSec);

// Số batch đang queue.
size_t queueSize();

// Runtime storage diagnostics. Values are zero when queueBegin() failed.
const char* queueStorageName();
uint64_t queueStorageTotalBytes();
uint64_t queueStorageUsedBytes();

// Xóa tất cả — debug/reset.
bool queueClear();

}  // namespace queue
