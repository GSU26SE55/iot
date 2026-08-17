// ==================================================================
// Sprint 4 — Native test cho cmd_logic (pure decision)
//
// Run: pio test -e native -f test_cmd_logic
//
// Cover:
//   1. classifyType — case-insensitive + cả `_` và `-` variants
//   2. isValidPollingSeconds — bounds [1, 3600]
//   3. parseCommandPayload — happy path + edge cases (empty, malformed,
//      missing type, params null, alternative naming pollingIntervalSeconds)
// ==================================================================
#include <unity.h>

#include "cmd/cmd_logic.h"

#include <cstdio>
#include <cstring>

using namespace cmd::logic;

// ---- classifyType ----

void test_classify_set_interval_underscore() {
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::SetInterval),
                    static_cast<int>(classifyType("set_interval")));
}
void test_classify_set_interval_dash() {
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::SetInterval),
                    static_cast<int>(classifyType("set-interval")));
}
void test_classify_set_interval_uppercase() {
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::SetInterval),
                    static_cast<int>(classifyType("SET_INTERVAL")));
}
void test_classify_request_heartbeat_both_variants() {
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::RequestHeartbeat),
                    static_cast<int>(classifyType("request_heartbeat")));
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::RequestHeartbeat),
                    static_cast<int>(classifyType("request-heartbeat")));
}
void test_classify_trigger_ota_both_variants() {
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::TriggerOta),
                    static_cast<int>(classifyType("trigger_ota")));
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::TriggerOta),
                    static_cast<int>(classifyType("Trigger-OTA")));
}
void test_classify_empty_is_unknown() {
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::Unknown),
                    static_cast<int>(classifyType("")));
}
void test_classify_null_is_unknown() {
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::Unknown),
                    static_cast<int>(classifyType(nullptr)));
}
void test_classify_garbage_is_unknown() {
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::Unknown),
                    static_cast<int>(classifyType("reboot_now")));
}

// Regression: prefix-similar types KHÔNG được nhầm với valid types.
// Vd: "set_interval_x" (extra suffix) phải Unknown, không phải SetInterval.
void test_classify_prefix_similar_is_unknown() {
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::Unknown),
                    static_cast<int>(classifyType("set_interval_x")));
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::Unknown),
                    static_cast<int>(classifyType("set_intervals")));
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::Unknown),
                    static_cast<int>(classifyType("trigger_otax")));
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::Unknown),
                    static_cast<int>(classifyType("xset_interval")));
}

// ---- isValidPollingSeconds ----

void test_polling_bounds() {
  TEST_ASSERT_FALSE(isValidPollingSeconds(0));        // dưới min
  TEST_ASSERT_TRUE (isValidPollingSeconds(1));        // min
  TEST_ASSERT_TRUE (isValidPollingSeconds(5));        // default Sprint 1
  TEST_ASSERT_TRUE (isValidPollingSeconds(3600));     // max (1h)
  TEST_ASSERT_FALSE(isValidPollingSeconds(3601));     // trên max
  TEST_ASSERT_FALSE(isValidPollingSeconds(86400));    // 1 ngày — quá lớn
}

// ---- parseCommandPayload — edge cases ----

void test_parse_empty_payload_fails() {
  ParsedCommand r = parseCommandPayload("", 0);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_NOT_NULL(strstr(r.parseError, "empty"));
}

void test_parse_null_payload_fails() {
  ParsedCommand r = parseCommandPayload(nullptr, 10);
  TEST_ASSERT_FALSE(r.ok);
}

void test_parse_malformed_json_fails() {
  const char* j = "{not valid json";
  ParsedCommand r = parseCommandPayload(j, strlen(j));
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_GREATER_THAN(0, strlen(r.parseError));
}

void test_parse_missing_type_fails() {
  const char* j = R"({"cmdId":"abc123"})";
  ParsedCommand r = parseCommandPayload(j, strlen(j));
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_NOT_NULL(strstr(r.parseError, "type"));
}

// ---- parseCommandPayload — happy paths ----

void test_parse_set_interval_pollingSeconds() {
  const char* j = R"({"cmdId":"a1b2","type":"set_interval","params":{"pollingSeconds":10}})";
  ParsedCommand r = parseCommandPayload(j, strlen(j));
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING("a1b2", r.cmdId);
  TEST_ASSERT_EQUAL_STRING("set_interval", r.type);
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::SetInterval),
                    static_cast<int>(r.kind));
  TEST_ASSERT_TRUE(r.hasPollingSeconds);
  TEST_ASSERT_EQUAL_UINT32(10, r.pollingSeconds);
}

void test_parse_set_interval_alt_naming() {
  // Backend MO §52.4 dùng `pollingIntervalSeconds`. Accept cả 2.
  const char* j = R"({"cmdId":"x","type":"set-interval","params":{"pollingIntervalSeconds":300}})";
  ParsedCommand r = parseCommandPayload(j, strlen(j));
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::SetInterval),
                    static_cast<int>(r.kind));
  TEST_ASSERT_TRUE(r.hasPollingSeconds);
  TEST_ASSERT_EQUAL_UINT32(300, r.pollingSeconds);
}

void test_parse_set_interval_missing_pollingSeconds() {
  // params có nhưng thiếu pollingSeconds → parse OK nhưng hasPollingSeconds=false.
  const char* j = R"({"cmdId":"x","type":"set_interval","params":{"foo":"bar"}})";
  ParsedCommand r = parseCommandPayload(j, strlen(j));
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_FALSE(r.hasPollingSeconds);
}

void test_parse_request_heartbeat_no_params() {
  const char* j = R"({"cmdId":"hb-1","type":"request_heartbeat"})";
  ParsedCommand r = parseCommandPayload(j, strlen(j));
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::RequestHeartbeat),
                    static_cast<int>(r.kind));
  TEST_ASSERT_FALSE(r.hasPollingSeconds);
}

void test_parse_trigger_ota_with_url() {
  // OTA params tự do — Sprint 4 chỉ parse type, S7 sẽ parse url/version.
  const char* j = R"({"cmdId":"ota-1","type":"trigger_ota","params":{"url":"https://x","firmwareVersion":"1.2.0"}})";
  ParsedCommand r = parseCommandPayload(j, strlen(j));
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::TriggerOta),
                    static_cast<int>(r.kind));
}

void test_parse_unknown_type_returns_ok_with_unknown_kind() {
  // Parse thành công (type field hợp lệ) nhưng classify ra Unknown.
  // Dispatcher xử lý bằng "unknown" ack — không phải parse error.
  const char* j = R"({"cmdId":"x","type":"reboot_universe","params":{}})";
  ParsedCommand r = parseCommandPayload(j, strlen(j));
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::Unknown),
                    static_cast<int>(r.kind));
  TEST_ASSERT_EQUAL_STRING("reboot_universe", r.type);
}

void test_parse_missing_cmdId_returns_ok_empty_cmdId() {
  // cmdId optional. Dispatcher ack với cmdId rỗng.
  const char* j = R"({"type":"request_heartbeat"})";
  ParsedCommand r = parseCommandPayload(j, strlen(j));
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING("", r.cmdId);
}

void test_parse_params_null_handled() {
  // params: null → dispatcher set_interval sẽ thấy hasPollingSeconds=false.
  const char* j = R"({"cmdId":"x","type":"set_interval","params":null})";
  ParsedCommand r = parseCommandPayload(j, strlen(j));
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_FALSE(r.hasPollingSeconds);
}

void test_parse_long_cmdId_truncated_safely() {
  // cmdId > 64 chars → truncate, không overflow buffer.
  char j[400];
  snprintf(j, sizeof(j),
           R"({"cmdId":"%s","type":"request_heartbeat"})",
           "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghij");
  ParsedCommand r = parseCommandPayload(j, strlen(j));
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_LESS_THAN(64, strlen(r.cmdId));
}

// ---- Unity setup ----
void setUp(void)    {}
void tearDown(void) {}


// ---- Hợp đồng với dropdown "Gửi command" của web admin (phát hiện 08/08/2026) ----

/// <summary>
/// Năm loại lệnh mà dropdown admin TỪNG liệt kê đều KHÔNG được firmware hiểu.
/// </summary>
/// <remarks>
/// `frontend/src/shared/enums/iot/iot.enum.ts` chép danh sách từ XML doc của
/// `IotDeviceCommandPayloadDto` — mà doc đó chưa bao giờ khớp `classifyType`. Hậu quả: Admin chọn
/// `reboot`, backend trả 202, thiết bị nhận đúng topic rồi ack `status: "unknown"` và không làm gì.
/// Không ai phát hiện suốt nhiều tuần vì mọi tầng đều báo thành công.
///
/// Bài test này ghim lại sự thật: nếu ai đó thêm `reboot` vào `classifyType`, test đỏ và người sửa
/// buộc phải cập nhật cả ba nơi (firmware · IOT_COMMAND_TYPES · docs/api-battery.md) cùng lúc.
/// </remarks>
void test_admin_dropdown_legacy_types_are_all_unknown() {
  const char* legacy[] = { "reboot", "ota", "sample-now", "calibrate", "set-config" };
  for (size_t i = 0; i < sizeof(legacy) / sizeof(legacy[0]); ++i) {
    char msg[96];
    snprintf(msg, sizeof(msg), "\"%s\" phải là Unknown — xem ghi chú trên", legacy[i]);
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(CommandKind::Unknown),
                              static_cast<int>(classifyType(legacy[i])), msg);
  }
}

/// Ba loại DUY NHẤT firmware hiểu — `IOT_COMMAND_TYPES` phía frontend phải khớp đúng ba cái này.
void test_only_three_supported_types_exist() {
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::SetInterval),
                    static_cast<int>(classifyType("set_interval")));
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::TriggerOta),
                    static_cast<int>(classifyType("trigger_ota")));
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::RequestHeartbeat),
                    static_cast<int>(classifyType("request_heartbeat")));

  // Biến thể gạch ngang cũng phải nhận — frontend có thể gửi dạng nào cũng được.
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::SetInterval),
                    static_cast<int>(classifyType("set-interval")));
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::TriggerOta),
                    static_cast<int>(classifyType("trigger-ota")));
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::RequestHeartbeat),
                    static_cast<int>(classifyType("request-heartbeat")));
}

/// `set_interval` chỉ đọc `pollingSeconds` hoặc `pollingIntervalSeconds` — tên khác bị bỏ qua.
/// Gợi ý params ở dropdown admin phải nêu đúng một trong hai tên này.
void test_set_interval_ignores_wrongly_named_param() {
  const char* json = "{\"cmdId\":\"c1\",\"type\":\"set_interval\",\"params\":{\"interval\":5}}";
  ParsedCommand r = parseCommandPayload(json, strlen(json));
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::SetInterval), static_cast<int>(r.kind));
  TEST_ASSERT_EQUAL_UINT32(0, r.pollingSeconds);   // 0 = không đọc được → handler ack "failed"
}

// ---- set_bms_switch ----
//
void test_parse_set_bms_switch_charge_enable() {
  const char* json =
      "{\"cmdId\":\"c9\",\"type\":\"set_bms_switch\","
      "\"params\":{\"serial\":\"BAT-2026-REAL-001\",\"target\":\"charge\",\"enable\":true}}";
  ParsedCommand r = parseCommandPayload(json, strlen(json));
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL(static_cast<int>(CommandKind::SetBmsSwitch), static_cast<int>(r.kind));
  TEST_ASSERT_TRUE(r.hasSwitchParams);
  TEST_ASSERT_EQUAL_UINT8(1, r.switchTarget);
  TEST_ASSERT_TRUE(r.switchEnable);
  TEST_ASSERT_EQUAL_STRING("BAT-2026-REAL-001", r.switchSerial);
}

void test_parse_set_bms_switch_discharge_disable() {
  const char* json =
      "{\"cmdId\":\"c10\",\"type\":\"set_bms_switch\","
      "\"params\":{\"serial\":\"BAT-1\",\"target\":\"discharge\",\"enable\":false}}";
  ParsedCommand r = parseCommandPayload(json, strlen(json));
  TEST_ASSERT_TRUE(r.hasSwitchParams);
  TEST_ASSERT_EQUAL_UINT8(2, r.switchTarget);
  TEST_ASSERT_FALSE(r.switchEnable);
}

/// Target lạ hoặc thiếu serial phải bị chặn ở parser, không được lọt xuống bus Modbus.
void test_parse_set_bms_switch_rejects_bad_params() {
  const char* badTarget =
      "{\"cmdId\":\"c13\",\"type\":\"set_bms_switch\","
      "\"params\":{\"serial\":\"BAT-1\",\"target\":\"both\",\"enable\":true}}";
  TEST_ASSERT_FALSE(parseCommandPayload(badTarget, strlen(badTarget)).hasSwitchParams);

  const char* combinedTarget =
      "{\"cmdId\":\"c13-all\",\"type\":\"set_bms_switch\","
      "\"params\":{\"serial\":\"BAT-1\",\"target\":\"all\",\"enable\":true}}";
  TEST_ASSERT_FALSE(parseCommandPayload(combinedTarget, strlen(combinedTarget)).hasSwitchParams);

  const char* noSerial =
      "{\"cmdId\":\"c14\",\"type\":\"set_bms_switch\","
      "\"params\":{\"target\":\"charge\",\"enable\":true}}";
  TEST_ASSERT_FALSE(parseCommandPayload(noSerial, strlen(noSerial)).hasSwitchParams);
}

int main(int, char**) {
  UNITY_BEGIN();

  // classifyType
  RUN_TEST(test_classify_set_interval_underscore);
  RUN_TEST(test_classify_set_interval_dash);
  RUN_TEST(test_classify_set_interval_uppercase);
  RUN_TEST(test_classify_request_heartbeat_both_variants);
  RUN_TEST(test_classify_trigger_ota_both_variants);
  RUN_TEST(test_classify_empty_is_unknown);
  RUN_TEST(test_classify_null_is_unknown);
  RUN_TEST(test_classify_garbage_is_unknown);
  RUN_TEST(test_classify_prefix_similar_is_unknown);

  // isValidPollingSeconds
  RUN_TEST(test_polling_bounds);

  // parseCommandPayload — edge
  RUN_TEST(test_parse_empty_payload_fails);
  RUN_TEST(test_parse_null_payload_fails);
  RUN_TEST(test_parse_malformed_json_fails);
  RUN_TEST(test_parse_missing_type_fails);

  // parseCommandPayload — happy
  RUN_TEST(test_parse_set_interval_pollingSeconds);
  RUN_TEST(test_parse_set_interval_alt_naming);
  RUN_TEST(test_parse_set_interval_missing_pollingSeconds);
  RUN_TEST(test_parse_request_heartbeat_no_params);
  RUN_TEST(test_parse_trigger_ota_with_url);
  RUN_TEST(test_parse_unknown_type_returns_ok_with_unknown_kind);
  RUN_TEST(test_parse_missing_cmdId_returns_ok_empty_cmdId);
  RUN_TEST(test_parse_params_null_handled);
  RUN_TEST(test_parse_long_cmdId_truncated_safely);
  RUN_TEST(test_admin_dropdown_legacy_types_are_all_unknown);
  RUN_TEST(test_only_three_supported_types_exist);
  RUN_TEST(test_set_interval_ignores_wrongly_named_param);

  // parseCommandPayload — set_bms_switch
  RUN_TEST(test_parse_set_bms_switch_charge_enable);
  RUN_TEST(test_parse_set_bms_switch_discharge_disable);
  RUN_TEST(test_parse_set_bms_switch_rejects_bad_params);

  return UNITY_END();
}
