// ==================================================================
// IOT3-49/89 — mã hoá/giải mã bảng ánh xạ pin cất trong NVS.
//
// Bảng này quyết định gateway đọc unitId Modbus nào và gắn số đo vào serial nào. Sai bảng thì
// backend VẪN trả 201 nhưng lặng lẽ bỏ reading — đúng triệu chứng GH-748
// (`[ingest] ⚠ NHẬN THIẾU: 2/4 reading vào được`). Nên mọi đường vòng lạ của bộ giải mã đều
// được chốt ở đây thay vì phát hiện ngoài hiện trường.
// ==================================================================
#include <unity.h>

#include <cstring>

#include "core/battery_map_codec.h"

namespace {

core::BatteryMapEntry makeEntry(const char* serial, uint8_t unitId, const char* code) {
  core::BatteryMapEntry e{};
  strncpy(e.serial, serial, sizeof(e.serial) - 1);
  strncpy(e.sensorSourceCode, code, sizeof(e.sensorSourceCode) - 1);
  e.unitId = unitId;
  return e;
}

void test_roundtrip_preserves_every_field() {
  core::BatteryMapEntry in[3] = {
      makeEntry("BAT-2026-REAL-001", 1, "primary"),
      makeEntry("BAT-2026-002", 2, "primary"),
      makeEntry("BAT-2026-003", 247, "redundant"),
  };

  char buf[640];
  const size_t n = core::encodeBatteryMap(in, 3, buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_STRING(
      "BAT-2026-REAL-001,1,primary;BAT-2026-002,2,primary;BAT-2026-003,247,redundant", buf);

  core::BatteryMapEntry out[8];
  TEST_ASSERT_EQUAL(3u, core::decodeBatteryMap(buf, out, 8));
  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_EQUAL_STRING(in[i].serial, out[i].serial);
    TEST_ASSERT_EQUAL_STRING(in[i].sensorSourceCode, out[i].sensorSourceCode);
    TEST_ASSERT_EQUAL(in[i].unitId, out[i].unitId);
  }
}

void test_single_entry_has_no_trailing_separator() {
  core::BatteryMapEntry in[1] = { makeEntry("BAT-1", 5, "primary") };
  char buf[64];
  TEST_ASSERT_TRUE(core::encodeBatteryMap(in, 1, buf, sizeof(buf)) > 0);
  TEST_ASSERT_EQUAL_STRING("BAT-1,5,primary", buf);
}

void test_encode_returns_empty_rather_than_truncating() {
  // Bảng cụt nguy hiểm hơn bảng trống: nhìn vẫn "có cấu hình" trong khi vài pin đã biến mất.
  core::BatteryMapEntry in[2] = {
      makeEntry("BAT-2026-REAL-001", 1, "primary"),
      makeEntry("BAT-2026-002", 2, "primary"),
  };
  char tiny[20];
  TEST_ASSERT_EQUAL(0u, core::encodeBatteryMap(in, 2, tiny, sizeof(tiny)));
  TEST_ASSERT_EQUAL_STRING("", tiny);
}

void test_validation_rejects_reserved_separators() {
  // ',' và ';' LÀ dấu phân cách — lọt vào serial là vỡ toàn bảng chứ không phải hỏng một dòng.
  TEST_ASSERT_EQUAL(core::BatteryMapError::ReservedChar,
                    core::validateBatteryMapEntry("BAT,001", 1, "primary"));
  TEST_ASSERT_EQUAL(core::BatteryMapError::ReservedChar,
                    core::validateBatteryMapEntry("BAT;001", 1, "primary"));
  TEST_ASSERT_EQUAL(core::BatteryMapError::ReservedChar,
                    core::validateBatteryMapEntry("BAT-001", 1, "pri,mary"));
}

void test_validation_rejects_bad_unit_id() {
  // Dải hợp lệ của Modbus RTU: 1–247. 0 là broadcast, 248–255 dành riêng.
  TEST_ASSERT_EQUAL(core::BatteryMapError::BadUnitId,
                    core::validateBatteryMapEntry("BAT-001", 0, "primary"));
  TEST_ASSERT_EQUAL(core::BatteryMapError::BadUnitId,
                    core::validateBatteryMapEntry("BAT-001", 248, "primary"));
  TEST_ASSERT_EQUAL(core::BatteryMapError::Ok,
                    core::validateBatteryMapEntry("BAT-001", 247, "primary"));
  TEST_ASSERT_EQUAL(core::BatteryMapError::Ok,
                    core::validateBatteryMapEntry("BAT-001", 1, "primary"));
}

void test_validation_rejects_empty_serial_but_allows_empty_source_code() {
  TEST_ASSERT_EQUAL(core::BatteryMapError::EmptySerial,
                    core::validateBatteryMapEntry("", 1, "primary"));
  TEST_ASSERT_EQUAL(core::BatteryMapError::EmptySerial,
                    core::validateBatteryMapEntry(nullptr, 1, "primary"));
  // sourceCode rỗng ⇒ dùng mặc định phía trên, không phải lỗi.
  TEST_ASSERT_EQUAL(core::BatteryMapError::Ok,
                    core::validateBatteryMapEntry("BAT-001", 1, ""));
  TEST_ASSERT_EQUAL(core::BatteryMapError::Ok,
                    core::validateBatteryMapEntry("BAT-001", 1, nullptr));
}

void test_decode_skips_bad_rows_but_keeps_good_ones() {
  // Một dòng hỏng (bản firmware cũ ghi vào) không được làm chết cả gateway.
  core::BatteryMapEntry out[8];
  const size_t n = core::decodeBatteryMap(
      "BAT-A,1,primary;"          // tốt
      ";"                          // rỗng
      "BAT-B,0,primary;"           // unitId 0 — sai
      "BAT-C;"                     // thiếu unitId
      "BAT-D,300,primary;"         // unitId ngoài dải
      "BAT-E,9,primary",           // tốt
      out, 8);
  TEST_ASSERT_EQUAL(2u, n);
  TEST_ASSERT_EQUAL_STRING("BAT-A", out[0].serial);
  TEST_ASSERT_EQUAL(1, out[0].unitId);
  TEST_ASSERT_EQUAL_STRING("BAT-E", out[1].serial);
  TEST_ASSERT_EQUAL(9, out[1].unitId);
}

void test_decode_accepts_missing_source_code() {
  core::BatteryMapEntry out[2];
  TEST_ASSERT_EQUAL(1u, core::decodeBatteryMap("BAT-A,3", out, 2));
  TEST_ASSERT_EQUAL_STRING("BAT-A", out[0].serial);
  TEST_ASSERT_EQUAL(3, out[0].unitId);
  TEST_ASSERT_EQUAL_STRING("", out[0].sensorSourceCode);
}

void test_decode_respects_max_out() {
  core::BatteryMapEntry out[2];
  TEST_ASSERT_EQUAL(2u, core::decodeBatteryMap("A,1,p;B,2,p;C,3,p;D,4,p", out, 2));
  TEST_ASSERT_EQUAL_STRING("A", out[0].serial);
  TEST_ASSERT_EQUAL_STRING("B", out[1].serial);
}

void test_decode_handles_empty_and_null() {
  core::BatteryMapEntry out[4];
  TEST_ASSERT_EQUAL(0u, core::decodeBatteryMap("", out, 4));
  TEST_ASSERT_EQUAL(0u, core::decodeBatteryMap(nullptr, out, 4));
  TEST_ASSERT_EQUAL(0u, core::decodeBatteryMap("A,1,p", nullptr, 4));
  TEST_ASSERT_EQUAL(0u, core::decodeBatteryMap("A,1,p", out, 0));
}

void test_decode_drops_oversized_serial_instead_of_truncating() {
  // Serial dài quá buffer mà bị cắt sẽ trỏ sang một pin KHÁC — tệ hơn hẳn là bỏ hẳn dòng đó.
  char line[128];
  memset(line, 'X', 60); line[60] = '\0';
  strcat(line, ",1,primary");

  core::BatteryMapEntry out[2];
  TEST_ASSERT_EQUAL(0u, core::decodeBatteryMap(line, out, 2));
}

void test_max_entries_matches_hardware_slot_count() {
  // Trùng `kMockMaxBatteries` và số dòng của `config::kBatteryMappings`. Nới số này mà không
  // đụng buffer trong bms_source.cpp là tràn stack.
  TEST_ASSERT_EQUAL(8u, core::kMaxBatteryMapEntries);
}

}  // namespace

void setUp()    {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_roundtrip_preserves_every_field);
  RUN_TEST(test_single_entry_has_no_trailing_separator);
  RUN_TEST(test_encode_returns_empty_rather_than_truncating);
  RUN_TEST(test_validation_rejects_reserved_separators);
  RUN_TEST(test_validation_rejects_bad_unit_id);
  RUN_TEST(test_validation_rejects_empty_serial_but_allows_empty_source_code);
  RUN_TEST(test_decode_skips_bad_rows_but_keeps_good_ones);
  RUN_TEST(test_decode_accepts_missing_source_code);
  RUN_TEST(test_decode_respects_max_out);
  RUN_TEST(test_decode_handles_empty_and_null);
  RUN_TEST(test_decode_drops_oversized_serial_instead_of_truncating);
  RUN_TEST(test_max_entries_matches_hardware_slot_count);
  return UNITY_END();
}
