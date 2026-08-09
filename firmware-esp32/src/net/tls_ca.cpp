// GH-735 — xem tls_ca.h.
//
// CHỈ chứa phần logic THUẦN (không Arduino, không LittleFS) để biên dịch được ở
// env:native và test bằng Unity.
//
// IOT3-24 — phần chạm phần cứng KHÔNG nằm ở `tls_ca_device.cpp` (file đó chưa bao
// giờ tồn tại). Mỗi đường TLS tự nạp cert trong file của nó:
//   · HTTPS/OTA → http_client.cpp::loadCaPemOnce()
//   · MQTT      → mqtt_client.cpp::loadCaCert()
// Cả hai ưu tiên `kMqttCaCert` (net/ca_cert_embedded.h) rồi mới tới LittleFS.

#include "net/tls_ca.h"

#include <cstring>

namespace tls {

namespace {
constexpr char kPemBegin[] = "-----BEGIN CERTIFICATE-----";
}  // namespace

bool isLikelyPemCertificate(const char* pem, size_t len) {
  if (pem == nullptr) return false;
  if (len < kMinCaCertLength) return false;

  // Dùng độ dài truyền vào thay vì strstr trên chuỗi kết thúc NUL: file đọc từ LittleFS
  // có thể chứa byte 0 ở giữa (khi lỡ upload file DER nhị phân), lúc đó strstr sẽ dừng
  // sớm và bỏ sót — đúng ca cần bắt.
  const size_t needle = sizeof(kPemBegin) - 1;
  if (len < needle) return false;

  for (size_t i = 0; i + needle <= len; ++i) {
    if (std::memcmp(pem + i, kPemBegin, needle) == 0) return true;
  }
  return false;
}

const char* describe(CaLoadStatus status) {
  switch (status) {
    case CaLoadStatus::Ok:                    return "ok";
    case CaLoadStatus::FilesystemUnavailable: return "LittleFS không mount được";
    case CaLoadStatus::FileMissing:           return "thiếu file CA cert trên LittleFS";
    case CaLoadStatus::FileUnreadable:        return "không đọc được file CA cert";
    case CaLoadStatus::NotPemFormat:          return "CA cert không đúng định dạng PEM";
  }
  return "không rõ";
}

}  // namespace tls
