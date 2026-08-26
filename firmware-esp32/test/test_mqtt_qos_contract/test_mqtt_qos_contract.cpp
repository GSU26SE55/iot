// GH-746 — ghim HỢP ĐỒNG QoS của đường publish MQTT.
//
// Lỗi gốc: header ghi "QoS 1" ở cả 4 hàm publish, trong khi `publishWithStats()` có nguyên
// dòng `(void)qos;` — tham số bị vứt thẳng. PubSubClient v2.8 chỉ có
// `publish(topic, payload, len, retain)`, không hề có overload QoS. Doc cũ còn tự mâu thuẫn:
// vừa ghi "QoS 1" vừa ghi "KHÔNG đợi PUBACK" — mà QoS 1 theo định nghĩa LÀ chờ PUBACK.
//
// Hệ quả: caller tin `true` nghĩa là broker đã nhận, trong khi thực tế chỉ là "đã đẩy vào
// socket TCP". Mất kết nối ngay sau đó là bản tin biến mất không dấu vết.
//
// Test này giữ hai thứ:
//   1. Cam kết ở dạng MÁY ĐỌC ĐƯỢC (`kPublishGuarantee`) thay vì nằm trong lời văn rồi trôi.
//   2. Chặn `(void)qos` quay lại — tham số giả chính là thứ khiến lời văn và hiện thực lệch
//      nhau mà không ai thấy.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "net/mqtt_client.h"

namespace {

/// Đọc cả file; trả chuỗi rỗng nếu không mở được.
std::string readFile(const char* path) {
  std::FILE* f = std::fopen(path, "rb");
  if (f == nullptr) return {};
  std::string out;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
  std::fclose(f);
  return out;
}

/// pio test chạy từ thư mục project (firmware-esp32).
std::string readSource(const char* relative) {
  std::string s = readFile(relative);
  if (s.empty()) {
    std::string alt = std::string("firmware-esp32/") + relative;
    s = readFile(alt.c_str());
  }
  return s;
}

}  // namespace

void test_publish_guarantee_is_best_effort_qos0() {
  // Cam kết máy đọc được. Ai hiện thực QoS 1 thật sẽ phải đổi hằng số này, và test đỏ nhắc
  // họ cập nhật luôn phần doc + guard bên dưới.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(net::PublishGuarantee::BestEffortQos0),
                        static_cast<int>(net::kPublishGuarantee));
}

void test_qos1_is_declared_but_not_claimed_as_current() {
  // Enum vẫn có AtLeastOnceQos1 để mô tả đích đến, nhưng KHÔNG được là giá trị hiện tại.
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(net::PublishGuarantee::AtLeastOnceQos1),
                        static_cast<int>(net::kPublishGuarantee));
}

void test_dead_qos_parameter_is_gone() {
  const std::string src = readSource("src/net/mqtt_client.cpp");
  TEST_ASSERT_TRUE_MESSAGE(!src.empty(), "không đọc được mqtt_client.cpp — test sẽ xanh giả");

  TEST_ASSERT_TRUE_MESSAGE(
      src.find("(void)qos") == std::string::npos,
      "`(void)qos` quay lại — tham số giả khiến call site tưởng mình chọn được QoS");
}

void test_header_states_the_real_guarantee() {
  const std::string hdr = readSource("src/net/mqtt_client.h");
  TEST_ASSERT_TRUE_MESSAGE(!hdr.empty(), "không đọc được mqtt_client.h — test sẽ xanh giả");

  TEST_ASSERT_TRUE_MESSAGE(
      hdr.find("kPublishGuarantee") != std::string::npos,
      "header phải nêu mức bảo đảm bằng hằng số, không chỉ bằng lời văn");
  TEST_ASSERT_TRUE_MESSAGE(
      hdr.find("BestEffortQos0") != std::string::npos,
      "header phải ghi rõ đường publish là QoS 0");
}

void test_reader_helper_actually_reads() {
  // Chốt ngược: nếu readSource() luôn trả rỗng thì 2 test quét source ở trên vô nghĩa.
  const std::string hdr = readSource("src/net/mqtt_client.h");
  TEST_ASSERT_TRUE(hdr.size() > 200);
  TEST_ASSERT_TRUE(hdr.find("mqttPublishTelemetry") != std::string::npos);

  // Và phải trả rỗng cho file không tồn tại (không phải luôn trả một chuỗi nào đó).
  TEST_ASSERT_TRUE(readSource("src/net/khong_ton_tai_dau.h").empty());
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_publish_guarantee_is_best_effort_qos0);
  RUN_TEST(test_qos1_is_declared_but_not_claimed_as_current);
  RUN_TEST(test_dead_qos_parameter_is_gone);
  RUN_TEST(test_header_states_the_real_guarantee);
  RUN_TEST(test_reader_helper_actually_reads);
  return UNITY_END();
}
