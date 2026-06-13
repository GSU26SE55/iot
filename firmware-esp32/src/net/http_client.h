// ==================================================================
// Sprint 1 — S1-FW-06: HTTPS client để POST batch ingest
//
// Tham chiếu:
//   - tasksprint.md S1-FW-06
//   - newiot.md §7.4 (header X-Api-Key + X-Device-Code)
//
// Lưu ý dev:
//   - Sprint 1 dùng WiFiClientSecure::setInsecure() — KHÔNG verify CA (PILOT only).
//   - Sprint 3 sẽ thay bằng root CA bundle + Idempotency-Key + local queue.
// ==================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace net {

struct PostResult {
  int    httpCode;          // HTTP status (200 = OK), -1 nếu transport fail.
  size_t requestBytes;      // số byte body đã gửi (debug).
  uint32_t durationMs;      // wall-clock thời gian POST (debug).
  // Trả body backend nếu cần debug — tối đa 256 chars (truncate). null-terminated.
  char   responseSnippet[256];
};

// Khởi tạo HTTP client (lần đầu setup TLS). Gọi 1 lần trong setup().
void httpClientBegin();

// POST `body` (Content-Type: application/json) lên `BACKEND_URL + path`.
// Thêm header X-Api-Key, X-Device-Code (từ config.h).
//
// Trả PostResult. Hàm KHÔNG retry — caller quyết định.
PostResult httpPostJson(const char* path,
                        const char* body,
                        size_t      bodyLen);

}  // namespace net
