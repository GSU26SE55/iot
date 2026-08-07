// GH-745 — kiểm quyết định "có chạy firmware-check bây giờ không".
//
// Lỗi gốc: lệnh từ xa `trigger_ota` chỉ in log "PLACEHOLDER" rồi ACK **"ok"**. OTA thật đã
// làm xong ở Sprint 7 nhưng chỗ này không bao giờ được nối vào ⇒ người vận hành bấm "cập
// nhật ngay", thấy báo thành công, mà thiết bị vẫn ngồi đợi hết chu kỳ 1 giờ. Kiểu lỗi tệ
// nhất: hệ thống nói dối là đã làm.

#include <unity.h>

#include "core/ota_check_policy.h"

namespace {

constexpr uint32_t kHour = 3600000UL;
constexpr uint32_t kWarmup = 30000UL;

core::OtaCheckInputs base() {
  core::OtaCheckInputs in;
  in.enabled = true;
  in.verifying = false;
  in.forced = false;
  in.lastCheckMs = 1'000'000;
  in.nowMs = 1'000'000;
  in.intervalMs = kHour;
  in.warmupMs = kWarmup;
  return in;
}

int decide(const core::OtaCheckInputs& in) {
  return static_cast<int>(core::decideOtaCheck(in));
}

int expected(core::OtaCheckDecision d) { return static_cast<int>(d); }

}  // namespace

void test_forced_runs_immediately_instead_of_waiting_an_hour() {
  // ĐÂY là ca của GH-745: vừa check xong 1ms trước, nhưng có lệnh trigger_ota ⇒ chạy ngay.
  core::OtaCheckInputs in = base();
  in.forced = true;
  in.nowMs = in.lastCheckMs + 1;

  TEST_ASSERT_EQUAL_INT(expected(core::OtaCheckDecision::Run), decide(in));
}

void test_without_force_waits_for_interval() {
  core::OtaCheckInputs in = base();
  in.nowMs = in.lastCheckMs + kHour - 1;

  TEST_ASSERT_EQUAL_INT(expected(core::OtaCheckDecision::SkipTooSoon), decide(in));
}

void test_runs_when_interval_elapsed() {
  core::OtaCheckInputs in = base();
  in.nowMs = in.lastCheckMs + kHour;

  TEST_ASSERT_EQUAL_INT(expected(core::OtaCheckDecision::Run), decide(in));
}

void test_disabled_beats_forced() {
  // OTA tắt bằng cấu hình thì lệnh từ xa KHÔNG được vượt mặt.
  core::OtaCheckInputs in = base();
  in.enabled = false;
  in.forced = true;

  TEST_ASSERT_EQUAL_INT(expected(core::OtaCheckDecision::SkipDisabled), decide(in));
}

void test_verifying_beats_forced() {
  // Quan trọng nhất về AN TOÀN: đang xác minh bản vừa flash mà tải chồng bản mới là mất
  // luôn đường lùi nếu bản mới hỏng. `forced` KHÔNG được vượt qua điều kiện này.
  core::OtaCheckInputs in = base();
  in.verifying = true;
  in.forced = true;

  TEST_ASSERT_EQUAL_INT(expected(core::OtaCheckDecision::SkipVerifying), decide(in));
}

void test_warmup_blocks_first_check_after_boot() {
  core::OtaCheckInputs in = base();
  in.lastCheckMs = 0;          // chưa từng check
  in.nowMs = kWarmup - 1;

  TEST_ASSERT_EQUAL_INT(expected(core::OtaCheckDecision::SkipWarmup), decide(in));
}

void test_first_check_runs_after_warmup() {
  core::OtaCheckInputs in = base();
  in.lastCheckMs = 0;
  in.nowMs = kWarmup;

  TEST_ASSERT_EQUAL_INT(expected(core::OtaCheckDecision::Run), decide(in));
}

void test_forced_bypasses_warmup() {
  // Bấm "cập nhật ngay" vừa lúc thiết bị mới cắm điện là tình huống bình thường.
  core::OtaCheckInputs in = base();
  in.lastCheckMs = 0;
  in.nowMs = 5'000;            // còn trong warm-up
  in.forced = true;

  TEST_ASSERT_EQUAL_INT(expected(core::OtaCheckDecision::Run), decide(in));
}

void test_interval_survives_millis_rollover() {
  // Lần check cuối ngay trước mốc tràn, hiện tại đã qua mốc và đủ 1 giờ ⇒ phải chạy.
  // Nếu so bằng `now >= last + interval` thì phép cộng tràn và lịch OTA chết ~49,7 ngày.
  core::OtaCheckInputs in = base();
  in.lastCheckMs = 0U - (kHour / 2);      // nửa giờ trước khi tràn
  in.nowMs = kHour / 2;                   // nửa giờ sau khi tràn

  TEST_ASSERT_EQUAL_INT(expected(core::OtaCheckDecision::Run), decide(in));
}

void test_rollover_still_waits_when_interval_not_elapsed() {
  core::OtaCheckInputs in = base();
  in.lastCheckMs = 0U - 1000;   // 1s trước mốc tràn
  in.nowMs = 1000;              // 1s sau mốc tràn → mới trôi 2s

  TEST_ASSERT_EQUAL_INT(expected(core::OtaCheckDecision::SkipTooSoon), decide(in));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_forced_runs_immediately_instead_of_waiting_an_hour);
  RUN_TEST(test_without_force_waits_for_interval);
  RUN_TEST(test_runs_when_interval_elapsed);
  RUN_TEST(test_disabled_beats_forced);
  RUN_TEST(test_verifying_beats_forced);
  RUN_TEST(test_warmup_blocks_first_check_after_boot);
  RUN_TEST(test_first_check_runs_after_warmup);
  RUN_TEST(test_forced_bypasses_warmup);
  RUN_TEST(test_interval_survives_millis_rollover);
  RUN_TEST(test_rollover_still_waits_when_interval_not_elapsed);
  return UNITY_END();
}
