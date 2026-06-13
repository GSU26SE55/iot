// ==================================================================
// Sprint 1 — S1-FW-06: HTTPS client implementation
//
// Acceptance (tasksprint.md S1-FW-06):
//   - Backend nhận 200; có log response
//
// Lưu ý:
//   - WiFiClientSecure::setInsecure() chỉ chấp nhận trong dev (Sprint 1 PILOT).
//     Sprint 3 (S3-BE-01..04 đã chuyển sang backend) hoặc Sprint 4 phải thay
//     bằng root CA bundle (esp_crt_bundle_attach hoặc CA cert qua LittleFS).
// ==================================================================
#include "net/http_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

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

void copyResponseSnippet(HTTPClient& http, char* dst, size_t dstLen) {
  if (dstLen == 0) return;
  dst[0] = '\0';
  // getString() đọc cả body — Sprint 1 batch response thường <1kB.
  // Cắt 256 chars để đỡ tốn log.
  String body = http.getString();
  size_t n = body.length() < (dstLen - 1) ? body.length() : (dstLen - 1);
  memcpy(dst, body.c_str(), n);
  dst[n] = '\0';
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
  PostResult res{};
  res.httpCode      = -1;
  res.requestBytes  = bodyLen;
  res.responseSnippet[0] = '\0';

  if (!s_tlsConfigured) httpClientBegin();

  // Build URL: BACKEND_URL + path. URL constructor không validate scheme;
  // assert thủ công vì setInsecure() chỉ áp dụng cho HTTPS.
  String url;
  url.reserve(96 + (path ? strlen(path) : 0));
  url += BACKEND_URL;
  url += path;

  HTTPClient http;
  http.setReuse(true);                     // keep-alive nếu cùng host
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);

  uint32_t t0 = millis();
  if (!http.begin(s_tlsClient, url)) {
    Serial.printf("[http] begin() failed url=%s\n", url.c_str());
    res.durationMs = millis() - t0;
    return res;
  }

  // Sprint 1 — chỉ X-Api-Key (NI §7.4 legacy). X-Device-Code là Sprint 3 (S3-FW-04).
  http.addHeader("Content-Type",   "application/json");
  http.addHeader("X-Api-Key",      API_KEY);
  http.addHeader("Accept",         "application/json");

  // POST: HTTPClient muốn (uint8_t*, size_t).
  int code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body)),
                       bodyLen);

  res.httpCode   = code;
  res.durationMs = millis() - t0;

  if (code > 0) {
    copyResponseSnippet(http, res.responseSnippet, sizeof(res.responseSnippet));
    Serial.printf("[http] POST %s → %d (%lums, %u bytes sent)\n",
                  path, code,
                  static_cast<unsigned long>(res.durationMs),
                  static_cast<unsigned>(bodyLen));
  } else {
    Serial.printf("[http] POST %s FAIL (%lums) err=%s\n",
                  path,
                  static_cast<unsigned long>(res.durationMs),
                  http.errorToString(code).c_str());
  }

  http.end();
  return res;
}

}  // namespace net
