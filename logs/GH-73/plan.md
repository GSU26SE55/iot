# Plan — GH-73: [S7-FW-01] OTA update (esp_https_ota / Update lib)

## Metadata
- Status: PLANNING | Role: FW | Ngày: 2026-06-26
- Issue: #73 — https://github.com/GSU26SE55/iot/issues/73
- Sprint: Sprint 7
- Spec: NI §9.1, OV §B6, tasksprint S7-FW-01

## Mục tiêu
ESP32 định kỳ check firmware mới, download .bin qua HTTPS, verify SHA-256, ghi OTA
partition, reboot vào firmware mới. AC: OTA chạy thành công; reboot vào firmware mới.

## Backend contract (verified với source thật capstone/backend)
- `GET /api/iot-devices/firmware-check?currentVersion=<ver>` — auth ApiKey scope `FirmwareCheck=8`.
  Response `CommonResponse<IotFirmwareCheckDto>`: `{ data: { updateAvailable/hasUpdate,
  targetVersion, artifactUrl/downloadUrl (presigned), sha256Checksum (64 hex),
  artifactSizeBytes, updateLogId (Guid), releaseNotes, isRequired, channel } }`.
  Backend TỰ tạo IotFirmwareUpdateLog(Pending) khi có update → trả updateLogId.
  So version = chuỗi tuyệt đối (Ordinal), KHÔNG semver.
- `PUT /api/iot-devices/firmware-update-log/{id}` body `{ status:int, bytesDownloaded?, failureReason? }`.
  Enum: Pending=1, Downloading=2, Installing=3, Success=4, Failed=5, Skipped=6, RolledBack=7.
  Khi Success → backend set device.CurrentFirmwareVersion = log.ToVersion.

## Hạ tầng đã có
- Partition `default_16MB.csv` ĐÃ có OTA slots (app0/app1 = ota_0/ota_1, 6.5MB mỗi cái) → KHÔNG đổi partition.
- TLS: `setInsecure()` (pilot) — OTA download dùng cùng pattern.
- NVS store (`nvsPutString/GetString/HasKey`) — persist state qua reboot.
- `Update` library + `mbedtls/sha256.h` có sẵn trong framework (không thêm lib_deps).

## Files
| File | Action | Ghi chú |
|------|--------|---------|
| `src/ota/ota_update.h` | create | API: otaBegin/otaTick/otaCheckNow + counters |
| `src/ota/ota_update.cpp` | create | check→download→sha256→flash |
| `src/ota/ota_decision.h` | create | pure logic: otaShouldUpdate + otaSha256Equal (test native) |
| `src/net/http_client.h/.cpp` | modify | thêm `httpGetJsonRecv()` (GET + headers) |
| `include/config.h` + `config.example.h` | modify | `OTA_*` + `BACKEND_FW_CHECK_PATH` + `BACKEND_FW_LOG_PATH` |
| `src/main.cpp` | modify | wire otaBegin (setup) + otaTick (loop) |
| `test/test_ota_decision/test_ota_decision.cpp` | create | native test |

## Config thêm
```c
#define OTA_ENABLED            1
#define BACKEND_FW_CHECK_PATH  "/api/iot-devices/firmware-check"
#define BACKEND_FW_LOG_PATH    "/api/iot-devices/firmware-update-log/"  // + {id}
#define OTA_CHECK_INTERVAL_MS  3600000UL   // 1h poll
#define OTA_HEALTH_TIMEOUT_MS  120000UL    // 2 phút (rollback #74)
#define OTA_HTTP_TIMEOUT_MS    20000UL     // download timeout lớn hơn
```

## Luồng otaTick (online + throttle 1h)
```
GET firmware-check?currentVersion=FW_VERSION → parse CommonResponse<IotFirmwareCheckDto>
hasUpdate == false → return
persist NVS: otaPend=1, otaLogId, otaToVer, otaFromVer=FW_VERSION
PUT update-log Downloading
Update.begin(size); stream HTTPS .bin → Update.write(chunk) + mbedtls_sha256_update(chunk)
sha256 finalize → hex compare vs backend (case-insensitive)
  match  → PUT Installing → Update.end(true) → esp_restart()
  mismatch → Update.abort() → PUT Failed("checksum mismatch") → clear NVS otaPend
```

## Steps
- [ ] config knobs (2 file)
- [ ] http_client httpGetJsonRecv
- [ ] ota_decision.h (pure) + test_ota_decision
- [ ] ota_update.cpp/.h
- [ ] wire main.cpp
- [ ] `pio test -e native` PASS + build esp32 ×2 PASS
```
```
> ⚠ Giới hạn: chỉ verify compile + logic native. OTA flash THẬT cần ESP32 vật lý (QA #72).
