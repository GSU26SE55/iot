// ==================================================================
// IOT3-49 — bảng ánh xạ pin lúc CHẠY (serial ↔ unitId Modbus ↔ sensorSourceCode).
//
// Nạp theo thứ tự:
//   1. NVS (khoá "batmap") — do `/provision` trả về trong `batteryMappings[]`
//   2. `config::kBatteryMappings` trong `battery_mapping.h` — chỉ là FALLBACK
//
// Vì sao cần: backend ĐÃ trả `batteryMappings[]` từ IoT-2, nhưng `provision.cpp` chỉ đọc 4 trường
// và bỏ qua hoàn toàn mảng này. Hệ quả là firmware vẫn dùng bảng cứng, và khi serial trong bảng
// cứng không khớp `battery_assets` của backend thì backend **vẫn trả 201** nhưng lặng lẽ bỏ reading
// — đúng triệu chứng `[ingest] ⚠ NHẬN THIẾU: 2/4 reading vào được` (GH-748).
//
// KHÔNG chứa `batteryAssetId` và `initialCycleCount`: backend không gửi hai trường đó
// (`BatteryMappingEntry` chỉ có serial + unitId + sensorSourceCode) và từ S3-FW-04 backend tự
// resolve serial → BatteryAssetId. Hai trường ấy vẫn tra ở bảng cứng như cũ, xem `assetIdForSerial()`.
// ==================================================================
#pragma once
#include <cstddef>
#include <cstdint>

#include "core/battery_map_codec.h"

namespace batmap {

/// Nạp bảng từ NVS, trống thì dựng từ `config::kBatteryMappings`.
/// Gọi trong setup(), SAU `storage::nvsBegin()`.
void begin();

size_t count();

/// Con trỏ tới mục thứ `i`, hoặc `nullptr` nếu ngoài dải. KHÔNG giữ qua lần `applyFromProvision()`.
const core::BatteryMapEntry* at(size_t i);

const core::BatteryMapEntry* findBySerial(const char* serial);
const core::BatteryMapEntry* findByUnitId(uint8_t unitId);

/// `unitId` của pin, 0 nếu không có trong bảng.
uint8_t unitIdForSerial(const char* serial);

/// Serial của unitId, `nullptr` nếu không có.
const char* serialForUnitId(uint8_t unitId);

/// <summary>Guid asset của pin, tra ở BẢNG CỨNG (backend không gửi trường này).</summary>
/// <remarks>
/// Trả chuỗi rỗng khi không tìm thấy — KHÔNG phải lỗi. Hợp đồng production (S3-FW-04) là backend
/// resolve theo `batteryAssetSerial`; `batteryAssetId` chỉ còn là đường lui cho seed Sprint 1.
/// </remarks>
const char* assetIdForSerial(const char* serial);

/// True nếu bảng đang dùng đến từ NVS (khác: fallback bảng cứng).
bool isFromNvs();

/// <summary>Ghi bảng mới từ `/provision`. Trả true nếu có THAY ĐỔI THẬT.</summary>
/// <remarks>
/// Mảng rỗng bị TỪ CHỐI (trả false, giữ nguyên bảng cũ): backend trả `batteryMappings: []` nghĩa
/// là chưa gán pin cho gateway, chứ không phải "hãy quên hết pin đang theo dõi". Ghi đè bằng rỗng
/// sẽ làm gateway ngưng gửi số liệu và không có gì trong log giải thích tại sao.
/// </remarks>
bool applyFromProvision(const core::BatteryMapEntry* entries, size_t n);

/// Xoá khỏi NVS → quay về bảng cứng.
bool clear();

void printStatus();

}  // namespace batmap
