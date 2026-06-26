// ==================================================================
// Sprint 6 — S6-FW-01/02: Environmental incident reporter implementation.
// ==================================================================
#include "sensor/environmental_incident.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#include "config/device_identity.h"
#include "net/http_client.h"
#include "net/time_sync.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cstring>

namespace sensor {

namespace {

constexpr size_t kSiteIdBufLen = 40;
char s_siteId[kSiteIdBufLen] = {0};

// Payload nhỏ: 6 field × ~40B + envelope → 384 dư thoải mái.
constexpr size_t kIncidentPayloadBuf = 384;

uint32_t s_reportOk   = 0;
uint32_t s_reportFail = 0;

}  // namespace

void envIncidentSetSiteId(const char* siteIdGuid) {
  if (!siteIdGuid) { s_siteId[0] = '\0'; return; }
  strncpy(s_siteId, siteIdGuid, kSiteIdBufLen - 1);
  s_siteId[kSiteIdBufLen - 1] = '\0';
  Serial.printf("[env-incident] siteId set: %s\n", s_siteId);
}

bool envIncidentReport(IncidentType type, IncidentSeverity severity, const char* notes) {
  // Backend SiteId REQUIRED — chưa provision xong thì backend reject (400). Skip.
  if (s_siteId[0] == '\0') {
    Serial.println("[env-incident] siteId chưa set (provision chưa xong?) — skip report");
    s_reportFail++;
    return false;
  }

  char ts[24];
  if (!net::isoNow(ts, sizeof(ts))) {
    Serial.println("[env-incident] NTP chưa sync — skip report");
    s_reportFail++;
    return false;
  }

  char payload[kIncidentPayloadBuf];
  JsonDocument doc;
  doc["siteId"]       = s_siteId;
  doc["incidentType"] = static_cast<int>(type);
  doc["severity"]     = static_cast<int>(severity);
  doc["reportedBy"]   = identity::deviceCode();   // ≤256 — định danh nguồn report
  doc["detectedAt"]   = ts;
  if (notes && notes[0] != '\0') {
    doc["notes"] = notes;
  }

  size_t n = serializeJson(doc, payload, sizeof(payload));
  if (n == 0 || n >= sizeof(payload)) {
    Serial.println("[env-incident] payload serialize FAIL — buffer overflow");
    s_reportFail++;
    return false;
  }

  net::PostResult res = net::httpPostJson(BACKEND_ENV_INCIDENT_PATH, payload, n);
  // 201 Created (mới) hoặc 200 OK (dedup reuse) đều là thành công.
  if (res.httpCode >= 200 && res.httpCode < 300) {
    s_reportOk++;
    Serial.printf("[env-incident] reported type=%d severity=%d (%d) [%lums]\n",
                  static_cast<int>(type), static_cast<int>(severity), res.httpCode,
                  static_cast<unsigned long>(res.durationMs));
    return true;
  }
  s_reportFail++;
  Serial.printf("[env-incident] report FAIL code=%d resp=\"%s\"\n",
                res.httpCode, res.responseSnippet);
  return false;
}

uint32_t envIncidentReportOkCount()   { return s_reportOk; }
uint32_t envIncidentReportFailCount() { return s_reportFail; }

}  // namespace sensor
