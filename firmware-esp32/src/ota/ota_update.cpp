// ==================================================================
// Sprint 7 — S7-FW-01/02 (#73/#74): OTA update + rollback implementation.
// Xem ota_update.h cho luồng tổng quan + contract backend.
//
// Robustness (sau adversarial review):
//   - Boot-counter self-healing: FW mới boot quá OTA_MAX_BOOT_ATTEMPTS lần mà chưa
//     confirm health (vd crash sớm trong setup) → rollback. Bù cho việc Arduino
//     KHÔNG bật bootloader app-rollback.
//   - Health = WiFi + NTP + MQTT broker (đúng spec §B6 "WiFi/broker"), KHÔNG phụ
//     thuộc PUT Success 2xx (tránh rollback nhầm FW tốt khi backend tạm lỗi).
//   - Report RolledBack: ghi NVS otaRb=1 → sau khi rollback về FW cũ + online lại,
//     FW cũ mới PUT RolledBack (nguyên nhân rollback thường là mất mạng → không thể
//     report ngay lúc đó).
//   - Skip known-bad version: nhớ otaBadVer → tránh re-OTA loop bản lỗi mà backend
//     vẫn đang target.
// ==================================================================
#include "ota/ota_update.h"
#include "ota/ota_decision.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#include "config/nvs_store.h"
#include "net/http_client.h"
#include "core/ota_check_policy.h"
#include "net/wifi_manager.h"
#include "net/time_sync.h"
#include "net/mqtt_client.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>

#include "esp_ota_ops.h"
#include "mbedtls/sha256.h"

#include <cstring>

namespace ota {

namespace {

// Backend IotFirmwareUpdateStatusEnum (verified capstone/backend).
enum class FwStatus : int {
  Downloading = 2,
  Installing  = 3,
  Success     = 4,
  Failed      = 5,
  Skipped     = 6,
  RolledBack  = 7,
};

// NVS keys (≤ 15 chars). Namespace "iot".
constexpr const char* kNvsPend    = "otaPend";    // uint8 1 = FW mới chờ verify
constexpr const char* kNvsBootN   = "otaBootN";   // uint8 số lần boot từ khi flash, chưa confirm
constexpr const char* kNvsRb      = "otaRb";      // uint8 1 = đã rollback, FW cũ cần report
constexpr const char* kNvsLogId   = "otaLogId";   // string updateLogId
constexpr const char* kNvsToVer   = "otaToVer";   // string target version
constexpr const char* kNvsFromVer = "otaFromVer"; // string version trước OTA
constexpr const char* kNvsBadVer  = "otaBadVer";  // string version đã xác định lỗi (skip re-OTA)
constexpr const char* kNvsFailN   = "otaFailN";   // uint8 số chu kỳ fail của otaFailVer
constexpr const char* kNvsFailVer = "otaFailVer"; // string version đang đếm fail

bool     s_enabled        = false;
bool     s_verifyMode     = false;
bool     s_reportRollback = false;
uint32_t s_verifyDeadline = 0;
uint32_t s_lastCheckMs    = 0;
uint32_t s_checkCount     = 0;
uint32_t s_updateOk       = 0;

char s_logId[40]   = {0};
char s_toVer[32]   = {0};
char s_fromVer[32] = {0};

// ---- helpers ----

void hashToHex(const uint8_t in[32], char out[65]) {
  static const char* hexd = "0123456789abcdef";
  for (int i = 0; i < 32; ++i) {
    out[i * 2]     = hexd[(in[i] >> 4) & 0xF];
    out[i * 2 + 1] = hexd[in[i] & 0xF];
  }
  out[64] = '\0';
}

// PUT firmware-update-log/{logId}. Trả true nếu backend 2xx.
bool putLog(const char* logId, FwStatus status, long bytes, const char* reason) {
  if (!logId || logId[0] == '\0') return false;

  char path[96];
  snprintf(path, sizeof(path), "%s%s", BACKEND_FW_LOG_PATH, logId);

  char body[256];
  JsonDocument doc;
  doc["status"] = static_cast<int>(status);
  if (bytes > 0) doc["bytesDownloaded"] = bytes;
  if (reason && reason[0] != '\0') doc["failureReason"] = reason;
  size_t n = serializeJson(doc, body, sizeof(body));
  if (n == 0 || n >= sizeof(body)) return false;

  net::PostResult res = net::httpPutJson(path, body, n);
  bool ok = (res.httpCode >= 200 && res.httpCode < 300);
  Serial.printf("[ota] PUT log status=%d → %d %s\n",
                static_cast<int>(status), res.httpCode, ok ? "OK" : "FAIL");
  return ok;
}

// Đánh dấu version lỗi để doCheckAndApply skip (chống re-OTA loop sau rollback).
void markBadVersion(const char* ver) {
  if (ver && ver[0] != '\0') storage::nvsPutString(kNvsBadVer, ver);
}

// Xoá toàn bộ tracking lỗi khi 1 version verify thành công (reset bộ đếm + bad marker).
void clearFailureTracking() {
  storage::nvsPutString(kNvsBadVer, "");
  storage::nvsPutString(kNvsFailVer, "");
  storage::nvsPutUInt8(kNvsFailN, 0);
}

// Đếm số chu kỳ OTA→rollback FAIL của 1 version (do mất kết nối). Tới ngưỡng
// OTA_MAX_VERSION_FAILS → mark bad. Phân biệt:
//   - Mất mạng transient: fail 1-2 lần rồi mạng về → verify OK (clear) → KHÔNG block.
//   - Binary thực sự hỏng kết nối: fail đủ N lần → mark bad → dừng re-OTA loop vô hạn.
void recordVersionFailure(const char* ver) {
  if (!ver || ver[0] == '\0') return;
  char failVer[32] = {0};
  storage::nvsGetString(kNvsFailVer, failVer, sizeof(failVer));
  uint8_t n = (strcmp(failVer, ver) == 0) ? storage::nvsGetUInt8(kNvsFailN, 0) : 0;
  n++;
  storage::nvsPutString(kNvsFailVer, ver);
  storage::nvsPutUInt8(kNvsFailN, n);
  Serial.printf("[ota] version %s fail verify lần %u/%u\n",
                ver, n, static_cast<unsigned>(OTA_MAX_VERSION_FAILS));
  if (n >= OTA_MAX_VERSION_FAILS) {
    Serial.printf("[ota] version %s fail %u lần → MARK BAD (nghi binary hỏng kết nối)\n", ver, n);
    markBadVersion(ver);
  }
}

void clearVerifyState() {
  storage::nvsPutUInt8(kNvsPend, 0);
  storage::nvsPutUInt8(kNvsBootN, 0);
  s_verifyMode = false;
}

// Rollback app-level: boot về partition KHÔNG chạy hiện tại (= FW cũ trong layout
// 2-slot ota_0/ota_1). esp_ota_set_boot_partition tự validate partition có app hợp lệ.
void rollbackToPrevious() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* prev    = esp_ota_get_next_update_partition(nullptr);
  if (prev == nullptr || prev == running) {
    Serial.println("[ota] ROLLBACK FAIL — không xác định được partition cũ (giữ FW hiện tại)");
    return;
  }
  esp_err_t err = esp_ota_set_boot_partition(prev);   // fail nếu prev không có app bootable
  if (err != ESP_OK) {
    Serial.printf("[ota] ROLLBACK set_boot_partition FAIL err=%d (giữ FW hiện tại)\n", err);
    return;
  }
  Serial.printf("[ota] ROLLBACK → boot '%s' + restart\n", prev->label);
  delay(300);
  esp_restart();
}

// Vào rollback: bật cờ report (FW cũ sẽ PUT RolledBack khi online) + reboot về FW cũ.
//   markBad=true  → nghi BINARY lỗi rõ (boot-loop) → chặn re-OTA version này NGAY.
//   markBad=false → rollback do MẤT KẾT NỐI → đếm fail per-version: transient thì cho
//                    retry (mạng về sẽ cài OK), nhưng binary hỏng kết nối fail đủ N lần
//                    → mới mark bad → chống re-OTA loop vô hạn (B3 + lỗ hổng loop).
void enterRollback(bool markBad) {
  if (markBad) markBadVersion(s_toVer);
  else         recordVersionFailure(s_toVer);
  storage::nvsPutUInt8(kNvsRb, 1);   // logId/toVer/fromVer vẫn giữ trong NVS để report
  putLog(s_logId, FwStatus::RolledBack, 0, "health/boot check failed after OTA");  // best-effort
  storage::nvsPutUInt8(kNvsPend, 0);
  storage::nvsPutUInt8(kNvsBootN, 0);
  s_verifyMode = false;
  rollbackToPrevious();
}

// Stream .bin qua HTTPS → Update partition + tính SHA-256 song song.
bool downloadAndFlash(const char* url, const char* expectedSha, const char* logId) {
  WiFiClientSecure client;
  // GH-735 — dùng chung đường nạp CA với http_client. Trước đây gọi setInsecure():
  // kẻ đứng giữa dựng được server giả trả .bin của mình kèm SHA khớp, và thiết bị
  // flash luôn — SHA chỉ chứng minh "tải đúng thứ server nói", không chứng minh
  // server đó là thật.
  if (!net::httpConfigureTls(client)) {
    Serial.println("[ota] TLS không cấu hình được — HUỶ tải firmware.");
    putLog(logId, FwStatus::Failed, 0, "TLS CA unavailable");
    return false;
  }

  HTTPClient http;
  http.setTimeout(OTA_HTTP_TIMEOUT_MS);
  http.setConnectTimeout(OTA_HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    Serial.println("[ota] download begin() FAIL");
    putLog(logId, FwStatus::Failed, 0, "download begin failed");
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[ota] download HTTP %d\n", code);
    char r[48]; snprintf(r, sizeof(r), "download http %d", code);
    putLog(logId, FwStatus::Failed, 0, r);
    http.end();
    return false;
  }

  int contentLen = http.getSize();   // -1 nếu chunked
  size_t beginSize = (contentLen > 0) ? static_cast<size_t>(contentLen) : UPDATE_SIZE_UNKNOWN;
  if (!Update.begin(beginSize)) {
    Serial.printf("[ota] Update.begin FAIL: %s\n", Update.errorString());
    putLog(logId, FwStatus::Failed, 0, "Update.begin (flash space?)");
    http.end();
    return false;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);   // 0 = SHA-256

  WiFiClient* stream = http.getStreamPtr();
  static uint8_t buf[1024];   // static: giảm áp lực stack (xem doCheckAndApply)
  size_t written = 0;
  uint32_t lastData = millis();

  while (http.connected() && (contentLen < 0 || written < static_cast<size_t>(contentLen))) {
    size_t avail = stream->available();
    if (avail > 0) {
      int r = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
      if (r <= 0) break;
      mbedtls_sha256_update(&sha, buf, r);
      if (Update.write(buf, r) != static_cast<size_t>(r)) {
        Serial.printf("[ota] Update.write FAIL: %s\n", Update.errorString());
        Update.abort();
        mbedtls_sha256_free(&sha);
        putLog(logId, FwStatus::Failed, written, "flash write failed");
        http.end();
        return false;
      }
      written += r;
      lastData = millis();
    } else {
      if (millis() - lastData > OTA_HTTP_TIMEOUT_MS) {
        Serial.println("[ota] download stall timeout");
        Update.abort();
        mbedtls_sha256_free(&sha);
        putLog(logId, FwStatus::Failed, written, "download timeout");
        http.end();
        return false;
      }
      delay(2);
    }
  }
  http.end();

  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);

  if (contentLen > 0 && written != static_cast<size_t>(contentLen)) {
    Serial.printf("[ota] size mismatch: got %u / expected %d\n",
                  static_cast<unsigned>(written), contentLen);
    Update.abort();
    putLog(logId, FwStatus::Failed, written, "size mismatch");
    return false;
  }

  char hex[65];
  hashToHex(digest, hex);
  if (!otaSha256Equal(hex, expectedSha)) {
    Serial.printf("[ota] SHA-256 MISMATCH\n  got=%s\n  exp=%s\n", hex, expectedSha);
    Update.abort();
    putLog(logId, FwStatus::Failed, written, "checksum mismatch");
    return false;
  }
  Serial.printf("[ota] SHA-256 OK (%u bytes)\n", static_cast<unsigned>(written));

  if (!Update.end(true)) {   // flush + (no setMD5 → bỏ MD5) + esp_ota_set_boot_partition
    Serial.printf("[ota] Update.end FAIL: %s\n", Update.errorString());
    putLog(logId, FwStatus::Failed, written, "Update.end failed");
    return false;
  }
  if (!Update.isFinished()) {
    Serial.println("[ota] Update not finished");
    putLog(logId, FwStatus::Failed, written, "update not finished");
    return false;
  }
  return true;
}

void doCheckAndApply() {
  s_checkCount++;

  char path[160];
  snprintf(path, sizeof(path), "%s?currentVersion=%s", BACKEND_FW_CHECK_PATH, FW_VERSION);

  // static: tránh đặt 2KB trên stack loopTask (8KB) — chuỗi gọi otaTick→doCheckAndApply
  // →downloadAndFlash cộng dồn nhiều buffer lớn. otaTick single-threaded → an toàn.
  static char resp[2048];   // đủ chứa releaseNotes markdown (tránh truncate → parse fail)
  size_t got = 0;
  net::PostResult res = net::httpGetJsonRecv(path, resp, sizeof(resp), &got);
  if (res.httpCode < 200 || res.httpCode >= 300 || got == 0) {
    Serial.printf("[ota] firmware-check FAIL code=%d\n", res.httpCode);
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, resp) != DeserializationError::Ok) {
    Serial.println("[ota] firmware-check JSON parse FAIL (response truncated?)");
    return;
  }
  JsonObject data = doc["data"];
  bool hasUpdate = data["updateAvailable"] | (data["hasUpdate"] | false);
  const char* targetVer = data["targetVersion"] | "";

  if (!otaShouldUpdate(hasUpdate, FW_VERSION, targetVer)) {
    Serial.printf("[ota] no update (current=%s)\n", FW_VERSION);
    return;
  }

  const char* url   = data["downloadUrl"] | (data["artifactUrl"] | "");
  const char* sha   = data["sha256Checksum"] | "";
  const char* logId = data["updateLogId"] | "";
  long sizeBytes    = data["artifactSizeBytes"] | 0L;

  if (url[0] == '\0' || sha[0] == '\0' || logId[0] == '\0') {
    Serial.println("[ota] update available nhưng thiếu url/sha/logId — skip");
    return;
  }

  // Skip version đã fail/rollback trước đó (backend còn target bản lỗi → tránh re-OTA loop).
  char badVer[32] = {0};
  storage::nvsGetString(kNvsBadVer, badVer, sizeof(badVer));
  if (badVer[0] != '\0' && strcmp(targetVer, badVer) == 0) {
    Serial.printf("[ota] SKIP known-bad version %s (đã rollback) — chờ release mới\n", targetVer);
    putLog(logId, FwStatus::Skipped, 0, "known-bad version (previously rolled back)");
    return;
  }

  Serial.printf("[ota] UPDATE %s → %s (%ld bytes) log=%s\n",
                FW_VERSION, targetVer, sizeBytes, logId);

  // Persist state TRƯỚC khi flash. Power-loss giữa chừng: boot partition chưa switch
  // → boot FW cũ → verify thấy version mismatch → báo Failed + clear.
  storage::nvsPutString(kNvsLogId, logId);
  storage::nvsPutString(kNvsToVer, targetVer);
  storage::nvsPutString(kNvsFromVer, FW_VERSION);
  storage::nvsPutUInt8(kNvsBootN, 0);
  storage::nvsPutUInt8(kNvsPend, 1);

  putLog(logId, FwStatus::Downloading, 0, nullptr);

  if (!downloadAndFlash(url, sha, logId)) {
    // Flash fail (checksum/write/timeout): KHÔNG mark bad → retry ở lần poll sau
    // (self-heal nếu lỗi do mạng transient). Nếu artifact server hỏng vĩnh viễn →
    // lặp tải lại mỗi OTA_CHECK_INTERVAL_MS + sinh Failed log (admin thấy → sửa release).
    Serial.println("[ota] flash FAIL — giữ FW hiện tại (sẽ retry lần poll sau)");
    storage::nvsPutUInt8(kNvsPend, 0);   // không reboot, không cần verify
    return;
  }

  putLog(logId, FwStatus::Installing, sizeBytes, nullptr);
  Serial.printf("[ota] flash OK → reboot vào %s\n", targetVer);
  delay(500);
  esp_restart();
}

// Verify-mode sau reboot: health-check (WiFi+NTP+MQTT) → Success | (deadline) rollback.
void handleVerify() {
  // Version đang chạy KHÁC target → flash không áp dụng (bootloader fallback / hỏng).
  if (strcmp(FW_VERSION, s_toVer) != 0) {
    Serial.printf("[ota] verify: running=%s != target=%s → flash không áp dụng → Failed\n",
                  FW_VERSION, s_toVer);
    markBadVersion(s_toVer);
    putLog(s_logId, FwStatus::Failed, 0, "boot version mismatch (flash not applied)");
    clearVerifyState();
    return;
  }

  // Health = kết nối được (đúng spec §B6 "WiFi/broker"). KHÔNG gắn cứng vào PUT 2xx.
  bool healthy = net::wifiIsConnected() && net::timeIsSynced() && net::mqttIsConnected();
  // Fallback: broker chưa lên nhưng HTTPS tới được backend (PUT Success 2xx) cũng tính khỏe.
  if (!healthy && net::wifiIsConnected() && net::timeIsSynced()) {
    if (putLog(s_logId, FwStatus::Success, 0, nullptr)) {
      esp_ota_mark_app_valid_cancel_rollback();
      clearFailureTracking();   // version mới OK → reset bad marker + bộ đếm fail
      clearVerifyState();
      s_updateOk++;
      Serial.printf("[ota] verify OK (HTTPS) — %s confirmed\n", s_toVer);
      return;
    }
  }

  if (healthy) {
    esp_ota_mark_app_valid_cancel_rollback();  // safe no-op nếu bootloader rollback off
    putLog(s_logId, FwStatus::Success, 0, nullptr);  // report best-effort
    clearFailureTracking();
    clearVerifyState();
    s_updateOk++;
    Serial.printf("[ota] verify OK (broker) — %s confirmed valid\n", s_toVer);
    return;
  }

  // Chưa khỏe + quá deadline → rollback (overflow-safe so sánh millis).
  if (static_cast<int32_t>(millis() - s_verifyDeadline) > 0) {
    Serial.println("[ota] health-check FAIL trong 2' → ROLLBACK (transient — không mark bad)");
    enterRollback(false);   // mất kết nối → KHÔNG mark bad, cho retry lần sau
  }
}

// FW cũ sau rollback: report RolledBack khi online lại (nguyên nhân rollback thường
// là mất mạng → không report được lúc đó).
void handleReportRollback() {
  if (!net::wifiIsConnected() || !net::timeIsSynced()) return;
  char reason[64];
  snprintf(reason, sizeof(reason), "rolled back %s→%s (health fail)", s_fromVer, s_toVer);
  if (putLog(s_logId, FwStatus::RolledBack, 0, reason)) {
    storage::nvsPutUInt8(kNvsRb, 0);
    s_reportRollback = false;
    Serial.println("[ota] đã report RolledBack lên backend");
  }
}

}  // namespace

bool otaBegin() {
#if !OTA_ENABLED
  Serial.println("[ota] disabled (OTA_ENABLED=0)");
  return false;
#else
  s_enabled = true;

  storage::nvsGetString(kNvsLogId, s_logId, sizeof(s_logId));
  storage::nvsGetString(kNvsToVer, s_toVer, sizeof(s_toVer));
  storage::nvsGetString(kNvsFromVer, s_fromVer, sizeof(s_fromVer));

  if (storage::nvsGetUInt8(kNvsPend, 0) == 1) {
    // FW mới vừa boot — đếm số lần boot chưa confirm health (self-healing chống brick).
    uint8_t bootN = storage::nvsGetUInt8(kNvsBootN, 0) + 1;
    storage::nvsPutUInt8(kNvsBootN, bootN);

    if (bootN > OTA_MAX_BOOT_ATTEMPTS) {
      Serial.printf("[ota] boot lần %u > max %u chưa confirm → ROLLBACK (nghi boot-loop binary)\n",
                    bootN, static_cast<unsigned>(OTA_MAX_BOOT_ATTEMPTS));
      enterRollback(true);   // boot-loop → nghi binary lỗi → mark bad + reboot về FW cũ
      return true;           // (không tới đây — đã restart)
    }

    s_verifyMode = true;
    s_verifyDeadline = millis() + OTA_HEALTH_TIMEOUT_MS;
    Serial.printf("[ota] boot sau OTA — VERIFY mode (target=%s, boot %u/%u, health 2')\n",
                  s_toVer, bootN, static_cast<unsigned>(OTA_MAX_BOOT_ATTEMPTS));
  } else if (storage::nvsGetUInt8(kNvsRb, 0) == 1) {
    // FW cũ sau rollback — cần report RolledBack khi online.
    s_reportRollback = true;
    Serial.printf("[ota] boot sau ROLLBACK (%s) — sẽ report khi online\n", s_fromVer);
  } else {
    Serial.printf("[ota] ready (current=%s, poll mỗi %lus)\n",
                  FW_VERSION, static_cast<unsigned long>(OTA_CHECK_INTERVAL_MS / 1000));
  }
  return true;
#endif
}

namespace {
bool        s_forceCheck = false;
const char* s_lastReject  = "";
}  // namespace

void otaTick() {
  if (!s_enabled) return;

  // Verify-mode chạy KỂ CẢ offline (cần bắt deadline rollback dù mất mạng).
  if (s_verifyMode) {
    handleVerify();
    return;
  }

  // Report RolledBack tồn đọng (FW cũ) — cần network, tự gate bên trong.
  if (s_reportRollback) {
    handleReportRollback();
    // không return — vẫn cho phép check bình thường (sẽ skip bad version)
  }

  if (!net::wifiIsConnected() || !net::timeIsSynced()) return;

  // GH-745 — quyết định tách sang core::decideOtaCheck() (thuần, test ở env:native).
  core::OtaCheckInputs in;
  in.enabled     = s_enabled;
  in.verifying   = s_verifyMode;
  in.forced      = s_forceCheck;
  in.lastCheckMs = s_lastCheckMs;
  in.nowMs       = millis();
  in.intervalMs  = OTA_CHECK_INTERVAL_MS;
  in.warmupMs    = 30000UL;   // chờ WiFi/NTP/provision ổn định sau boot

  if (core::decideOtaCheck(in) != core::OtaCheckDecision::Run) return;

  if (s_forceCheck) {
    Serial.println("[ota] check do lệnh trigger_ota (bỏ qua khoảng chờ định kỳ)");
    s_forceCheck = false;
  }
  s_lastCheckMs = in.nowMs;
  doCheckAndApply();
}

bool otaRequestCheck() {
  if (!s_enabled) {
    s_lastReject = "OTA disabled (OTA_ENABLED=0)";
    Serial.println("[ota] trigger_ota TỪ CHỐI — OTA tắt bằng cấu hình");
    return false;
  }
  if (s_verifyMode) {
    s_lastReject = "verifying previous update";
    Serial.println("[ota] trigger_ota TỪ CHỐI — đang xác minh bản vừa flash");
    return false;
  }
  s_forceCheck = true;
  s_lastReject = "";
  Serial.println("[ota] trigger_ota ĐÃ NHẬN — sẽ check ở tick kế tiếp");
  return true;
}

const char* otaLastRejectReason() { return s_lastReject; }

bool     otaInVerifyMode() { return s_verifyMode; }
uint32_t otaCheckCount()   { return s_checkCount; }
uint32_t otaUpdateOkCount(){ return s_updateOk; }

}  // namespace ota
