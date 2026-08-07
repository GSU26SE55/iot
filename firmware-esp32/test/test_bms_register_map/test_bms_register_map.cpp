// ==================================================================
// Sprint 5 — Native test cho bms_register_map decoders (pure C++).
//
// Run: pio test -e native -f test_bms_register_map
//
// Cover:
//   1. JBD voltage/current/temp/SOC decode với raw register sample
//   2. Signed current (discharge negative)
//   3. SOC clamping [0, 100]
//   4. Temperature bias (Kelvin → C cho JBD)
//   5. decodeChargingState mapping
//   6. decodeErrorCode bitmask → short codes (≤ 64 chars)
//   7. hasSoh / hasCycle / hasChargingState / hasErrorCode helpers
// ==================================================================
#include <unity.h>

#include "bms/bms_register_map.h"

#include <cstdio>
#include <cstring>

using namespace bms;

// ---- JBD preset decode ----

void test_jbd_voltage_decode() {
  // 0x0A28 = 2600 raw × 0.01V = 26.00V (e.g., 8S LiFePO4 fully charged)
  uint16_t raw[10] = {0};
  raw[0] = 2600;
  float v = decodeVoltage(kJbdBmsMap, raw);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 26.00f, v);
}

void test_jbd_current_charging_positive() {
  // 0x01F4 = 500 raw × 0.01A = +5.00A (charging)
  uint16_t raw[10] = {0};
  raw[1] = 500;
  float i = decodeCurrent(kJbdBmsMap, raw);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.00f, i);
}

void test_jbd_current_discharging_negative() {
  // int16 -200 = 0xFF38 raw × 0.01A = -2.00A (discharging)
  uint16_t raw[10] = {0};
  raw[1] = 0xFF38;  // -200 as int16
  float i = decodeCurrent(kJbdBmsMap, raw);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -2.00f, i);
}

void test_jbd_temperature_kelvin_to_celsius() {
  // JBD temp: Kelvin × 10 = 2982 → 298.2K → 25.05°C (offset -273.15)
  uint16_t raw[10] = {0};
  raw[4] = 2982;
  float t = decodeTemperature(kJbdBmsMap, raw);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 25.05f, t);
}

void test_jbd_soc() {
  uint16_t raw[10] = {0};
  raw[3] = 85;
  float s = decodeSoc(kJbdBmsMap, raw);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 85.0f, s);
}

void test_soc_clamping_lower() {
  // Raw 0xFFFF với scale 1.0 = 65535 → clamp 100
  uint16_t raw[10] = {0};
  raw[3] = 0xFFFF;
  float s = decodeSoc(kJbdBmsMap, raw);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, s);
}

void test_soc_clamping_upper() {
  // Raw 0 → 0.0 (lower bound)
  uint16_t raw[10] = {0};
  raw[3] = 0;
  float s = decodeSoc(kJbdBmsMap, raw);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s);
}

// ---- Daly preset (offset -40°C cho temp) ----

void test_daly_temperature_with_offset() {
  // Raw 65 - 40 = 25°C (scale 1.0, bias -40)
  uint16_t raw[8] = {0};
  raw[3] = 65;
  float t = decodeTemperature(kDalyBmsMap, raw);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, t);
}

// ---- JK-BMS Modbus V1.1 realtime decode (captured from real hardware) ----

void test_jk_realtime_values_match_hardware_capture() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 26.243f,
                           decodeJkPackVoltage(0x0000, 0x6683));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f,
                           decodeJkPackCurrent(0x0000, 0x0000));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 31.5f, decodeJkTemperature(0x013B));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.1f, decodeJkTemperature(0x012D));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 29.9f, decodeJkTemperature(0x012B));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.0f, decodeJkSoc(0x002A));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, decodeJkSoh(0x6400));
  TEST_ASSERT_EQUAL_UINT32(12617u, decodeJkUnsigned32(0x0000, 0x3149));
  TEST_ASSERT_EQUAL_UINT32(30000u, decodeJkUnsigned32(0x0000, 0x7530));
}

void test_jk_signed_current_decode() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.001f,
                           decodeJkPackCurrent(0xFFFF, 0xFFFF));
}

void test_jk_charging_state_uses_switches_and_current_direction() {
  TEST_ASSERT_EQUAL_UINT8(1, decodeJkChargingState(0x0101, 0.0f));
  TEST_ASSERT_EQUAL_UINT8(2, decodeJkChargingState(0x0101, 1.0f));
  TEST_ASSERT_EQUAL_UINT8(3, decodeJkChargingState(0x0101, -1.0f));
  TEST_ASSERT_EQUAL_UINT8(1, decodeJkChargingState(0x0000, 1.0f));
}

// ---- Charging state mapping ----

void test_charging_state_idle() {
  TEST_ASSERT_EQUAL_UINT8(1, decodeChargingState(0));
  TEST_ASSERT_EQUAL_UINT8(1, decodeChargingState(1));
}

void test_charging_state_charging() {
  TEST_ASSERT_EQUAL_UINT8(2, decodeChargingState(2));
  TEST_ASSERT_EQUAL_UINT8(2, decodeChargingState(0x10));
}

void test_charging_state_discharging() {
  TEST_ASSERT_EQUAL_UINT8(3, decodeChargingState(3));
  TEST_ASSERT_EQUAL_UINT8(3, decodeChargingState(0x20));
}

void test_charging_state_float() {
  TEST_ASSERT_EQUAL_UINT8(4, decodeChargingState(4));
  TEST_ASSERT_EQUAL_UINT8(4, decodeChargingState(0x40));
}

void test_charging_state_bypass() {
  TEST_ASSERT_EQUAL_UINT8(5, decodeChargingState(5));
  TEST_ASSERT_EQUAL_UINT8(5, decodeChargingState(0x80));
}

void test_charging_state_unknown_defaults_idle() {
  TEST_ASSERT_EQUAL_UINT8(1, decodeChargingState(0xFFFF));
}

// ---- Error code bitmask decode ----

void test_error_code_empty_bitmask() {
  char buf[64];
  size_t n = decodeErrorCode(0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_size_t(0, n);
  TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_error_code_single_flag_ovp() {
  char buf[64];
  size_t n = decodeErrorCode(0x0001, buf, sizeof(buf));  // OVP
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_EQUAL_STRING("OVP", buf);
}

void test_error_code_multiple_flags() {
  char buf[64];
  // OVP (0x0001) + OTP (0x0010) + SCD (0x0400)
  size_t n = decodeErrorCode(0x0411, buf, sizeof(buf));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_NOT_NULL(strstr(buf, "OVP"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "OTP"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "SCD"));
  TEST_ASSERT_NOT_NULL(strstr(buf, ","));  // separator
}

void test_error_code_all_flags_fits_64_chars() {
  // All 13 flags ON — phải fit trong 64 chars (backend MO §52.5 max).
  char buf[64];
  size_t n = decodeErrorCode(0xFFFF, buf, sizeof(buf));
  TEST_ASSERT_LESS_OR_EQUAL_size_t(63, strlen(buf));
  TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
}

void test_error_code_truncates_safely_on_small_buf() {
  // Buffer chỉ 8 chars — không được overflow. n ≤ 7 (cần 1 byte cho null).
  char buf[8];
  size_t n = decodeErrorCode(0xFFFF, buf, sizeof(buf));
  TEST_ASSERT_LESS_OR_EQUAL_size_t(7, n);
  TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
  // Null terminator phải có — strlen+1 ≤ buffer size (=8)
  TEST_ASSERT_LESS_OR_EQUAL_size_t(8, strlen(buf) + 1);
}

// ---- Has* helpers ----

void test_jbd_has_cycle_but_no_soh() {
  TEST_ASSERT_FALSE(hasSoh(kJbdBmsMap));      // JBD không expose SOH
  TEST_ASSERT_TRUE (hasCycle(kJbdBmsMap));
  TEST_ASSERT_FALSE(hasChargingState(kJbdBmsMap));
  TEST_ASSERT_TRUE (hasErrorCode(kJbdBmsMap));
}

void test_jk_uses_dedicated_sparse_layout() {
  TEST_ASSERT_FALSE(hasSoh(kJkBmsMap));
  TEST_ASSERT_FALSE(hasCycle(kJkBmsMap));
  TEST_ASSERT_FALSE(hasChargingState(kJkBmsMap));
  TEST_ASSERT_FALSE(hasErrorCode(kJkBmsMap));
  TEST_ASSERT_EQUAL_HEX16(0x1290, kJkPackVoltageAddress);
  TEST_ASSERT_EQUAL_HEX16(0x1298, kJkPackCurrentAddress);
  TEST_ASSERT_EQUAL_HEX16(0x12A6, kJkSocAddress);
}

void test_daly_has_minimal_fields() {
  TEST_ASSERT_FALSE(hasSoh(kDalyBmsMap));
  TEST_ASSERT_FALSE(hasCycle(kDalyBmsMap));
  TEST_ASSERT_FALSE(hasChargingState(kDalyBmsMap));
  TEST_ASSERT_FALSE(hasErrorCode(kDalyBmsMap));
}

// ---- Map preset names ----

void test_preset_names_present() {
  TEST_ASSERT_NOT_NULL(kJbdBmsMap.name);
  TEST_ASSERT_NOT_NULL(kJkBmsMap.name);
  TEST_ASSERT_NOT_NULL(kDalyBmsMap.name);
  TEST_ASSERT_NOT_NULL(kGenericBmsMap.name);
  TEST_ASSERT_GREATER_THAN(0, strlen(kJbdBmsMap.name));
}

// ---- Unity setup ----
void setUp(void)    {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();

  // JBD decode
  RUN_TEST(test_jbd_voltage_decode);
  RUN_TEST(test_jbd_current_charging_positive);
  RUN_TEST(test_jbd_current_discharging_negative);
  RUN_TEST(test_jbd_temperature_kelvin_to_celsius);
  RUN_TEST(test_jbd_soc);
  RUN_TEST(test_soc_clamping_lower);
  RUN_TEST(test_soc_clamping_upper);

  // Daly decode
  RUN_TEST(test_daly_temperature_with_offset);

  // JK-BMS Modbus V1.1 decode
  RUN_TEST(test_jk_realtime_values_match_hardware_capture);
  RUN_TEST(test_jk_signed_current_decode);
  RUN_TEST(test_jk_charging_state_uses_switches_and_current_direction);

  // Charging state
  RUN_TEST(test_charging_state_idle);
  RUN_TEST(test_charging_state_charging);
  RUN_TEST(test_charging_state_discharging);
  RUN_TEST(test_charging_state_float);
  RUN_TEST(test_charging_state_bypass);
  RUN_TEST(test_charging_state_unknown_defaults_idle);

  // Error code
  RUN_TEST(test_error_code_empty_bitmask);
  RUN_TEST(test_error_code_single_flag_ovp);
  RUN_TEST(test_error_code_multiple_flags);
  RUN_TEST(test_error_code_all_flags_fits_64_chars);
  RUN_TEST(test_error_code_truncates_safely_on_small_buf);

  // Has* helpers
  RUN_TEST(test_jbd_has_cycle_but_no_soh);
  RUN_TEST(test_jk_uses_dedicated_sparse_layout);
  RUN_TEST(test_daly_has_minimal_fields);

  // Preset name
  RUN_TEST(test_preset_names_present);

  return UNITY_END();
}
