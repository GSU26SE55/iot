#pragma once
//
// GH-735 — nạp & kiểm CA cert dùng chung cho MỌI đường TLS (HTTPS ingest, OTA, MQTT).
//
// Trước đây mỗi đường tự lo TLS theo một kiểu: MQTT nạp CA đàng hoàng, còn HTTPS và OTA
// gọi thẳng `setInsecure()` — chấp nhận mọi chứng chỉ. Hệ quả: kẻ đứng giữa dựng được
// server giả, trả về bản mô tả firmware của mình kèm SHA khớp, và thiết bị sẽ flash
// firmware đó. Ba bản sao logic TLS là lý do một trong ba bị bỏ quên.
//
// File này gom về một chỗ. Phần kiểm định dạng PEM tách riêng thành hàm thuần
// (`isLikelyPemCertificate`) để test được ở env:native — không cần phần cứng.
//
// IOT3-24 — nơi NẠP cert nằm ở đâu (sửa lại ghi chú cũ nói sai):
//   - `tls_ca.cpp` CHỈ chứa phần thuần (kiểm định dạng + mô tả trạng thái).
//   - Phần chạm phần cứng KHÔNG nằm ở file riêng nào cả. Mỗi đường TLS tự nạp:
//       · HTTPS/OTA → `http_client.cpp::loadCaPemOnce()`
//       · MQTT      → `mqtt_client.cpp::loadCaCert()`
//     Cả hai đều ƯU TIÊN `kMqttCaCert` (net/ca_cert_embedded.h) rồi mới tới LittleFS.
//   - Ghi chú cũ nhắc tới `tls_ca_device.cpp` — file đó CHƯA BAO GIỜ tồn tại.
//

#include <cstddef>

namespace tls {

/// Kết quả nạp CA — phân biệt rõ để caller báo lỗi đúng nguyên nhân.
enum class CaLoadStatus {
  Ok = 0,
  FilesystemUnavailable,  // LittleFS không mount được
  FileMissing,            // chưa upload ca_cert.pem
  FileUnreadable,
  NotPemFormat,           // sai định dạng (DER/JSON/rác) hoặc quá ngắn
};

/// Độ dài tối thiểu hợp lý của một CA PEM. Ngắn hơn gần như chắc chắn là file rác.
constexpr size_t kMinCaCertLength = 100;

/// <summary>
/// Kiểm chuỗi có phải PEM certificate không. Hàm THUẦN (không I/O, không phần cứng) để
/// test được ở env:native — đây là chỗ dễ sai nhất: file DER nhị phân, JSON lỗi, hay
/// placeholder rỗng đều "tồn tại" nhưng không dùng được.
/// </summary>
bool isLikelyPemCertificate(const char* pem, size_t len);

/// Mô tả trạng thái để in log/chẩn đoán.
const char* describe(CaLoadStatus status);

}  // namespace tls
