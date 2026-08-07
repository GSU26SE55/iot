// GH-749 — định danh quá khổ bị GHI vào NVS trước, rồi mới cắt trong RAM.
//
// Lỗi gốc, nguyên văn bản cũ:
//     if (strlen(newCode) >= kMaxDeviceCodeLen) { Serial.printf("… — truncate\n"); }
//     if (!storage::nvsPutString(kKeyDeviceCode, newCode)) return false;   // ghi NGUYÊN chuỗi
//     copySafe(s_deviceCode, sizeof(s_deviceCode), newCode);               // chỉ RAM bị cắt
//     return true;                                                         // vẫn báo thành công
//
// Ba cái sai chồng lên nhau:
//   1. Log nói "truncate" nhưng chẳng cắt gì trước khi ghi.
//   2. NVS giữ giá trị A, RAM chạy giá trị B ⇒ khởi động lại là đổi định danh.
//   3. Trả `true` nên CLI báo thành công cho một thao tác đã hỏng.
//
// Một khoá API bị cắt thì không xác thực được nữa, nên "cắt bớt" chưa bao giờ là cách cứu —
// chỉ có TỪ CHỐI mới trung thực. Nhưng đã từ chối thì phải chắc không chặn nhầm giá trị hợp
// lệ: backend cho `device_code` dài tới 64 ký tự, mà buffer cũ 64 byte chỉ chứa nổi 63.

#include <unity.h>

#include <cstring>

#include "core/identity_change_policy.h"
#include "core/identity_validation.h"

namespace {

// Cỡ buffer thật của firmware (xem identity::kMaxDeviceCodeLen = số ký tự + 1).
constexpr size_t kCodeBuf = core::kMaxDeviceCodeLen + 1;

int check(const char* v, size_t bufSize = kCodeBuf) {
  return static_cast<int>(core::validateIdentityField(v, bufSize));
}

int want(core::IdentityFieldError e) { return static_cast<int>(e); }

/// Dựng chuỗi 'a' dài đúng `n` ký tự.
void fill(char* dst, size_t n) {
  std::memset(dst, 'a', n);
  dst[n] = '\0';
}

}  // namespace

void test_normal_values_pass() {
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::Ok), check("gw-esp32-mvp-001"));
  // Khoá thật backend sinh: "iotk_" + base64url 32 byte ≈ 47 ký tự.
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::Ok),
                        check("iotk_5tRk9wQ2xLmN7pV4bY8zA1cD3eF6gH0jK2lM4nO6pQs", 129));
}

void test_max_length_value_is_accepted() {
  // Biên: backend cho device_code 64 ký tự ⇒ 64 ký tự PHẢI qua.
  // Đây chính là chỗ buffer cũ (64 byte) làm mất ký tự cuối trong im lặng.
  char atLimit[kCodeBuf + 8];
  fill(atLimit, core::kMaxDeviceCodeLen);
  TEST_ASSERT_EQUAL_INT(64, static_cast<int>(std::strlen(atLimit)));
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::Ok), check(atLimit));
}

void test_one_over_limit_is_rejected() {
  char over[kCodeBuf + 8];
  fill(over, core::kMaxDeviceCodeLen + 1);
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::TooLong), check(over));
}

void test_far_over_limit_is_rejected() {
  char over[kCodeBuf + 64];
  fill(over, sizeof(over) - 1);
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::TooLong), check(over));
}

void test_empty_and_null_rejected() {
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::Empty), check(""));
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::Empty), check(nullptr));
}

void test_crlf_is_rejected_header_injection() {
  // apiKey được nhét thẳng vào header `X-Api-Key`. Một ký tự CR/LF lọt qua là tiêm header.
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::InvalidChar), check("abc\r\nX-Evil: 1"));
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::InvalidChar), check("abc\ndef"));
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::InvalidChar), check("abc\rdef"));
}

void test_whitespace_and_control_chars_rejected() {
  // deviceCode còn ghép vào topic MQTT — khoảng trắng làm hỏng cả topic lẫn ACL.
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::InvalidChar), check("gw esp32"));
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::InvalidChar), check("gw\tesp32"));
  // Escape hex trong C++ "ăn" tham lam mọi chữ số hex phía sau, nên phải tách chuỗi:
  // viết liền "\x01esp32" sẽ ra ký tự 0x1E chứ không phải 0x01 rồi 'e'.
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::InvalidChar), check("gw\x7F" "esp32"));
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::InvalidChar), check("gw\x01" "esp32"));
}

void test_non_ascii_rejected() {
  // UTF-8 nhiều byte: backend sinh mã ASCII, còn NVS/topic thì không hứa gì về byte cao.
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::InvalidChar), check("gw-esp32-mvp-\xC3\xA9"));
}

void test_length_is_checked_before_charset() {
  // Chuỗi vừa quá dài vừa có ký tự lạ ở CUỐI phải báo TooLong — vòng lặp dừng ở biên độ dài,
  // không được đọc quá buffer để đi tìm ký tự lạ.
  char mixed[kCodeBuf + 8];
  fill(mixed, sizeof(mixed) - 2);
  mixed[sizeof(mixed) - 2] = ' ';
  mixed[sizeof(mixed) - 1] = '\0';
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::TooLong), check(mixed));
}

void test_tiny_buffer_never_accepts() {
  // Buffer 1 byte chỉ chứa nổi ký tự kết thúc ⇒ không giá trị nào vừa.
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::TooLong), check("a", 1));
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::Ok), check("a", 2));
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::TooLong), check("ab", 2));
}

void test_reboot_boundary_old_firmware_leftover_is_refused() {
  // Ca KHỞI ĐỘNG LẠI: firmware trước GH-749 đã kịp ghi một giá trị quá khổ vào NVS.
  // Đây là vị từ mà `loadValidated()` dựa vào để BỎ giá trị đó và quay về mặc định
  // compile-time. Bản thân `loadValidated` đụng NVS + Serial nên không chạy được ở env:native;
  // phần dây nối chỉ được bảo chứng bởi hai bản build esp32 trong thang kiểm.
  char leftover[kCodeBuf + 16];
  fill(leftover, kCodeBuf + 4);
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::TooLong), check(leftover));
}

void test_policy_and_storage_agree_on_the_boundary() {
  // Chống trôi: CLI (decideDeviceCodeChange) và tầng ghi phải cùng một biên. Nếu lệch, CLI
  // sẽ "duyệt" một giá trị mà setDeviceCode từ chối — người dùng thấy hai thông báo đá nhau.
  char atLimit[kCodeBuf + 8];
  fill(atLimit, core::kMaxDeviceCodeLen);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(core::IdentityChangeDecision::Accept),
                        static_cast<int>(core::decideDeviceCodeChange(atLimit, atLimit, true)));
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::Ok), check(atLimit));

  char over[kCodeBuf + 8];
  fill(over, core::kMaxDeviceCodeLen + 1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(core::IdentityChangeDecision::RejectInvalid),
                        static_cast<int>(core::decideDeviceCodeChange(over, over, true)));
  TEST_ASSERT_EQUAL_INT(want(core::IdentityFieldError::TooLong), check(over));
}

void test_policy_rejects_whitespace_too() {
  // Sau GH-749, CLI dùng chung bộ kiểm nên khoảng trắng bị chặn ngay ở tầng luật.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(core::IdentityChangeDecision::RejectInvalid),
                        static_cast<int>(core::decideDeviceCodeChange("gw esp32", "gw esp32", true)));
}

void test_describe_covers_every_error() {
  // Thông báo phải nói được vì sao bị từ chối — "false" trơ trọi thì người dùng chịu chết.
  const core::IdentityFieldError all[] = {
      core::IdentityFieldError::Ok,
      core::IdentityFieldError::Empty,
      core::IdentityFieldError::TooLong,
      core::IdentityFieldError::InvalidChar,
  };
  for (const auto e : all) {
    const char* msg = core::describeIdentityError(e);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_TRUE(std::strlen(msg) > 0);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_normal_values_pass);
  RUN_TEST(test_max_length_value_is_accepted);
  RUN_TEST(test_one_over_limit_is_rejected);
  RUN_TEST(test_far_over_limit_is_rejected);
  RUN_TEST(test_empty_and_null_rejected);
  RUN_TEST(test_crlf_is_rejected_header_injection);
  RUN_TEST(test_whitespace_and_control_chars_rejected);
  RUN_TEST(test_non_ascii_rejected);
  RUN_TEST(test_length_is_checked_before_charset);
  RUN_TEST(test_tiny_buffer_never_accepts);
  RUN_TEST(test_reboot_boundary_old_firmware_leftover_is_refused);
  RUN_TEST(test_policy_and_storage_agree_on_the_boundary);
  RUN_TEST(test_policy_rejects_whitespace_too);
  RUN_TEST(test_describe_covers_every_error);
  return UNITY_END();
}
