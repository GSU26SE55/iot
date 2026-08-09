#pragma once
//
// IOT3-49 — mã hoá/giải mã bảng ánh xạ pin để cất vào NVS.
//
// Tách thuần khỏi `config/battery_map_runtime.cpp` (file đó chạm Preferences) để test được ở
// env:native, cùng khuôn với `core/net_config_rules.h`.
//
// Định dạng: `serial,unitId,sourceCode;serial,unitId,sourceCode;...`
//
// Vì sao KHÔNG cất nguyên JSON backend trả về:
//   1. NVS giới hạn ~4000 byte một chuỗi; JSON của 8 pin đã ~700 byte, mà mỗi lần nới hợp đồng
//      backend là lại phình — hết chỗ thì `putString` trả false và bảng âm thầm không được cập nhật.
//   2. Giải JSON lúc boot buộc phải kéo ArduinoJson vào đường khởi động sớm, trong khi ta chỉ cần
//      ba trường. Chuỗi phẳng giải bằng vòng lặp, không cấp phát, không phụ thuộc thư viện.
//

#include <cstddef>
#include <cstdint>

namespace core {

/// Số pin tối đa một gateway quản. Bằng `kMockMaxBatteries` — vượt số này là đổi cả thiết kế
/// buffer ở `bms_source.cpp`, không phải chỉ nới một hằng số.
constexpr size_t kMaxBatteryMapEntries = 8;

constexpr size_t kBatterySerialBufLen    = 40;
constexpr size_t kBatterySourceCodeBufLen = 24;

struct BatteryMapEntry {
  char    serial[kBatterySerialBufLen];
  char    sensorSourceCode[kBatterySourceCodeBufLen];
  uint8_t unitId;
};

enum class BatteryMapError : uint8_t {
  Ok = 0,
  /// Serial rỗng.
  EmptySerial = 1,
  /// Serial hoặc sourceCode dài quá buffer.
  TooLong = 2,
  /// Chứa `,` hoặc `;` — hai ký tự này LÀ dấu phân cách, lọt vào là vỡ toàn bảng.
  ReservedChar = 3,
  /// unitId ngoài [1, 247] (dải hợp lệ của Modbus RTU; 0 là broadcast, 248–255 dành riêng).
  BadUnitId = 4,
};

inline const char* describeBatteryMapError(BatteryMapError e) {
  switch (e) {
    case BatteryMapError::Ok:           return "hợp lệ";
    case BatteryMapError::EmptySerial:  return "serial rỗng";
    case BatteryMapError::TooLong:      return "serial hoặc sourceCode quá dài";
    case BatteryMapError::ReservedChar: return "chứa ký tự phân cách ',' hoặc ';'";
    case BatteryMapError::BadUnitId:    return "unitId ngoài dải Modbus [1,247]";
  }
  return "không rõ";
}

namespace detail {
inline BatteryMapError checkField(const char* v, size_t bufLen, bool allowEmpty) {
  if (v == nullptr || v[0] == '\0') return allowEmpty ? BatteryMapError::Ok
                                                      : BatteryMapError::EmptySerial;
  size_t n = 0;
  for (; v[n] != '\0'; ++n) {
    if (n >= bufLen - 1) return BatteryMapError::TooLong;
    if (v[n] == ',' || v[n] == ';') return BatteryMapError::ReservedChar;
  }
  return BatteryMapError::Ok;
}
}  // namespace detail

inline BatteryMapError validateBatteryMapEntry(const char* serial, int unitId,
                                               const char* sourceCode) {
  auto e = detail::checkField(serial, kBatterySerialBufLen, /*allowEmpty=*/false);
  if (e != BatteryMapError::Ok) return e;
  e = detail::checkField(sourceCode, kBatterySourceCodeBufLen, /*allowEmpty=*/true);
  if (e != BatteryMapError::Ok) {
    // checkField trả EmptySerial cho trường rỗng; ở đây rỗng đã được cho qua nên không thể rơi vào.
    return e;
  }
  if (unitId < 1 || unitId > 247) return BatteryMapError::BadUnitId;
  return BatteryMapError::Ok;
}

/// <summary>Nối bảng thành một chuỗi. Trả số ký tự đã ghi, 0 nếu không vừa buffer.</summary>
/// <remarks>Không vừa thì trả chuỗi RỖNG chứ không cắt cụt — bảng cụt nguy hiểm hơn bảng trống,
/// vì trông vẫn "có cấu hình" trong khi vài pin đã biến mất không dấu vết.</remarks>
inline size_t encodeBatteryMap(const BatteryMapEntry* entries, size_t count,
                               char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) return 0;
  out[0] = '\0';
  if (entries == nullptr || count == 0) return 0;

  size_t w = 0;
  for (size_t i = 0; i < count; ++i) {
    if (i > 0) {
      if (w + 1 >= outLen) { out[0] = '\0'; return 0; }
      out[w++] = ';';
    }
    for (const char* p = entries[i].serial; *p != '\0'; ++p) {
      if (w + 1 >= outLen) { out[0] = '\0'; return 0; }
      out[w++] = *p;
    }
    if (w + 1 >= outLen) { out[0] = '\0'; return 0; }
    out[w++] = ',';

    // unitId ≤ 247 ⇒ tối đa 3 chữ số.
    char num[4];
    size_t nl = 0;
    unsigned v = entries[i].unitId;
    do { num[nl++] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v != 0 && nl < 3);
    while (nl > 0) {
      if (w + 1 >= outLen) { out[0] = '\0'; return 0; }
      out[w++] = num[--nl];
    }

    if (w + 1 >= outLen) { out[0] = '\0'; return 0; }
    out[w++] = ',';
    for (const char* p = entries[i].sensorSourceCode; *p != '\0'; ++p) {
      if (w + 1 >= outLen) { out[0] = '\0'; return 0; }
      out[w++] = *p;
    }
  }
  out[w] = '\0';
  return w;
}

/// <summary>Giải chuỗi thành bảng. Trả số mục đọc được (bỏ qua mục hỏng).</summary>
/// <remarks>Bỏ qua mục hỏng thay vì vứt cả bảng: một dòng lỗi do bản firmware cũ ghi không nên
/// làm chết cả gateway. Caller nên so số trả về với số dấu `;` nếu muốn biết có mất mát.</remarks>
inline size_t decodeBatteryMap(const char* text, BatteryMapEntry* out, size_t maxOut) {
  if (text == nullptr || out == nullptr || maxOut == 0) return 0;

  size_t produced = 0;
  const char* p = text;
  while (*p != '\0' && produced < maxOut) {
    // --- trường 1: serial ---
    char serial[kBatterySerialBufLen];
    size_t n = 0;
    bool overflow = false;
    while (*p != '\0' && *p != ',' && *p != ';') {
      if (n < kBatterySerialBufLen - 1) serial[n++] = *p;
      else overflow = true;
      ++p;
    }
    serial[n] = '\0';

    // --- trường 2: unitId ---
    int unitId = 0;
    bool haveUnit = false;
    if (*p == ',') {
      ++p;
      while (*p >= '0' && *p <= '9') {
        unitId = unitId * 10 + (*p - '0');
        if (unitId > 9999) unitId = 9999;   // chặn tràn, để validate loại sau
        haveUnit = true;
        ++p;
      }
    }

    // --- trường 3: sourceCode (tuỳ chọn) ---
    char code[kBatterySourceCodeBufLen];
    code[0] = '\0';
    if (*p == ',') {
      ++p;
      size_t m = 0;
      while (*p != '\0' && *p != ';') {
        if (m < kBatterySourceCodeBufLen - 1) code[m++] = *p;
        else overflow = true;
        ++p;
      }
      code[m] = '\0';
    }

    // Nhảy tới mục kế
    while (*p != '\0' && *p != ';') ++p;
    if (*p == ';') ++p;

    if (overflow || !haveUnit) continue;
    if (validateBatteryMapEntry(serial, unitId, code) != BatteryMapError::Ok) continue;

    BatteryMapEntry& e = out[produced];
    size_t k = 0;
    for (; serial[k] != '\0'; ++k) e.serial[k] = serial[k];
    e.serial[k] = '\0';
    k = 0;
    for (; code[k] != '\0'; ++k) e.sensorSourceCode[k] = code[k];
    e.sensorSourceCode[k] = '\0';
    e.unitId = static_cast<uint8_t>(unitId);
    ++produced;
  }
  return produced;
}

}  // namespace core
