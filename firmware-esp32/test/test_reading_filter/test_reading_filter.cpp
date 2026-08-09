// GH-740 — kiểm việc loại reading ĐÃ publish qua MQTT khỏi fallback HTTPS.
//
// Kịch bản trong issue: MQTT publish theo TỪNG NHÓM serial. Nhóm đầu gửi xong, nhóm sau fail
// ⇒ hàm trả false ⇒ caller rơi xuống HTTPS và gửi lại **TOÀN BỘ** batch. Nhóm đầu vì thế được
// ghi hai lần. Khoá idempotency của đường HTTPS không cứu được: nó chỉ khử trùng giữa các lần
// gửi HTTPS, còn bản ghi kia đã vào backend qua MQTT với hình dạng payload khác hẳn.

#include <unity.h>

#include <cstring>

#include "core/reading_filter.h"

namespace {

core::SensorReading make(const char* serial, float voltage) {
  core::SensorReading r{};
  std::strncpy(r.serial, serial, sizeof(r.serial) - 1);
  r.voltage = voltage;
  return r;
}

}  // namespace

void test_removes_published_group_keeps_the_rest() {
  // ĐÂY là ca của GH-740: BAT-001 đã qua MQTT, BAT-002 thì chưa.
  core::SensorReading in[] = {
      make("BAT-001", 3.7f), make("BAT-001", 3.8f),   // nhóm đã publish
      make("BAT-002", 3.6f), make("BAT-002", 3.5f),   // nhóm chưa gửi
  };
  const char* published[] = {"BAT-001"};
  core::SensorReading out[8];

  const size_t n = core::filterOutPublished(in, 4, published, 1, out, 8);

  TEST_ASSERT_EQUAL_UINT32(2, n);
  TEST_ASSERT_EQUAL_STRING("BAT-002", out[0].serial);
  TEST_ASSERT_EQUAL_STRING("BAT-002", out[1].serial);
}

void test_nothing_published_keeps_everything() {
  // MQTT tắt hẳn / fail ngay nhóm đầu ⇒ HTTPS phải gửi đủ, không được mất bản ghi nào.
  core::SensorReading in[] = {make("BAT-001", 3.7f), make("BAT-002", 3.6f)};
  core::SensorReading out[4];

  TEST_ASSERT_EQUAL_UINT32(2, core::filterOutPublished(in, 2, nullptr, 0, out, 4));
}

void test_all_published_leaves_nothing() {
  // MQTT đẩy hết ⇒ không còn gì để fallback; caller phải bỏ qua HTTPS.
  core::SensorReading in[] = {make("BAT-001", 3.7f), make("BAT-002", 3.6f)};
  const char* published[] = {"BAT-001", "BAT-002"};
  core::SensorReading out[4];

  TEST_ASSERT_EQUAL_UINT32(0, core::filterOutPublished(in, 2, published, 2, out, 4));
}

void test_empty_serial_is_kept_not_dropped() {
  // Bản ghi không định danh được: thà gửi thừa còn hơn làm mất. Nếu coi chuỗi rỗng là "đã
  // gửi" thì nó biến mất khỏi cả hai đường truyền.
  core::SensorReading in[] = {make("", 3.7f), make("BAT-002", 3.6f)};
  const char* published[] = {"BAT-001"};
  core::SensorReading out[4];

  const size_t n = core::filterOutPublished(in, 2, published, 1, out, 4);
  TEST_ASSERT_EQUAL_UINT32(2, n);
  TEST_ASSERT_EQUAL_STRING("", out[0].serial);
}

void test_partial_serial_match_is_not_a_match() {
  // "BAT-00" KHÔNG được coi là khớp "BAT-001" — so sánh phải là bằng nhau hoàn toàn,
  // nếu không sẽ âm thầm bỏ mất bản ghi của một pin khác.
  core::SensorReading in[] = {make("BAT-001", 3.7f), make("BAT-0011", 3.6f)};
  const char* published[] = {"BAT-00"};
  core::SensorReading out[4];

  TEST_ASSERT_EQUAL_UINT32(2, core::filterOutPublished(in, 2, published, 1, out, 4));
}

void test_exact_match_only_removes_that_serial() {
  core::SensorReading in[] = {make("BAT-001", 3.7f), make("BAT-0011", 3.6f)};
  const char* published[] = {"BAT-001"};
  core::SensorReading out[4];

  const size_t n = core::filterOutPublished(in, 2, published, 1, out, 4);
  TEST_ASSERT_EQUAL_UINT32(1, n);
  TEST_ASSERT_EQUAL_STRING("BAT-0011", out[0].serial);
}

void test_respects_output_capacity() {
  core::SensorReading in[] = {make("A", 1), make("B", 2), make("C", 3)};
  core::SensorReading out[2];

  TEST_ASSERT_EQUAL_UINT32(2, core::filterOutPublished(in, 3, nullptr, 0, out, 2));
}

void test_null_inputs_are_safe() {
  core::SensorReading out[2];
  TEST_ASSERT_EQUAL_UINT32(0, core::filterOutPublished(nullptr, 3, nullptr, 0, out, 2));

  core::SensorReading in[] = {make("A", 1)};
  TEST_ASSERT_EQUAL_UINT32(0, core::filterOutPublished(in, 1, nullptr, 0, nullptr, 2));
  TEST_ASSERT_EQUAL_UINT32(0, core::filterOutPublished(in, 1, nullptr, 0, out, 0));
}

void test_null_entry_inside_published_list_is_ignored() {
  core::SensorReading in[] = {make("BAT-001", 3.7f)};
  const char* published[] = {nullptr, "BAT-001"};
  core::SensorReading out[2];

  TEST_ASSERT_EQUAL_UINT32(0, core::filterOutPublished(in, 1, published, 2, out, 2));
}

void test_payload_fields_are_copied_not_just_serial() {
  // Bản ghi giữ lại phải còn nguyên số đo — lọc chứ không được làm hỏng dữ liệu.
  core::SensorReading in[] = {make("BAT-002", 3.61f)};
  core::SensorReading out[2];

  core::filterOutPublished(in, 1, nullptr, 0, out, 2);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.61f, out[0].voltage);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_removes_published_group_keeps_the_rest);
  RUN_TEST(test_nothing_published_keeps_everything);
  RUN_TEST(test_all_published_leaves_nothing);
  RUN_TEST(test_empty_serial_is_kept_not_dropped);
  RUN_TEST(test_partial_serial_match_is_not_a_match);
  RUN_TEST(test_exact_match_only_removes_that_serial);
  RUN_TEST(test_respects_output_capacity);
  RUN_TEST(test_null_inputs_are_safe);
  RUN_TEST(test_null_entry_inside_published_list_is_ignored);
  RUN_TEST(test_payload_fields_are_copied_not_just_serial);
  return UNITY_END();
}
