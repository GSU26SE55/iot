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

// Publish telemetry cho 1 pin. Backend topic schema yêu cầu batterySerial
// trong topic path — caller phải split batch multi-source theo pin trước khi gọi.
//
// QoS 1, retain=false. Trả true nếu PubSubClient::publish accept (đã đẩy vào
// out queue, KHÔNG đợi PUBACK). Trả false nếu chưa connect hoặc buffer overflow.
bool mqttPublishTelemetry(const char* batterySerial,
                          const char* payload,
                          size_t      payloadLen);

// Publish heartbeat. QoS 1, retain=false.
bool mqttPublishHeartbeat(const char* payload, size_t payloadLen);

// Publish status ("online" / "offline"). QoS 1, retain=true.
// Caller chỉ publish "online" — "offline" tự lo qua LWT khi disconnect.
bool mqttPublishStatus(const char* payload, size_t payloadLen);

// Publish command ack (S4-FW-05). QoS 1, retain=false.
bool mqttPublishCmdAck(const char* payload, size_t payloadLen);

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

}  // namespace net
