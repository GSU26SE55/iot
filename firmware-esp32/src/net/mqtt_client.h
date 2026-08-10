// ==================================================================
// Sprint 4 — S4-FW-01/02/03 (#40, #41, #42): MQTT broker client
//
// Wrapper trên PubSubClient + WiFiClientSecure cho 5 topic theo backend
// MqttTopicMap (overall.md §52.14, MqttTopicMap.cs):
//
//   solar/{deviceCode}/{batterySerial}/telemetry  — publish (S4-FW-04)
//   solar/{deviceCode}/heartbeat                  — publish (S4-FW-04)
//   solar/{deviceCode}/status                     — publish online/offline + LWT (S4-FW-02/03)
//   solar/{deviceCode}/cmd/ack                    — publish ack sau cmd (S4-FW-05)
//   solar/{deviceCode}/cmd                        — subscribe downlink (S4-FW-03)
//
// LWT (S4-FW-02): set qua mqttBegin() trước connect — broker tự push "offline"
//                 retain QoS 1 khi device disconnect bất ngờ.
//
// Sau khi connect (S4-FW-03):
//   1. Publish "online" retain QoS 1 lên status topic (override LWT "offline").
//   2. Subscribe solar/{deviceCode}/cmd QoS 1.
//
// CA cert (S4-FW-01): load từ LittleFS path MQTT_CA_CERT_PATH lúc mqttBegin().
//                     Nếu MQTT_USE_TLS=0 thì bỏ qua, dùng WiFiClient plain.
//
// Tham chiếu: tasksprint.md S4-FW-01..06, newiot.md §8.3.
// ==================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace net {

// ---- Init / lifecycle ----

// Khởi tạo MQTT client: load CA từ LittleFS (nếu TLS), setup PubSubClient
// (broker host/port + LWT topic/payload/qos/retain). KHÔNG connect ngay —
// connect xảy ra trong mqttTick() đầu tiên sau khi WiFi up.
//
// Trả false nếu LittleFS mount fail hoặc CA file thiếu (chỉ khi MQTT_USE_TLS=1).
bool mqttBegin();

// Gọi mỗi loop tick. Tự handle reconnect (backoff 5s).
// Caller PHẢI đảm bảo WiFi connected trước khi gọi (mqttTick tự skip nếu wifi down).
void mqttTick();

// Trạng thái connect hiện tại với broker.
bool mqttIsConnected();

// ---- Publish helpers (S4-FW-04) ----
//
// GH-746 — MỨC BẢO ĐẢM THẬT CỦA ĐƯỜNG PUBLISH LÀ **QoS 0**.
//
// Trước đây phần doc dưới đây ghi "QoS 1" ở cả 4 hàm, trong khi `publishWithStats()` có
// nguyên dòng `(void)qos;` — tham số bị vứt thẳng. PubSubClient v2.8 chỉ có
// `publish(topic, payload, len, retain)`, KHÔNG hề có overload QoS. Bản thân doc cũ cũng tự
// mâu thuẫn: vừa ghi "QoS 1" vừa ghi "KHÔNG đợi PUBACK" — mà QoS 1 theo định nghĩa LÀ chờ
// PUBACK.
//
// Lưu ý phân biệt hai CHIỀU (chỗ dễ nhầm nhất):
//   - Chiều VÀO  (subscribe cmd, LWT): QoS 1 — PubSubClient hỗ trợ thật.
//   - Chiều RA   (4 hàm publish dưới): QoS 0 — best-effort.
//
// Hệ quả phải biết: `true` chỉ có nghĩa "đã đẩy được vào socket TCP", KHÔNG phải "broker đã
// nhận". Mất kết nối ngay sau đó là bản tin biến mất mà không ai báo. Caller nào coi `true`
// là đã-gửi-chắc-chắn thì đang tự tạo lỗ mất dữ liệu.
//
// Muốn bảo đảm thật phải đổi thư viện (esp-mqtt / AsyncMqttClient) — việc riêng, không nằm
// trong GH-746 vốn chỉ yêu cầu "sửa contract cho khớp thực tế".

/// Mức bảo đảm của đường publish. Là HẰNG SỐ để cam kết được kiểm bằng test, thay vì nằm
/// trong lời văn rồi trôi khỏi hiện thực như trước.
enum class PublishGuarantee : uint8_t {
  /// QoS 0 — gửi một lần, không xác nhận, có thể mất.
  BestEffortQos0 = 0,
  /// QoS 1 — có PUBACK, ít nhất một lần. CHƯA hỗ trợ (cần đổi thư viện).
  AtLeastOnceQos1 = 1,
};

constexpr PublishGuarantee kPublishGuarantee = PublishGuarantee::BestEffortQos0;

// Publish telemetry cho 1 pin. Backend topic schema yêu cầu batterySerial
// trong topic path — caller phải split batch multi-source theo pin trước khi gọi.
//
// retain=false. Trả true nếu PubSubClient::publish accept (đã đẩy vào out queue,
// KHÔNG đợi PUBACK — xem kPublishGuarantee). False nếu chưa connect hoặc buffer overflow.
bool mqttPublishTelemetry(const char* batterySerial,
                          const char* payload,
                          size_t      payloadLen);

// Publish heartbeat. retain=false.
bool mqttPublishHeartbeat(const char* payload, size_t payloadLen);

// Publish status ("online" / "offline"). retain=true.
// Caller chỉ publish "online" — "offline" tự lo qua LWT khi disconnect (LWT ĐƯỢC set QoS 1).
bool mqttPublishStatus(const char* payload, size_t payloadLen);

// Publish command ack (S4-FW-05). retain=false.
bool mqttPublishCmdAck(const char* payload, size_t payloadLen);

/// GH-747 — gọi SAU khi deviceCode đổi lúc đang chạy.
///
/// Phiên MQTT đang mở mang LWT và subscription của deviceCode CŨ; topic publish thì lấy
/// deviceCode runtime. Không ép kết nối lại thì thiết bị publish sang topic mới trong khi
/// broker vẫn khoá ACL theo phiên cũ, còn lệnh downlink vẫn về topic cũ — câm cả hai chiều.
///
/// Hàm này ngắt kết nối để tick kế tiếp dựng lại LWT + topic + subscribe theo code mới.
void mqttOnIdentityChanged();

/// <summary>
/// IOT3-41 — gọi SAU khi <c>mqttcfg</c> đổi (provision trả cấu hình mới, hoặc đặt tay qua CLI).
/// </summary>
/// <remarks>
/// <para>
/// Ba việc: <c>setServer()</c> theo broker mới · ngắt phiên cũ để dựng lại LWT/subscribe ·
/// cho phép nối lại ngay thay vì đợi hết <c>MQTT_RECONNECT_INTERVAL_MS</c>.
/// </para>
/// <para>
/// Nếu <c>mqttBegin()</c> lúc khởi động đã bỏ qua vì chưa provision, hàm này sẽ **khởi tạo luôn** —
/// đó là đường duy nhất để thiết bị lên MQTT ngay trong phiên đầu tiên mà không phải reboot.
/// </para>
/// <para>
/// ⚠️ Gọi CẢ ở nhánh "đã provision từ trước" lúc boot, không chỉ nhánh vừa provision xong.
/// Bỏ sót nhánh đó thì boot lần đầu chạy đẹp còn từ lần hai trở đi âm thầm chạy HTTPS-only —
/// không log lỗi nào vì HTTPS vẫn hoạt động bình thường (IOT3-46).
/// </para>
/// </remarks>
/// <returns>true nếu MQTT đang (hoặc vừa được) khởi tạo với cấu hình dùng được.</returns>
bool mqttApplyConfig();

// ---- Subscribe callback (S4-FW-05) ----

// Signature callback handler downlink command. `payload` null-terminated,
// `len` = strlen (không tính null). Caller (command_handler) parse JSON + ack.
using CommandCallback = void (*)(const char* payload, size_t len);

// Đăng ký callback. Gọi 1 lần sau mqttBegin().
void mqttSetCommandCallback(CommandCallback cb);

// ---- Stats (Sprint 4 log) ----
uint32_t mqttPublishOkCount();
uint32_t mqttPublishFailCount();
uint32_t mqttConnectCount();        // số lần (re)connect thành công
uint32_t mqttConsecutiveFailCount(); // streak fail liên tiếp (S4-FW-06 fallback)
void     mqttResetConsecutiveFails();

/// <summary>IOT3-44 — số lần connect bị TỪ CHỐI VÌ XÁC THỰC liên tiếp (state 4 hoặc 5).</summary>
/// <remarks>
/// Đếm tách hẳn khỏi <c>mqttConsecutiveFailCount()</c> (lỗi publish) và khỏi lỗi mạng
/// (state −2/−3/−4). Phân biệt này là điểm mấu chốt: mất mạng thì chờ sẽ hết, còn sai
/// credential thì chờ bao lâu cũng vô ích — chỉ xin credential mới qua <c>/provision</c>
/// mới cứu được. Lỗi mạng xen giữa KHÔNG xoá streak, nếu không thì mạng chập chờn sẽ giữ
/// bộ đếm ở 0 mãi và thiết bị không bao giờ tự lành.
/// </remarks>
uint32_t mqttAuthFailureCount();

/// Ngưỡng khuyến nghị để đi re-provision (hiện là 5).
int      mqttAuthFailureThreshold();

/// Xoá streak — gọi sau khi đã re-provision, để lần hỏng sau đếm lại từ đầu.
void     mqttResetAuthFailures();

}  // namespace net
