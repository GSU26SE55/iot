// GH-748 — kiểm việc đọc kết quả NHẬN MỘT PHẦN nằm trong response HTTP 2xx.
//
// Lỗi gốc: firmware coi mọi 2xx là "cả batch đã vào" và không đọc thân response, trong khi
// backend trả `{ totalReceived, inserted, skipped }`. Backend nhận thiếu ⇒ firmware vẫn báo
// thành công và xoá phần còn lại khỏi hàng đợi. Việc bỏ số đo diễn ra trong im lặng.
//
// Rủi ro lớn nhất của bản sửa KHÔNG phải là parse sai JSON đẹp, mà là **cảnh báo giả**:
// firmware chỉ giữ một ĐOẠN ĐẦU của response (responseSnippet), nên JSON thường bị cắt giữa
// chừng. Nếu parser đoán bừa `inserted = 0` thì mỗi lần gửi đều la "nhận thiếu" và người vận
// hành sẽ đi tìm một sự cố không tồn tại — rồi học cách phớt lờ cảnh báo thật.

#include <unity.h>

#include <cstring>

#include "core/ingest_result.h"

namespace {

core::IngestResult parse(const char* s) {
  return core::parseIngestResult(s, std::strlen(s));
}

}  // namespace

void test_full_success_is_not_partial() {
  const auto r = parse(R"({"isSuccess":true,"statusCode":201,)"
                       R"("data":{"totalReceived":6,"inserted":6,"skipped":0}})");
  TEST_ASSERT_TRUE(r.parsed);
  TEST_ASSERT_EQUAL_INT(6, r.totalReceived);
  TEST_ASSERT_EQUAL_INT(6, r.inserted);
  TEST_ASSERT_FALSE(r.isPartial());
}

void test_partial_ingest_is_detected() {
  // ĐÂY là ca của GH-748: HTTP 201 nhưng backend chỉ nhận 4/6.
  const auto r = parse(R"({"isSuccess":true,"statusCode":201,)"
                       R"("data":{"totalReceived":6,"inserted":4,"skipped":2}})");
  TEST_ASSERT_TRUE(r.parsed);
  TEST_ASSERT_TRUE(r.isPartial());
  TEST_ASSERT_EQUAL_INT(2, r.skipped);
}

void test_zero_inserted_is_partial() {
  const auto r = parse(R"({"data":{"totalReceived":6,"inserted":0,"skipped":6}})");
  TEST_ASSERT_TRUE(r.isPartial());
}

void test_truncated_body_does_not_raise_false_alarm() {
  // Response bị cắt giữa chừng — parser PHẢI trả parsed=false, KHÔNG được đoán inserted=0.
  const auto r = parse(R"({"isSuccess":true,"data":{"totalReceived":6,"inser)");
  TEST_ASSERT_FALSE(r.parsed);
  TEST_ASSERT_FALSE(r.isPartial());
}

void test_truncated_right_after_data_key() {
  const auto r = parse(R"({"isSuccess":true,"data":)");
  TEST_ASSERT_FALSE(r.parsed);
  TEST_ASSERT_FALSE(r.isPartial());
}

void test_missing_data_object() {
  const auto r = parse(R"({"isSuccess":true,"statusCode":201,"message":"ok"})");
  TEST_ASSERT_FALSE(r.parsed);
  TEST_ASSERT_FALSE(r.isPartial());
}

void test_missing_inserted_field_is_not_parsed() {
  // Thiếu `inserted` mà mặc định 0 sẽ thành cảnh báo giả — phải coi là không đọc được.
  const auto r = parse(R"({"data":{"totalReceived":6,"skipped":0}})");
  TEST_ASSERT_FALSE(r.parsed);
  TEST_ASSERT_FALSE(r.isPartial());
}

void test_missing_skipped_defaults_to_zero_but_still_parses() {
  // `skipped` là phụ — thiếu nó vẫn tính được nhận-thiếu từ inserted/totalReceived.
  const auto r = parse(R"({"data":{"totalReceived":6,"inserted":6}})");
  TEST_ASSERT_TRUE(r.parsed);
  TEST_ASSERT_EQUAL_INT(0, r.skipped);
  TEST_ASSERT_FALSE(r.isPartial());
}

void test_empty_and_null_input() {
  TEST_ASSERT_FALSE(parse("").parsed);
  TEST_ASSERT_FALSE(core::parseIngestResult(nullptr, 10).parsed);
  TEST_ASSERT_FALSE(core::parseIngestResult("{}", 0).parsed);
}

void test_garbage_input() {
  TEST_ASSERT_FALSE(parse("khong phai json").parsed);
  TEST_ASSERT_FALSE(parse("<html>502 Bad Gateway</html>").parsed);
}

void test_zero_total_received_is_never_partial() {
  // totalReceived = 0 (batch rỗng) không được coi là "nhận thiếu".
  const auto r = parse(R"({"data":{"totalReceived":0,"inserted":0,"skipped":0}})");
  TEST_ASSERT_TRUE(r.parsed);
  TEST_ASSERT_FALSE(r.isPartial());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_full_success_is_not_partial);
  RUN_TEST(test_partial_ingest_is_detected);
  RUN_TEST(test_zero_inserted_is_partial);
  RUN_TEST(test_truncated_body_does_not_raise_false_alarm);
  RUN_TEST(test_truncated_right_after_data_key);
  RUN_TEST(test_missing_data_object);
  RUN_TEST(test_missing_inserted_field_is_not_parsed);
  RUN_TEST(test_missing_skipped_defaults_to_zero_but_still_parses);
  RUN_TEST(test_empty_and_null_input);
  RUN_TEST(test_garbage_input);
  RUN_TEST(test_zero_total_received_is_never_partial);
  return UNITY_END();
}
