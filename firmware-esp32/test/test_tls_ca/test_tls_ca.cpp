// GH-735 — kiểm phần logic thuần của việc nạp CA cert.
//
// Bối cảnh: HTTPS ingest và OTA trước đây gọi `setInsecure()` — chấp nhận MỌI chứng chỉ.
// Kẻ đứng giữa dựng server giả, trả bản mô tả firmware của mình kèm SHA khớp, thiết bị
// flash luôn (SHA chỉ chứng minh "tải đúng thứ server nói", không chứng minh server là thật).
//
// Phần chạm phần cứng (LittleFS, setCACert) không test được ở đây. Nhưng chỗ SAI NHIỀU NHẤT
// lại là phần thuần: quyết định "file này có phải CA PEM dùng được không". Một file DER nhị
// phân, một placeholder rỗng, hay JSON lỗi đều TỒN TẠI trên LittleFS — nếu chỉ kiểm
// `exists()` thì firmware sẽ nạp rác rồi bắt tay TLS thất bại một cách khó hiểu.

#include <unity.h>

#include <cstring>
#include <string>

#include "net/tls_ca.h"

namespace {

std::string ValidPem() {
  // Không cần chứng chỉ thật — chỉ cần đủ dài + có marker PEM.
  std::string s = "-----BEGIN CERTIFICATE-----\n";
  s += std::string(200, 'A');
  s += "\n-----END CERTIFICATE-----\n";
  return s;
}

void expectAccepted(const std::string& s, const char* msg) {
  TEST_ASSERT_TRUE_MESSAGE(tls::isLikelyPemCertificate(s.data(), s.size()), msg);
}

void expectRejected(const std::string& s, const char* msg) {
  TEST_ASSERT_FALSE_MESSAGE(tls::isLikelyPemCertificate(s.data(), s.size()), msg);
}

}  // namespace

void test_accepts_valid_pem() {
  expectAccepted(ValidPem(), "PEM hợp lệ phải được chấp nhận");
}

void test_accepts_pem_with_leading_comment() {
  // openssl hay chèn phần mô tả trước khối PEM — vẫn dùng được.
  std::string s = "subject=CN = Solar CA\nissuer=CN = Solar CA\n" + ValidPem();
  expectAccepted(s, "PEM có phần mô tả ở đầu vẫn hợp lệ");
}

void test_rejects_null() {
  TEST_ASSERT_FALSE(tls::isLikelyPemCertificate(nullptr, 500));
}

void test_rejects_empty() {
  expectRejected("", "chuỗi rỗng không phải CA");
}

void test_rejects_too_short_even_with_marker() {
  // Đúng ca placeholder: có marker nhưng nội dung rỗng ⇒ TLS sẽ hỏng lúc chạy.
  expectRejected("-----BEGIN CERTIFICATE-----\n-----END CERTIFICATE-----\n",
                 "PEM quá ngắn phải bị từ chối");
}

void test_rejects_long_text_without_marker() {
  expectRejected(std::string(500, 'x'), "văn bản dài nhưng không có marker thì không phải PEM");
}

void test_rejects_json_error_page() {
  // Ca thật: tải CA bằng curl mà server trả JSON lỗi, file vẫn được ghi ra.
  std::string s = "{\"error\":\"not found\",\"detail\":\"";
  s += std::string(200, 'z');
  s += "\"}";
  expectRejected(s, "JSON lỗi không phải CA PEM");
}

void test_rejects_der_binary_with_embedded_nul() {
  // Ca quan trọng nhất: file DER nhị phân có byte 0 Ở GIỮA. Nếu hàm kiểm dùng strstr
  // (dừng ở NUL đầu tiên) thì sẽ bỏ sót — đây là lý do hàm nhận cả độ dài.
  std::string s;
  s.push_back('\x30');
  s.push_back('\x82');
  s.push_back('\0');
  s += std::string(300, '\x01');
  expectRejected(s, "DER nhị phân phải bị từ chối");
}

void test_marker_after_embedded_nul_is_still_found() {
  // Ngược lại: marker nằm SAU byte 0 thì vẫn phải tìm ra (chứng minh không dùng strstr).
  std::string s = "junk";
  s.push_back('\0');
  s += ValidPem();
  expectAccepted(s, "marker sau byte NUL vẫn phải tìm được");
}

void test_describe_covers_every_status() {
  // describe() không được trả "không rõ" cho status hợp lệ — log chẩn đoán mà vô nghĩa
  // thì người vận hành không biết phải làm gì.
  const tls::CaLoadStatus all[] = {
      tls::CaLoadStatus::Ok,
      tls::CaLoadStatus::FilesystemUnavailable,
      tls::CaLoadStatus::FileMissing,
      tls::CaLoadStatus::FileUnreadable,
      tls::CaLoadStatus::NotPemFormat,
  };
  for (auto st : all) {
    const char* d = tls::describe(st);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_TRUE(std::strlen(d) > 0);
    TEST_ASSERT_TRUE_MESSAGE(std::strcmp(d, "không rõ") != 0,
                             "mọi status phải có mô tả riêng");
  }
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_accepts_valid_pem);
  RUN_TEST(test_accepts_pem_with_leading_comment);
  RUN_TEST(test_rejects_null);
  RUN_TEST(test_rejects_empty);
  RUN_TEST(test_rejects_too_short_even_with_marker);
  RUN_TEST(test_rejects_long_text_without_marker);
  RUN_TEST(test_rejects_json_error_page);
  RUN_TEST(test_rejects_der_binary_with_embedded_nul);
  RUN_TEST(test_marker_after_embedded_nul_is_still_found);
  RUN_TEST(test_describe_covers_every_status);
  return UNITY_END();
}
