// ==================================================================
// IOT3-37 — MQTT runtime store (broker + thông tin đăng nhập + tiền tố topic), lưu NVS.
//
// Boot nạp theo thứ tự:
//   1. NVS ("mqhost"/"mqport"/"mqtls"/"mquser"/"mqpass"/"mqprefix") — do /provision cấp
//   2. Compile-time (MQTT_BROKER_HOST/PORT/USERNAME/PASSWORD trong config.h) — chỉ là FALLBACK
//
// Đây là mấu chốt của Phương án A: trước đây bốn giá trị này nhúng cứng trong config.h nên MỖI
// THIẾT BỊ MỘT BẢN BUILD. Nay backend cấp lúc chạy, kỹ thuật viên chỉ còn nạp deviceCode + apiKey.
//
// ⚠️ TLS vẫn là COMPILE-TIME (`MQTT_USE_TLS`), KHÔNG runtime — quyết định Q2 của sprint.
//    Lý do: `WiFiClientSecure` và `WiFiClient` là HAI KIỂU khác nhau, chọn lúc biên dịch qua
//    `#if` trong mqtt_client.cpp. Runtime hoá chỉ để đổi một cờ là không đáng. Trường `useTls`
//    backend gửi xuống vẫn được LƯU và ĐỐI CHIẾU để cảnh báo khi lệch với bản build.
// ==================================================================
#pragma once
#include <cstddef>

#include "core/net_config_rules.h"

namespace mqttcfg {

constexpr size_t kHostBufLen   = core::kMaxMqttHostChars + 1;
constexpr size_t kUserBufLen   = core::kMaxMqttUserChars + 1;
constexpr size_t kPassBufLen   = core::kMaxMqttPassChars + 1;
constexpr size_t kPrefixBufLen = core::kMaxMqttPrefixChars + 1;

/// Nạp từ NVS, thiếu thì lấy compile-time. Gọi trong setup(), SAU storage::nvsBegin()
/// và SAU identity::identityBegin() (cần deviceCode để suy tiền tố topic dự phòng).
void begin();

const char* host();
int         port();
/// Giá trị `useTls` backend gửi xuống. CHỈ để đối chiếu — đường TLS thật do `MQTT_USE_TLS` quyết.
bool        brokerWantsTls();
const char* username();
const char* password();

/// <summary>
/// Tiền tố topic thiết bị PHẢI dùng, ví dụ <c>solar/gw-esp32-001</c>.
/// </summary>
/// <remarks>
/// Ưu tiên giá trị backend cấp (`mqprefix`). Chưa có thì suy từ deviceCode theo đúng quy ước
/// <c>MqttBrokerEndpointProvider.TopicPrefixFor()</c>. KHÔNG BAO GIỜ trả chuỗi rỗng — gọi trước
/// khi `begin()` cũng ra được một tiền tố dùng tạm.
/// </remarks>
const char* topicPrefix();

/// True khi có ĐỦ host + cổng + username + mật khẩu. Thiếu ⇒ `mqttBegin()` không nối broker.
bool isConfigured();

/// True nếu cấu hình đang dùng đến từ NVS (khác: fallback compile-time). Dùng cho log chẩn đoán.
bool isFromNvs();

/// <summary>
/// Ghi cấu hình do <c>/provision</c> trả về. Trả true nếu có THAY ĐỔI THẬT so với đang chạy.
/// </summary>
/// <remarks>
/// <para>
/// Trả false khi giá trị y hệt: caller (<c>provision.cpp</c>) dùng nó để quyết định có gọi
/// <c>net::mqttApplyConfig()</c> hay không — ngắt phiên MQTT đang chạy tốt chỉ để dựng lại y
/// nguyên là tự tạo khoảng mất kết nối vô cớ.
/// </para>
/// <para>
/// <paramref name="prefix"/> để <c>nullptr</c> thì tự suy từ deviceCode.
/// Mọi tham số đều được kiểm trước khi ghi; sai một trường là BỎ CẢ LƯỢT (không ghi nửa vời).
/// </para>
/// </remarks>
bool applyFromProvision(const char* newHost, int newPort, bool newUseTls,
                        const char* prefix, const char* user, const char* pass);

/// Đặt tay từng phần qua Serial CLI. Trả true nếu ghi được.
bool setBroker(const char* newHost, int newPort);
bool setCredential(const char* user, const char* pass);
bool setTopicPrefix(const char* prefix);

/// Xoá cấu hình MQTT khỏi NVS → quay về compile-time.
bool clear();

/// In trạng thái ra Serial, mật khẩu ĐƯỢC MASK.
void printStatus();

}  // namespace mqttcfg
