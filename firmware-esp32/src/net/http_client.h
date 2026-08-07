// ==================================================================
// Sprint 1 — S1-FW-06: HTTPS client để POST batch ingest
// Sprint 2 — S2-FW-02/03/04: provision + heartbeat + X-Device-Code
//
// Tham chiếu:
//   - tasksprint.md S1-FW-06, S2-FW-04
//   - newiot.md §7.4 (header X-Api-Key + X-Device-Code)
//
// Lưu ý dev:
//   - Sprint 1 dùng WiFiClientSecure::setInsecure() — KHÔNG verify CA (PILOT only).
//   - Sprint 3 sẽ thay bằng root CA bundle + Idempotency-Key + local queue.
// ==================================================================
#pragma once
#include <cstddef>
#include <cstdint>

#include <WiFiClientSecure.h>

namespace net {

struct PostResult {
  int      httpCode;        // HTTP status (200/201 = OK), -1 nếu transport fail.
  size_t   requestBytes;    // số byte body đã gửi (debug).
  uint32_t durationMs;      // wall-clock thời gian POST (debug).
  size_t   responseBytes;   // số byte response body backend trả (sau khi truncate vào snippet).
  // Trả body backend nếu cần debug — tối đa 256 chars (truncate). null-terminated.
  char     responseSnippet[256];
};

// Khởi tạo HTTP client (lần đầu setup TLS). Gọi 1 lần trong setup().
void httpClientBegin();

/// GH-735 — gắn CA cert vào một WiFiClientSecure bất kỳ (dùng chung cho HTTPS ingest + OTA).
/// Trả false khi KHÔNG nạp được CA ⇒ caller PHẢI từ chối kết nối, không được đi tiếp.
/// (Ngoại lệ duy nhất: build đặt TLS_ALLOW_INSECURE=1 — chỉ dành cho dev.)
bool httpConfigureTls(WiFiClientSecure& client);

/// True nếu TLS đang chạy ở chế độ KHÔNG verify (chỉ xảy ra khi TLS_ALLOW_INSECURE=1).
bool httpTlsIsInsecure();

// POST `body` (Content-Type: application/json) lên `BACKEND_URL + path`.
// Thêm header X-Api-Key + X-Device-Code (lấy từ identity store — S2-FW-04).
// Response body bị truncate vào `responseSnippet` (256 chars).
PostResult httpPostJson(const char* path,
                        const char* body,
                        size_t      bodyLen);

// Sprint 2 variant: caller cấp respBuf để nhận full response (cho provision JSON).
// `respBufLen` ≥ response thực; nếu nhỏ hơn → truncate + null-terminate.
// PostResult.responseSnippet vẫn được điền (256 chars đầu).
// Trả về số byte ghi vào respBuf qua `outRespBytes` (out param).
PostResult httpPostJsonRecv(const char* path,
                            const char* body,
                            size_t      bodyLen,
                            char*       respBuf,
                            size_t      respBufLen,
                            size_t*     outRespBytes);

// Sprint 7 (S7-FW-01): GET `BACKEND_URL + path` (path đã gồm query string nếu có).
// Thêm header X-Api-Key + X-Device-Code (giống POST). Response body ghi vào `respBuf`
// (truncate + null-terminate nếu nhỏ hơn). Số byte ghi trả qua `outRespBytes`.
// Dùng cho firmware-check (response JSON nhỏ).
PostResult httpGetJsonRecv(const char* path,
                           char*       respBuf,
                           size_t      respBufLen,
                           size_t*     outRespBytes);

// Sprint 7 (S7-FW-02): PUT JSON lên `BACKEND_URL + path` (firmware-update-log status).
// Headers X-Api-Key + X-Device-Code. Response chỉ cần status code (snippet cho debug).
PostResult httpPutJson(const char* path, const char* body, size_t bodyLen);

// Sprint 3 (S3-FW-02): POST với `Idempotency-Key` header.
// Backend §IoT2-16 dedup theo (deviceCode, idempotencyKey) TTL 24h.
// `idempotencyKey` null/empty → KHÔNG set header (legacy mode).
PostResult httpPostJsonWithIdempotency(const char* path,
                                       const char* body,
                                       size_t      bodyLen,
                                       const char* idempotencyKey);

}  // namespace net
