// GH-747 — kiểm luật đổi deviceCode lúc đang chạy.
//
// Lỗi gốc: CLI `set devcode` đổi deviceCode runtime rồi in "hot reloaded", trong khi
// username/password MQTT là macro compile-time và phiên đang mở vẫn giữ LWT + subscription
// của code CŨ. Sau khi đổi, thiết bị publish sang topic MỚI nhưng broker ACL khoá theo
// username CŨ ⇒ bị từ chối; lệnh downlink vẫn về topic CŨ ⇒ không bao giờ tới.
// Thiết bị câm cả hai chiều mà log báo thành công — nằm ngoài hiện trường thì rất khó truy.
//
// Quy ước backend: mqtt_username = lowercase(deviceCode).

#include <unity.h>

#include "core/identity_change_policy.h"

namespace {

int decide(const char* code, const char* user, bool mqtt = true) {
  return static_cast<int>(core::decideDeviceCodeChange(code, user, mqtt));
}

int want(core::IdentityChangeDecision d) { return static_cast<int>(d); }

}  // namespace

void test_matching_lowercase_code_is_accepted() {
  TEST_ASSERT_EQUAL_INT(want(core::IdentityChangeDecision::Accept),
                        decide("gw-esp32-mvp-001", "gw-esp32-mvp-001"));
}

void test_case_difference_is_accepted_because_backend_lowercases() {
  // Backend dùng ToLowerInvariant() nên "GW-ESP32-MVP-001" vẫn ra đúng username đó.
  TEST_ASSERT_EQUAL_INT(want(core::IdentityChangeDecision::Accept),
                        decide("GW-ESP32-MVP-001", "gw-esp32-mvp-001"));
  TEST_ASSERT_EQUAL_INT(want(core::IdentityChangeDecision::Accept),
                        decide("Gw-EsP32-MvP-001", "gw-esp32-mvp-001"));
}

void test_different_code_is_rejected() {
  // ĐÂY là ca của GH-747: đổi sang code khác mà credential vẫn cũ ⇒ câm hai chiều.
  TEST_ASSERT_EQUAL_INT(want(core::IdentityChangeDecision::RejectAclMismatch),
                        decide("gw-esp32-mvp-002", "gw-esp32-mvp-001"));
}

void test_prefix_is_not_a_match() {
  // So sánh phải là bằng nhau HOÀN TOÀN — chấp nhận tiền tố sẽ cho qua một code sai.
  TEST_ASSERT_EQUAL_INT(want(core::IdentityChangeDecision::RejectAclMismatch),
                        decide("gw-esp32-mvp-00", "gw-esp32-mvp-001"));
  TEST_ASSERT_EQUAL_INT(want(core::IdentityChangeDecision::RejectAclMismatch),
                        decide("gw-esp32-mvp-0011", "gw-esp32-mvp-001"));
}

void test_empty_and_null_are_invalid() {
  TEST_ASSERT_EQUAL_INT(want(core::IdentityChangeDecision::RejectInvalid),
                        decide("", "gw-esp32-mvp-001"));
  TEST_ASSERT_EQUAL_INT(want(core::IdentityChangeDecision::RejectInvalid),
                        decide(nullptr, "gw-esp32-mvp-001"));
}

void test_overlong_code_is_invalid() {
  char tooLong[core::kMaxDeviceCodeLen + 10];
  for (size_t i = 0; i < sizeof(tooLong) - 1; ++i) tooLong[i] = 'a';
  tooLong[sizeof(tooLong) - 1] = '\0';

  TEST_ASSERT_EQUAL_INT(want(core::IdentityChangeDecision::RejectInvalid),
                        decide(tooLong, tooLong));
}

void test_max_length_code_is_still_accepted() {
  // Đúng giới hạn thì phải qua — chặn nhầm ở biên cũng là lỗi.
  char atLimit[core::kMaxDeviceCodeLen + 1];
  for (size_t i = 0; i < core::kMaxDeviceCodeLen; ++i) atLimit[i] = 'a';
  atLimit[core::kMaxDeviceCodeLen] = '\0';

  TEST_ASSERT_EQUAL_INT(want(core::IdentityChangeDecision::Accept), decide(atLimit, atLimit));
}

void test_without_mqtt_any_valid_code_is_accepted() {
  // Build không dùng MQTT thì ACL không liên quan — chỉ cần chuỗi hợp lệ.
  TEST_ASSERT_EQUAL_INT(want(core::IdentityChangeDecision::Accept),
                        decide("bat-ky-code-nao", "gw-esp32-mvp-001", /*mqtt=*/false));
  // Nhưng chuỗi rỗng vẫn phải bị chặn.
  TEST_ASSERT_EQUAL_INT(want(core::IdentityChangeDecision::RejectInvalid),
                        decide("", "gw-esp32-mvp-001", /*mqtt=*/false));
}

void test_null_username_rejects_when_mqtt_used() {
  // Thiếu username mà vẫn dùng MQTT ⇒ không thể khẳng định ACL hợp lệ ⇒ từ chối (fail closed).
  TEST_ASSERT_EQUAL_INT(want(core::IdentityChangeDecision::RejectAclMismatch),
                        decide("gw-esp32-mvp-001", nullptr));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_matching_lowercase_code_is_accepted);
  RUN_TEST(test_case_difference_is_accepted_because_backend_lowercases);
  RUN_TEST(test_different_code_is_rejected);
  RUN_TEST(test_prefix_is_not_a_match);
  RUN_TEST(test_empty_and_null_are_invalid);
  RUN_TEST(test_overlong_code_is_invalid);
  RUN_TEST(test_max_length_code_is_still_accepted);
  RUN_TEST(test_without_mqtt_any_valid_code_is_accepted);
  RUN_TEST(test_null_username_rejects_when_mqtt_used);
  return UNITY_END();
}
