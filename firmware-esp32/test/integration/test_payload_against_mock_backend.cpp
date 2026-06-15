// ==================================================================
// Sprint 1 — Integration test: payload builder → live mock backend
//
// Mục đích: chứng minh JSON sinh bởi core::buildLegacyBatchPayload
// được mock-backend chấp nhận (HTTP 200 + isSuccess=true).
//
// Đây là test HOST-SIDE (build trên macOS/Linux, không phải ESP32).
// Lý do: src/net/http_client.cpp dùng WiFiClientSecure — chỉ chạy được trên Arduino.
//        Ở đây mô phỏng pipeline bằng libcurl để verify shape end-to-end.
//
// Run: tools/sprint1-integration-test.sh
// ==================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/payload.h"
#include "bms/mock_bms.h"

// libcurl — system lib, link với -lcurl
#include <curl/curl.h>

namespace {

size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  std::string* s = static_cast<std::string*>(userp);
  s->append(static_cast<char*>(contents), size * nmemb);
  return size * nmemb;
}

int postJson(const std::string& url, const char* body, size_t bodyLen,
             long& httpCode, std::string& responseOut) {
  CURL* curl = curl_easy_init();
  if (!curl) return -1;

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "X-Api-Key: iotk_test");
  headers = curl_slist_append(headers, "X-Device-Code: GW-ESP32-MVP-001");

  curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     body);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,  static_cast<long>(bodyLen));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  writeCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &responseOut);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT,        5L);

  CURLcode rc = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return rc == CURLE_OK ? 0 : -1;
}

}  // namespace

int main(int argc, char** argv) {
  const char* url = (argc > 1) ? argv[1] : "http://localhost:17200/api/sensor-readings/batch";
  printf("[itest] target=%s\n", url);

  // 1) Build 4 mock readings (deterministic — không gọi mockGenerate vì cần esp_random)
  core::SensorReading rs[4];
  const char* guids[4] = {
    "11111111-1111-4111-8111-000000000001",
    "11111111-1111-4111-8111-000000000002",
    "11111111-1111-4111-8111-000000000003",
    "11111111-1111-4111-8111-000000000004",
  };
  float volts[4] = {12.51f, 12.78f, 12.62f, 13.01f};
  for (int i = 0; i < 4; ++i) {
    memset(&rs[i], 0, sizeof(rs[i]));
    strcpy(rs[i].batteryAssetId, guids[i]);
    snprintf(rs[i].serial, sizeof(rs[i].serial), "BAT-MOCK-%03d", i + 1);
    rs[i].voltage     = volts[i];
    rs[i].current     = -3.5f + i * 0.5f;
    rs[i].temperature = 26.5f + i * 0.7f;
    rs[i].socPercent  = 81.5f - i * 1.2f;
    rs[i].cycleCount  = 100 + i * 12;
  }

  // 2) Build JSON via library code (đúng file đang chạy trên ESP32)
  char buf[4096];
  size_t n = core::buildLegacyBatchPayload(rs, 4,
                                           "2026-06-13T08:15:42Z",
                                           "GW-ESP32-MVP-001",
                                           buf, sizeof(buf));
  if (n == 0) { fprintf(stderr, "[itest] FAIL: buildLegacyBatchPayload returned 0\n"); return 1; }
  printf("[itest] payload built (%zu bytes)\n", n);

  // 3) POST lên mock backend
  long code = 0;
  std::string resp;
  if (postJson(url, buf, n, code, resp) != 0) {
    fprintf(stderr, "[itest] FAIL: HTTP transport error\n"); return 2;
  }
  printf("[itest] response code=%ld body=%s\n", code, resp.c_str());

  // 4) Assert: code 200 + isSuccess=true + accepted=4
  //    (tasksprint S1-FW-06 acceptance: "Backend nhận 200; có log response")
  if (code != 201) { fprintf(stderr, "[itest] FAIL: expected 201 got %ld\n", code); return 3; }
  if (resp.find("\"isSuccess\": true") == std::string::npos &&
      resp.find("\"isSuccess\":true")  == std::string::npos) {
    fprintf(stderr, "[itest] FAIL: response missing isSuccess=true\n"); return 4;
  }
  // Sprint 3 mock backend returns totalReceived/inserted (matches real backend
  // SensorReadingBatchIngestResult shape).
  if (resp.find("\"totalReceived\": 4") == std::string::npos &&
      resp.find("\"totalReceived\":4")  == std::string::npos) {
    fprintf(stderr, "[itest] FAIL: response missing totalReceived=4\n"); return 5;
  }

  // 5) Negative #1: temperature out of NI §7.4 range (-50..150) → 400 + listErrors
  const char* badTemp = "{\"items\":[{"
                        "\"batteryAssetId\":\"11111111-1111-4111-8111-000000000001\","
                        "\"time\":\"2026-06-13T08:15:42Z\","
                        "\"voltage\":12.5,\"current\":0,\"temperature\":999,\"socPercent\":50}]}";
  std::string resp2;
  if (postJson(url, badTemp, strlen(badTemp), code, resp2) != 0 || code != 400) {
    fprintf(stderr, "[itest] FAIL: bad temp expected 400 got %ld body=%s\n", code, resp2.c_str());
    return 6;
  }
  if (resp2.find("listErrors") == std::string::npos) {
    fprintf(stderr, "[itest] FAIL: 400 response missing listErrors[]\n"); return 7;
  }
  printf("[itest] negative #1 OK (400 for temperature=999, NI §7.4 range -50..150)\n");

  // 6) Negative #2: voltage > 1000 (NI §7.4 hard-limit) → 400
  const char* badVolt = "{\"items\":[{"
                        "\"batteryAssetId\":\"11111111-1111-4111-8111-000000000001\","
                        "\"time\":\"2026-06-13T08:15:42Z\","
                        "\"voltage\":9999,\"current\":0,\"temperature\":25,\"socPercent\":50}]}";
  std::string resp3;
  if (postJson(url, badVolt, strlen(badVolt), code, resp3) != 0 || code != 400) {
    fprintf(stderr, "[itest] FAIL: voltage 9999 expected 400 got %ld\n", code); return 8;
  }
  printf("[itest] negative #2 OK (400 for voltage=9999, NI §7.4 hard-limit 1000)\n");

  // 7) Negative #3: batteryAssetId không phải Guid → 400
  const char* badUuid = "{\"items\":[{"
                        "\"batteryAssetId\":\"not-a-uuid\","
                        "\"time\":\"2026-06-13T08:15:42Z\","
                        "\"voltage\":12.5,\"current\":0,\"temperature\":25,\"socPercent\":50}]}";
  std::string resp4;
  if (postJson(url, badUuid, strlen(badUuid), code, resp4) != 0 || code != 400) {
    fprintf(stderr, "[itest] FAIL: bad Guid expected 400 got %ld\n", code); return 9;
  }
  printf("[itest] negative #3 OK (400 for batteryAssetId non-Guid)\n");

  printf("[itest] PASS — payload matches tasksprint Sprint 1 + NI §7.4 legacy\n");
  return 0;
}
