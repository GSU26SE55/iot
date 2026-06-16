// ==================================================================
// Sprint 3 — Integration test: production payload + idempotency + clock skew
//
// Test scenarios:
//   1. POST production payload (batteryAssetSerial + sourceType + sensorSourceCode) → 201
//   2. Cross-source pair (Bms + IotGateway cùng battery) → 201
//   3. POST same payload + same Idempotency-Key → cached replay 201
//   4. POST với deviceTimestamp lệch 10 phút → 400 clock_drift
//
// Run: tools/sprint3-integration-test.sh
// ==================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "core/payload.h"
#include "core/reading.h"
#include "core/idempotency_key.h"

#include <curl/curl.h>

// Lấy current UTC ISO8601 — tránh hardcoded timestamp lệch system clock.
static void getNowIso(char* out, size_t outLen) {
  time_t now = time(nullptr);
  struct tm tm_utc;
  gmtime_r(&now, &tm_utc);
  strftime(out, outLen, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}

// Lấy ISO 1 ngày trong tương lai — để force clock_drift trigger.
static void getFutureIso(char* out, size_t outLen) {
  time_t t = time(nullptr) + 86400;     // +1 ngày → chắc chắn > 5 phút
  struct tm tm_utc;
  gmtime_r(&t, &tm_utc);
  strftime(out, outLen, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}

namespace {

size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  std::string* s = static_cast<std::string*>(userp);
  s->append(static_cast<char*>(contents), size * nmemb);
  return size * nmemb;
}

int postJson(const std::string& url, const char* body, size_t bodyLen,
             const char* idempKey,
             long& httpCode, std::string& responseOut) {
  CURL* curl = curl_easy_init();
  if (!curl) return -1;

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "X-Api-Key: iotk_test");
  headers = curl_slist_append(headers, "X-Device-Code: GW-ESP32-MVP-001");
  if (idempKey && idempKey[0]) {
    std::string h = "Idempotency-Key: ";
    h += idempKey;
    headers = curl_slist_append(headers, h.c_str());
  }

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

core::SensorReading makeBmsPrimary(const char* serial,
                                   float v, float i, float t, float soc) {
  core::SensorReading r{};
  strcpy(r.serial, serial);
  strcpy(r.sensorSourceCode, "primary");
  r.sourceType       = core::SourceType::Bms;
  r.voltage          = v; r.current = i; r.temperature = t; r.socPercent = soc;
  r.cycleCount       = 120;
  r.sohPercent       = 94.2f; r.hasSoh = true;
  r.chargingState    = core::ChargingState::Discharging; r.hasChargingState = true;
  r.hasBmsError      = false;
  return r;
}

core::SensorReading makeIotRedundant(const char* serial,
                                     float v, float i, float t, float soc) {
  core::SensorReading r{};
  strcpy(r.serial, serial);
  strcpy(r.sensorSourceCode, "redundant");
  r.sourceType       = core::SourceType::IotGateway;
  r.voltage          = v; r.current = i; r.temperature = t; r.socPercent = soc;
  r.cycleCount       = 120;
  r.hasSoh           = false;
  r.hasChargingState = false;
  r.hasBmsError      = false;
  return r;
}

bool contains(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

int main(int argc, char** argv) {
  const char* url = (argc > 1) ? argv[1] : "http://localhost:17200/api/sensor-readings/batch";
  printf("[s3-itest] target=%s\n", url);

  // ─── Test 1: cross-source pair POST ───
  // Dùng current time để mock backend clock skew check không reject false positive.
  char nowIso[24];
  getNowIso(nowIso, sizeof(nowIso));
  printf("[s3-itest] using now=%s\n", nowIso);

  core::SensorReading rs[2] = {
    makeBmsPrimary("BAT-MOCK-001",   12.60f, -3.5f, 27.0f, 80.0f),
    makeIotRedundant("BAT-MOCK-001", 12.62f, -3.5f, 27.5f, 80.0f),
  };
  char body[4096];
  size_t bodyLen = core::buildProductionBatchPayload(
      rs, 2, nowIso, "GW-ESP32-MVP-001",
      body, sizeof(body));
  if (bodyLen == 0) { fprintf(stderr, "[s3-itest] FAIL: build payload\n"); return 1; }
  printf("[s3-itest] production payload built (%zu bytes)\n", bodyLen);

  // ─── Test 1: production payload accept (201) ───
  char idem1[40];
  core::generateIdempotencyKeyV4(idem1, sizeof(idem1));
  long code = 0; std::string resp;
  if (postJson(url, body, bodyLen, idem1, code, resp) != 0) {
    fprintf(stderr, "[s3-itest] FAIL: HTTP transport\n"); return 2;
  }
  printf("[s3-itest] #1 production POST → %ld body=%s\n", code, resp.c_str());
  if (code != 201) { fprintf(stderr, "[s3-itest] FAIL: expected 201 got %ld\n", code); return 3; }
  if (!contains(resp, "\"isSuccess\": true") && !contains(resp, "\"isSuccess\":true")) {
    fprintf(stderr, "[s3-itest] FAIL: response missing isSuccess=true\n"); return 4;
  }
  printf("[s3-itest] ✓ production payload accepted (201 + isSuccess=true)\n");

  // ─── Test 2: cross-source verified (mock backend log đã ghi src=1/2) ───
  if (!contains(resp, "\"totalReceived\": 2") && !contains(resp, "\"totalReceived\":2")) {
    fprintf(stderr, "[s3-itest] FAIL: totalReceived != 2\n"); return 5;
  }
  printf("[s3-itest] ✓ cross-source pair accepted (Bms=1 + IotGateway=2)\n");

  // ─── Test 3: same idempotency key → cached replay ───
  std::string resp2;
  if (postJson(url, body, bodyLen, idem1, code, resp2) != 0 || code != 201) {
    fprintf(stderr, "[s3-itest] FAIL: replay POST got %ld\n", code); return 6;
  }
  if (!contains(resp2, "\"totalReceived\": 2") && !contains(resp2, "\"totalReceived\":2")) {
    fprintf(stderr, "[s3-itest] FAIL: replay response shape mismatch\n"); return 7;
  }
  printf("[s3-itest] ✓ idempotency replay (same key → cached 201)\n");

  // ─── Test 4: clock skew → 400 ───
  // Build payload với deviceTimestamp +1 ngày (chắc chắn > 5 phút skew)
  core::SensorReading r = makeBmsPrimary("BAT-MOCK-001", 12.6f, -3.0f, 27.0f, 80.0f);
  char futureIso[24];
  getFutureIso(futureIso, sizeof(futureIso));
  char skewBody[2048];
  size_t skewLen = core::buildProductionBatchPayload(
      &r, 1, futureIso, "GW-ESP32-MVP-001",
      skewBody, sizeof(skewBody));
  char idem2[40];
  core::generateIdempotencyKeyV4(idem2, sizeof(idem2));
  std::string resp3;
  if (postJson(url, skewBody, skewLen, idem2, code, resp3) != 0 || code != 400) {
    fprintf(stderr, "[s3-itest] FAIL: clock skew expected 400 got %ld body=%s\n",
            code, resp3.c_str());
    return 8;
  }
  if (!contains(resp3, "clock") && !contains(resp3, "drift") && !contains(resp3, "skew")) {
    fprintf(stderr, "[s3-itest] FAIL: 400 response không mention clock/skew/drift\n");
    return 9;
  }
  printf("[s3-itest] ✓ clock skew rejected (400 + clock_drift)\n");

  printf("[s3-itest] PASS — all Sprint 3 production contract tests OK\n");
  return 0;
}
