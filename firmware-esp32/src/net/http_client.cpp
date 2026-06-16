// ==================================================================
// Sprint 1 — S1-FW-06: HTTPS client implementation
// Sprint 2 — S2-FW-04: thêm `X-Device-Code` + đọc credentials runtime từ identity store
//
// Acceptance:
//   - S1-FW-06: Backend nhận 2xx; có log response
//   - S2-FW-04: backend log thấy 2 header X-Api-Key + X-Device-Code
//
// Lưu ý:
//   - WiFiClientSecure::setInsecure() chỉ chấp nhận trong dev (Sprint 1 PILOT).
//     Sprint 3 hoặc Sprint 4 phải thay bằng root CA bundle.
// ==================================================================
#include "net/http_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

#include "config/device_identity.h"     // S2-FW-01 + S2-FW-04: runtime credentials

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

namespace net {

namespace {
// Static để không cấp phát lại heap mỗi request — TLS handshake tốn 30–60kB heap;
// reuse giúp keep-alive (cùng host).
WiFiClientSecure s_tlsClient;
bool             s_tlsConfigured = false;

size_t copyResponseFull(HTTPClient& http,
                        char* snippet, size_t snippetLen,
                        char* fullBuf, size_t fullBufLen) {
  String body = http.getString();
  size_t bodyLen = body.length();

  if (snippet != nullptr && snippetLen > 0) {
    size_t n = bodyLen < (snippetLen - 1) ? bodyLen : (snippetLen - 1);
    memcpy(snippet, body.c_str(), n);
    snippet[n] = '\0';
  }
  if (fullBuf != nullptr && fullBufLen > 0) {
    size_t n = bodyLen < (fullBufLen - 1) ? bodyLen : (fullBufLen - 1);
    memcpy(fullBuf, body.c_str(), n);
    fullBuf[n] = '\0';
    return n;
  }
  return 0;
}

PostResult postJsonInternal(const char* path,
                            const char* body, size_t bodyLen,
                            char* respBuf, size_t respBufLen,
                            size_t* outRespBytes,
                            const char* idempotencyKey = nullptr) {
  PostResult res{};
  res.httpCode      = -1;
  res.requestBytes  = bodyLen;
  res.responseBytes = 0;
  res.responseSnippet[0] = '\0';
  if (outRespBytes) *outRespBytes = 0;

  if (!s_tlsConfigured) httpClientBegin();

  String url;
  url.reserve(96 + (path ? strlen(path) : 0));
  url += BACKEND_URL;
  url += path;

  HTTPClient http;
  http.setReuse(true);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);

  uint32_t t0 = millis();
  if (!http.begin(s_tlsClient, url)) {
    Serial.printf("[http] begin() failed url=%s\n", url.c_str());
    res.durationMs = millis() - t0;
    return res;
  }

  // S2-FW-04: 2 header bắt buộc đọc từ identity store runtime.
  http.addHeader("Content-Type",   "application/json");
  http.addHeader("X-Api-Key",      identity::apiKey());
  http.addHeader("X-Device-Code",  identity::deviceCode());
  http.addHeader("Accept",         "application/json");

  // Sprint 3 (S3-FW-02): Idempotency-Key cho ingest endpoint — dedup backend §IoT2-16.
  if (idempotencyKey && idempotencyKey[0] != '\0') {
    http.addHeader("Idempotency-Key", idempotencyKey);
  }

  int code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body)),
                       bodyLen);

  res.httpCode   = code;
  res.durationMs = millis() - t0;

  if (code > 0) {
    size_t n = copyResponseFull(http,
                                res.responseSnippet, sizeof(res.responseSnippet),
                                respBuf, respBufLen);
    res.responseBytes = n;
    if (outRespBytes) *outRespBytes = n;
    Serial.printf("[http] POST %s → %d (%lums, %u bytes sent, %u bytes resp)\n",
                  path, code,
                  static_cast<unsigned long>(res.durationMs),
                  static_cast<unsigned>(bodyLen),
                  static_cast<unsigned>(n));
  } else {
    Serial.printf("[http] POST %s FAIL (%lums) err=%s\n",
                  path,
                  static_cast<unsigned long>(res.durationMs),
                  http.errorToString(code).c_str());
  }

  http.end();
  return res;
}
}  // namespace

void httpClientBegin() {
  if (s_tlsConfigured) return;
  // PILOT: bỏ qua verify CA. Sprint 3+ phải set root CA.
  s_tlsClient.setInsecure();
  s_tlsConfigured = true;
  Serial.println("[http] TLS configured (INSECURE — dev only)");
}

PostResult httpPostJson(const char* path,
                        const char* body,
                        size_t      bodyLen) {
  return postJsonInternal(path, body, bodyLen, nullptr, 0, nullptr);
}

PostResult httpPostJsonRecv(const char* path,
                            const char* body, size_t bodyLen,
                            char* respBuf, size_t respBufLen,
                            size_t* outRespBytes) {
  return postJsonInternal(path, body, bodyLen, respBuf, respBufLen, outRespBytes);
}

PostResult httpPostJsonWithIdempotency(const char* path,
                                       const char* body, size_t bodyLen,
                                       const char* idempotencyKey) {
  return postJsonInternal(path, body, bodyLen, nullptr, 0, nullptr, idempotencyKey);
}

}  // namespace net
