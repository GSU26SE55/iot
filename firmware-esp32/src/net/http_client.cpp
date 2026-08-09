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
#include <LittleFS.h>

#include "net/tls_ca.h"
#include "net/ca_cert_embedded.h"       // IOT3-23: CA nhúng dùng chung với mqtt_client
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
  if (!s_tlsConfigured) {          // GH-735 — không có CA thì không gửi đi đâu cả.
    res.durationMs = 0;
    return res;
  }

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

PostResult httpGetJsonRecv(const char* path,
                           char* respBuf, size_t respBufLen,
                           size_t* outRespBytes) {
  PostResult res{};
  res.httpCode      = -1;
  res.requestBytes  = 0;
  res.responseBytes = 0;
  res.responseSnippet[0] = '\0';
  if (outRespBytes) *outRespBytes = 0;

  if (!s_tlsConfigured) httpClientBegin();
  if (!s_tlsConfigured) {          // GH-735 — không có CA thì không gửi đi đâu cả.
    res.durationMs = 0;
    return res;
  }

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
    Serial.printf("[http] GET begin() failed url=%s\n", url.c_str());
    res.durationMs = millis() - t0;
    return res;
  }

  http.addHeader("X-Api-Key",     identity::apiKey());
  http.addHeader("X-Device-Code", identity::deviceCode());
  http.addHeader("Accept",        "application/json");

  int code = http.GET();
  res.httpCode   = code;
  res.durationMs = millis() - t0;

  if (code > 0) {
    size_t n = copyResponseFull(http,
                                res.responseSnippet, sizeof(res.responseSnippet),
                                respBuf, respBufLen);
    res.responseBytes = n;
    if (outRespBytes) *outRespBytes = n;
    Serial.printf("[http] GET %s → %d (%lums, %u bytes resp)\n",
                  path, code,
                  static_cast<unsigned long>(res.durationMs),
                  static_cast<unsigned>(n));
  } else {
    Serial.printf("[http] GET %s FAIL (%lums) err=%s\n",
                  path,
                  static_cast<unsigned long>(res.durationMs),
                  http.errorToString(code).c_str());
  }

  http.end();
  return res;
}

PostResult httpPutJson(const char* path, const char* body, size_t bodyLen) {
  PostResult res{};
  res.httpCode      = -1;
  res.requestBytes  = bodyLen;
  res.responseBytes = 0;
  res.responseSnippet[0] = '\0';

  if (!s_tlsConfigured) httpClientBegin();
  if (!s_tlsConfigured) {          // GH-735 — không có CA thì không gửi đi đâu cả.
    res.durationMs = 0;
    return res;
  }

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
    Serial.printf("[http] PUT begin() failed url=%s\n", url.c_str());
    res.durationMs = millis() - t0;
    return res;
  }

  http.addHeader("Content-Type",  "application/json");
  http.addHeader("X-Api-Key",     identity::apiKey());
  http.addHeader("X-Device-Code", identity::deviceCode());
  http.addHeader("Accept",        "application/json");

  int code = http.PUT(reinterpret_cast<uint8_t*>(const_cast<char*>(body)), bodyLen);
  res.httpCode   = code;
  res.durationMs = millis() - t0;

  if (code > 0) {
    res.responseBytes = copyResponseFull(http, res.responseSnippet,
                                         sizeof(res.responseSnippet), nullptr, 0);
    Serial.printf("[http] PUT %s → %d (%lums)\n", path, code,
                  static_cast<unsigned long>(res.durationMs));
  } else {
    Serial.printf("[http] PUT %s FAIL (%lums) err=%s\n", path,
                  static_cast<unsigned long>(res.durationMs),
                  http.errorToString(code).c_str());
  }

  http.end();
  return res;
}

namespace {

// Cache CA PEM. setCACert() giữ CON TRỎ chứ không copy, nên chuỗi phải sống lâu hơn
// mọi kết nối ⇒ static, không phải biến cục bộ.
String s_caPem;
bool   s_caLoaded    = false;
bool   s_caAttempted = false;
bool   s_insecureMode = false;

tls::CaLoadStatus loadCaPemOnce() {
  if (s_caLoaded) return tls::CaLoadStatus::Ok;
  if (s_caAttempted && !s_caLoaded) return tls::CaLoadStatus::FileMissing;
  s_caAttempted = true;

  // IOT3-23 — ƯU TIÊN CA NHÚNG, giống hệt mqtt_client.cpp::loadCaCert().
  //
  // Trước đây đường HTTPS chỉ đọc LittleFS trong khi đường MQTT đã chuyển sang CA
  // nhúng (ca_cert_embedded.h). Lý do phải nhúng — ghi ngay trong header đó — là
  // "ảnh mklittlefs không mount được nên phân vùng bị xoá sạch mỗi lần boot, cuốn
  // theo ca_cert.pem". Nếu điều đó đúng thì LittleFS.begin(false) ở dưới trả false,
  // TLS_ALLOW_INSECURE=0 khiến httpConfigureTls() fail-closed, và postJsonInternal()
  // return ngay ⇒ provision + heartbeat + OTA + đẩy bù hàng đợi CHẾT HẾT, trong khi
  // telemetry vẫn chảy qua MQTT nên dashboard trông vẫn bình thường.
  //
  // Đặt CA nhúng TRƯỚC LittleFS (ngược với thứ tự "ưu tiên file để thay tại hiện
  // trường") là có chủ ý: giữ hai đường TLS CÙNG MỘT logic quan trọng hơn, vì chính
  // việc hai đường xử lý khác nhau đã tạo ra lỗ hổng này.
  if (kMqttCaCert[0] != '\0' &&
      tls::isLikelyPemCertificate(kMqttCaCert, strlen(kMqttCaCert))) {
    s_caPem = String(kMqttCaCert);
    s_caLoaded = true;
    return tls::CaLoadStatus::Ok;
  }

  // formatOnFail=false CÓ CHỦ Ý: format sẽ xoá luôn hàng đợi offline và chính file CA
  // (xem GH-936). Thà báo lỗi còn hơn tự ý xoá dữ liệu.
  if (!LittleFS.begin(false)) return tls::CaLoadStatus::FilesystemUnavailable;
  if (!LittleFS.exists(TLS_CA_CERT_PATH)) return tls::CaLoadStatus::FileMissing;

  File f = LittleFS.open(TLS_CA_CERT_PATH, "r");
  if (!f) return tls::CaLoadStatus::FileUnreadable;
  s_caPem = f.readString();
  f.close();

  if (!tls::isLikelyPemCertificate(s_caPem.c_str(), s_caPem.length()))
    return tls::CaLoadStatus::NotPemFormat;

  s_caLoaded = true;
  return tls::CaLoadStatus::Ok;
}

}  // namespace

bool httpConfigureTls(WiFiClientSecure& client) {
  const tls::CaLoadStatus st = loadCaPemOnce();
  if (st == tls::CaLoadStatus::Ok) {
    client.setCACert(s_caPem.c_str());
    s_insecureMode = false;
    return true;
  }

#if TLS_ALLOW_INSECURE
  // Lối thoát dev — cố ý ồn ào để không ai vô tình để lọt lên production.
  Serial.printf("[http] *** TLS KHÔNG VERIFY *** (%s). CHỈ dùng cho dev — "
                "TLS_ALLOW_INSECURE=1.\n", tls::describe(st));
  client.setInsecure();
  s_insecureMode = true;
  return true;
#else
  // GH-735 — FAIL CLOSED. Trước đây nhánh này gọi setInsecure(), nghĩa là thiếu CA thì
  // firmware vẫn chạy nhưng nhận mọi chứng chỉ: kẻ đứng giữa trả về bản mô tả firmware
  // giả kèm SHA khớp là thiết bị flash luôn. Không nạp được CA thì KHÔNG kết nối.
  Serial.printf("[http] TLS FAIL: %s. Từ chối kết nối.\n", tls::describe(st));
  Serial.printf("[http]   → copy CA vào data%s rồi `pio run -t uploadfs`\n", TLS_CA_CERT_PATH);
  s_insecureMode = false;
  return false;
#endif
}

bool httpTlsIsInsecure() { return s_insecureMode; }

void httpClientBegin() {
  if (s_tlsConfigured) return;
  s_tlsConfigured = httpConfigureTls(s_tlsClient);
  if (s_tlsConfigured)
    Serial.println("[http] TLS configured (verify CA)");
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
