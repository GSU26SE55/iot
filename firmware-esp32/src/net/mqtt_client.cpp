// ==================================================================
// Sprint 4 — S4-FW-01/02/03/04/05/06: MQTT client implementation
// ==================================================================
#include "net/mqtt_client.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#include "config/device_identity.h"
#include "config/runtime_config.h"
#include "net/time_sync.h"
#include "net/ca_cert_embedded.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include <cctype>
#include <cstring>

namespace net {

namespace {

// ---- Static state ----
WiFiClient       s_plainClient;
WiFiClientSecure s_tlsClient;
PubSubClient     s_plainMqtt(s_plainClient);
PubSubClient     s_tlsMqtt(s_tlsClient);
PubSubClient*    s_mqtt = &s_plainMqtt;
CommandCallback s_userCmdCb     = nullptr;
bool            s_inited        = false;
uint32_t        s_lastReconnectMs = 0;

// Stats
uint32_t s_pubOk          = 0;
uint32_t s_pubFail        = 0;
uint32_t s_connectCount   = 0;
uint32_t s_consecutiveFail = 0;

// CA cert buffer — load 1 lần lúc begin, giữ static để TLS handshake dùng lại.
// Heap để tránh stack overflow (CA PEM ~1.5KB).
String s_caCert;

// Topic buffer — deviceCode max 64 + batterySerial max ~32 + envelope segments
// ≈ 120 chars max. Buffer 160 dư an toàn. Mỗi publish helper có buffer static
// riêng để tránh race (callback có thể publish trong khi caller đang publish).
constexpr size_t kTopicBufLen = 160;

void onMessage(char* topic, byte* payload, unsigned int length) {
  // Sprint 4 chỉ handle 1 topic = solar/{dev}/cmd (subscribe duy nhất).
  // command_handler sẽ parse JSON + publish ack lên cmd/ack.
  Serial.printf("[mqtt] RX topic=%s len=%u\n", topic, length);
  if (s_userCmdCb && length > 0) {
    // Null-terminate cho callback xài như C-string. Bound bằng MQTT_MAX_PACKET_SIZE.
    static char rxBuf[MQTT_MAX_PACKET_SIZE];
    size_t copyLen = length < (sizeof(rxBuf) - 1) ? length : (sizeof(rxBuf) - 1);
    memcpy(rxBuf, payload, copyLen);
    rxBuf[copyLen] = '\0';
    s_userCmdCb(rxBuf, copyLen);
  }
}

bool configureRuntimeTransport() {
  const runtimecfg::RuntimeConfig& config = runtimecfg::runtimeConfig();
  if (!config.mqttUseTls) {
    Serial.println("[mqtt] plain MQTT selected (LAN/development only)");
    return true;
  }

  if (kMqttCaCert[0] != '\0') {
    s_caCert = String(kMqttCaCert);
    if (s_caCert.indexOf("-----BEGIN CERTIFICATE-----") >= 0) {
      s_tlsClient.setCACert(s_caCert.c_str());
      Serial.printf("[mqtt] embedded CA configured (%u bytes)\n",
                    static_cast<unsigned>(s_caCert.length()));
      return true;
    }
  }

  if (!LittleFS.begin(true) || !LittleFS.exists(MQTT_CA_CERT_PATH)) {
    Serial.printf("[mqtt] TLS selected but CA is unavailable at %s\n",
                  MQTT_CA_CERT_PATH);
    return false;
  }
  File certificateFile = LittleFS.open(MQTT_CA_CERT_PATH, "r");
  if (!certificateFile) return false;
  s_caCert = certificateFile.readString();
  certificateFile.close();
  if (s_caCert.length() < 100 ||
      s_caCert.indexOf("-----BEGIN CERTIFICATE-----") < 0) {
    Serial.println("[mqtt] TLS CA file is invalid");
    return false;
  }
  s_tlsClient.setCACert(s_caCert.c_str());
  Serial.printf("[mqtt] CA configured from %s (%u bytes)\n", MQTT_CA_CERT_PATH,
                static_cast<unsigned>(s_caCert.length()));
  return true;
}

bool tryConnect() {
  if (!WiFi.isConnected()) return false;
  const runtimecfg::RuntimeConfig& config = runtimecfg::runtimeConfig();

  // (Re)config LWT mỗi lần connect — vì deviceCode có thể đổi qua Serial CLI
  // (S2-FW-01 hot reload). LWT topic = solar/{dev}/status, payload="offline",
  // QoS 1, retain=true (S4-FW-02 spec).
  static char willTopicBuf[kTopicBufLen];
  snprintf(willTopicBuf, kTopicBufLen, "%s/%s/status",
           MQTT_TOPIC_PREFIX, identity::deviceCode());

  Serial.printf("[mqtt] connect host=%s port=%d user=%s lwt=%s ...\n",
                config.mqttHost, config.mqttPort,
                config.mqttUsername, willTopicBuf);

  // PubSubClient::connect(clientId, user, pass, willTopic, willQos, willRetain, willMsg)
  bool ok = s_mqtt->connect(
      identity::deviceCode(),
      config.mqttUsername,
      config.mqttPassword,
      willTopicBuf,           // S4-FW-02 will topic
      1,                       // willQos
      true,                    // willRetain
      "offline");              // willMessage

  if (!ok) {
    int state = s_mqtt->state();
    // PubSubClient state codes — map sang lý do dễ debug.
    const char* reason;
    switch (state) {
      case -4: reason = "MQTT_CONNECTION_TIMEOUT (broker không reply)"; break;
      case -3: reason = "MQTT_CONNECTION_LOST (socket drop giữa chừng)"; break;
      case -2: reason = "MQTT_CONNECT_FAILED (TCP/TLS connect lỗi — verify host/port/CA cert/NTP)"; break;
      case -1: reason = "MQTT_DISCONNECTED"; break;
      case  1: reason = "MQTT_CONNECT_BAD_PROTOCOL (broker từ chối phiên bản MQTT)"; break;
      case  2: reason = "MQTT_CONNECT_BAD_CLIENT_ID (clientId trùng hoặc invalid)"; break;
      case  3: reason = "MQTT_CONNECT_UNAVAILABLE (broker chưa sẵn sàng)"; break;
      case  4: reason = "MQTT_CONNECT_BAD_CREDENTIALS (sai user/password — chạy add-device.sh)"; break;
      case  5: reason = "MQTT_CONNECT_UNAUTHORIZED (auth OK nhưng ACL từ chối — check acl.conf)"; break;
      default: reason = "(unknown state)"; break;
    }
    Serial.printf("[mqtt] connect FAIL state=%d — %s\n", state, reason);
    return false;
  }

  s_connectCount++;
  s_consecutiveFail = 0;
  Serial.printf("[mqtt] CONNECTED (count=%lu)\n",
                static_cast<unsigned long>(s_connectCount));

  // S4-FW-03: publish "online" retain QoS 1 lên status — override LWT "offline".
  bool pubOk = s_mqtt->publish(willTopicBuf, "online", true /*retain*/);
  Serial.printf("[mqtt] status=online retain → %s\n", pubOk ? "OK" : "FAIL");

  // S4-FW-03: subscribe downlink cmd.
  static char cmdTopicBuf[kTopicBufLen];
  snprintf(cmdTopicBuf, kTopicBufLen, "%s/%s/cmd",
           MQTT_TOPIC_PREFIX, identity::deviceCode());
  bool subOk = s_mqtt->subscribe(cmdTopicBuf, 1 /*QoS 1*/);
  Serial.printf("[mqtt] subscribe %s → %s\n",
                cmdTopicBuf, subOk ? "OK" : "FAIL");

  return true;
}

}  // namespace

// ====================================================================
// Public API
// ====================================================================

// Sanity check — diagnose case-mismatch giữa DEVICE_CODE và MQTT_USERNAME.
//
// Backend convention (IotApiKeyService.GenerateMqttCredential):
//   mqtt_username = deviceCode.ToLowerInvariant()
//
// ACL pattern `solar/%u/...` dùng username = lowercase, nhưng backend
// `AdminIotDevicesController.SendCommand` publish topic dùng RAW deviceCode
// (`solar/{device.DeviceCode}/cmd`). Nếu deviceCode KHÔNG lowercase, topic
// backend gửi sẽ KHÔNG match ACL subscription của device → mất downlink.
//
// → Workaround Sprint 4: admin PHẢI tạo device với deviceCode đã lowercase.
//   Sprint 5+ backend nên lowercase topic build hoặc dùng MqttUsername field.
void warnIfCaseMismatch() {
  const char* dev = identity::deviceCode();
  const char* user = runtimecfg::runtimeConfig().mqttUsername;
  if (!dev || !user) return;

  size_t devLen = strlen(dev);
  size_t userLen = strlen(user);
  if (devLen != userLen) {
    Serial.printf("[mqtt] ⚠ DEVICE_CODE='%s' (len %u) khác MQTT_USERNAME='%s' (len %u) — "
                  "backend MqttUsername convention = lowercase(DeviceCode).\n",
                  dev, static_cast<unsigned>(devLen),
                  user, static_cast<unsigned>(userLen));
    return;
  }
  for (size_t i = 0; i < devLen; ++i) {
    if (static_cast<char>(tolower(static_cast<unsigned char>(dev[i]))) != user[i]) {
      Serial.printf("[mqtt] ⚠ MQTT_USERNAME='%s' KHÔNG phải lowercase(DEVICE_CODE='%s') — "
                    "downlink solar/{deviceCode}/cmd có thể NOT match ACL solar/%%u/cmd.\n",
                    user, dev);
      Serial.println("[mqtt]    → tạo device với deviceCode đã lowercase (vd 'gw-esp32-001'),");
      Serial.println("[mqtt]    → hoặc backend Sprint 5+ phải lowercase khi build topic.");
      return;
    }
  }
  Serial.printf("[mqtt] sanity OK — DEVICE_CODE='%s' lowercase khớp MQTT_USERNAME.\n", dev);
}

bool mqttBegin() {
  if (s_inited) return true;

  const runtimecfg::RuntimeConfig& config = runtimecfg::runtimeConfig();
  s_mqtt = config.mqttUseTls ? &s_tlsMqtt : &s_plainMqtt;

  if (!configureRuntimeTransport()) {
    Serial.println("[mqtt] mqttBegin FAIL — CA cert chưa sẵn sàng");
    return false;
  }

  s_mqtt->setServer(config.mqttHost, config.mqttPort);
  s_mqtt->setBufferSize(MQTT_MAX_PACKET_SIZE);
  s_mqtt->setKeepAlive(MQTT_KEEPALIVE_SEC);
  s_mqtt->setCallback(onMessage);

  warnIfCaseMismatch();

  s_inited = true;
  Serial.printf("[mqtt] init OK — broker=%s:%d tls=%d buf=%u keepalive=%ds\n",
                config.mqttHost, config.mqttPort, config.mqttUseTls ? 1 : 0,
                static_cast<unsigned>(MQTT_MAX_PACKET_SIZE),
                static_cast<int>(MQTT_KEEPALIVE_SEC));
  return true;
}

void mqttTick() {
  if (!s_inited) return;
  if (!WiFi.isConnected()) return;

  // TLS cert validation cần NTP synced — broker cert có notBefore=2026 GMT,
  // ESP32 boot time=1970 sẽ fail "certificate not yet valid" và loop spam log
  // tới khi NTP sync. Gate tryConnect bằng net::timeIsSynced() — Sprint 1 đã có
  // setup NTP qua time_sync.cpp.
  if (runtimecfg::runtimeConfig().mqttUseTls && !net::timeIsSynced()) {
    static uint32_t s_lastNtpWaitLogMs = 0;
    uint32_t now = millis();
    if (now - s_lastNtpWaitLogMs > 10000) {
      s_lastNtpWaitLogMs = now;
      Serial.println("[mqtt] đợi NTP sync trước khi TLS connect (tránh cert 'not yet valid')...");
    }
    return;
  }

  if (!s_mqtt->connected()) {
    uint32_t now = millis();
    if (now - s_lastReconnectMs < MQTT_RECONNECT_INTERVAL_MS) return;
    s_lastReconnectMs = now;
    tryConnect();
    return;
  }

  s_mqtt->loop();
}

bool mqttIsConnected() {
  return s_mqtt->connected();
}

void mqttSetCommandCallback(CommandCallback cb) {
  s_userCmdCb = cb;
}

// ---- Publish helpers ----

namespace {
bool publishWithStats(const char* topic, const char* payload, size_t len,
                     uint8_t qos, bool retain) {
  if (!s_mqtt->connected()) {
    s_pubFail++;
    s_consecutiveFail++;
    return false;
  }
  // PubSubClient::publish overload: (topic, payload, length, retain).
  // Lưu ý: không có overload nhận QoS publish cho v2.8 — QoS 0 mặc định.
  // Để giữ "QoS 1" spec, dùng beginPublish/write/endPublish (tự set QoS).
  // → Sprint 4: dùng publish (QoS 0) cho telemetry/heartbeat (acceptable per spec
  //   "publish/subscribe QoS 0..1"); LWT vẫn được set QoS 1 ở connect().
  //   Production có thể nâng cấp lên MQTTnet hoặc esp-mqtt nếu cần QoS 1 strict.
  (void)qos;
  bool ok = s_mqtt->publish(topic,
                            reinterpret_cast<const uint8_t*>(payload),
                            len, retain);
  if (ok) {
    s_pubOk++;
    s_consecutiveFail = 0;
  } else {
    s_pubFail++;
    s_consecutiveFail++;
  }
  return ok;
}
}  // namespace

bool mqttPublishTelemetry(const char* batterySerial,
                          const char* payload, size_t payloadLen) {
  // Defensive: batterySerial rỗng tạo topic invalid `solar/dev//telemetry` —
  // broker ACL `solar/%u/+/telemetry` reject. Báo lỗi early để không tốn round-trip.
  if (batterySerial == nullptr || batterySerial[0] == '\0') {
    Serial.println("[mqtt] publishTelemetry FAIL — batterySerial rỗng");
    s_pubFail++;
    s_consecutiveFail++;
    return false;
  }
  static char topicBuf[kTopicBufLen];
  snprintf(topicBuf, kTopicBufLen, "%s/%s/%s/telemetry",
           MQTT_TOPIC_PREFIX, identity::deviceCode(), batterySerial);
  return publishWithStats(topicBuf, payload, payloadLen, 1, false);
}

bool mqttPublishHeartbeat(const char* payload, size_t payloadLen) {
  static char topicBuf[kTopicBufLen];
  snprintf(topicBuf, kTopicBufLen, "%s/%s/heartbeat",
           MQTT_TOPIC_PREFIX, identity::deviceCode());
  return publishWithStats(topicBuf, payload, payloadLen, 1, false);
}

bool mqttPublishStatus(const char* payload, size_t payloadLen) {
  static char topicBuf[kTopicBufLen];
  snprintf(topicBuf, kTopicBufLen, "%s/%s/status",
           MQTT_TOPIC_PREFIX, identity::deviceCode());
  return publishWithStats(topicBuf, payload, payloadLen, 1, true);
}

bool mqttPublishCmdAck(const char* payload, size_t payloadLen) {
  static char topicBuf[kTopicBufLen];
  snprintf(topicBuf, kTopicBufLen, "%s/%s/cmd/ack",
           MQTT_TOPIC_PREFIX, identity::deviceCode());
  return publishWithStats(topicBuf, payload, payloadLen, 1, false);
}

// ---- Stats ----
uint32_t mqttPublishOkCount()         { return s_pubOk; }
uint32_t mqttPublishFailCount()       { return s_pubFail; }
uint32_t mqttConnectCount()           { return s_connectCount; }
uint32_t mqttConsecutiveFailCount()   { return s_consecutiveFail; }
void     mqttResetConsecutiveFails()  { s_consecutiveFail = 0; }

}  // namespace net
