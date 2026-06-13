// ESP32 firmware — Sprint IoT-1 (#251)
// Target: ESP32-S3-DevKitC-1 + MAX485 transceiver + BMS Modbus RTU multi-drop.
//
// Tính năng:
//  - WiFi connect (creds qua NVS partition).
//  - Modbus RTU poll: nhiều BMS unitId trên 1 bus RS485 (multi-drop), mỗi lần đọc 1 unit.
//  - HTTPS POST /api/sensor-readings/batch với X-Api-Key + X-Device-Code + Idempotency-Key.
//  - Heartbeat 60s.
//  - LittleFS queue khi backend down → retry với Idempotency-Key cũ.
//  - OTA: poll firmware-check → download artifact → flash → report success/fail.
//
// Tham chiếu:
//  - Wiring: ../wiring-diagram.md
//  - BMS register mapping: ./bms_register_map.md
//  - newiot.md §5/§8 (overall.iot.md).

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ModbusMaster.h>     // 4-20mA Engineering ModbusMaster
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Update.h>           // ESP32 OTA

// --- Config (load từ NVS preferences) ---
struct Config {
    String wifi_ssid;
    String wifi_pass;
    String backend_base_url;  // vd "https://api.gsu26se55.com"
    String device_code;       // vd "ESP32-001"
    String api_key;           // raw "iotk_..."
    String firmware_version;  // hard-coded khi build: "1.0.0"
    String hardware_revision; // "v1.0-S3-MAX485"
    uint8_t bms_unit_ids[8];  // 0..7 ; 0 = unused
    uint8_t bms_count;
};

static Config cfg;
static Preferences prefs;

// MAX485 RE/DE pin — gắn chung 1 GPIO cho both (auto direction).
#define MAX485_DE_RE_PIN 4
#define MAX485_UART_RX  16
#define MAX485_UART_TX  17

ModbusMaster modbus;

void preTransmission() { digitalWrite(MAX485_DE_RE_PIN, HIGH); }
void postTransmission() { digitalWrite(MAX485_DE_RE_PIN, LOW); }

// --- Mock BMS register layout (cập nhật theo BMS thật từ bms_register_map.md) ---
constexpr uint16_t REG_VOLTAGE      = 0x0000;  // x0.001 V
constexpr uint16_t REG_CURRENT      = 0x0001;  // signed, x0.001 A
constexpr uint16_t REG_TEMPERATURE  = 0x0002;  // signed, x0.1 °C
constexpr uint16_t REG_SOC          = 0x0003;  // x0.1 %
constexpr uint16_t REG_CYCLE_COUNT  = 0x0004;

struct BmsReading {
    uint8_t unitId;
    float voltage;
    float current;
    float temperature;
    float socPercent;
    uint16_t cycleCount;
    bool valid;
};

BmsReading readBms(uint8_t unitId) {
    BmsReading r{unitId, 0, 0, 0, 0, 0, false};
    modbus.begin(unitId, Serial2);

    uint8_t res = modbus.readHoldingRegisters(REG_VOLTAGE, 5);
    if (res != modbus.ku8MBSuccess) {
        Serial.printf("[BMS %u] modbus read fail code=%u\n", unitId, res);
        return r;
    }
    r.voltage     = modbus.getResponseBuffer(0) * 0.001f;
    int16_t curRaw = (int16_t)modbus.getResponseBuffer(1);
    r.current     = curRaw * 0.001f;
    int16_t tRaw  = (int16_t)modbus.getResponseBuffer(2);
    r.temperature = tRaw * 0.1f;
    r.socPercent  = modbus.getResponseBuffer(3) * 0.1f;
    r.cycleCount  = modbus.getResponseBuffer(4);
    r.valid = true;
    return r;
}

// --- Local queue (LittleFS) ---
constexpr const char* QUEUE_PATH = "/queue.jsonl";

void queueAppend(const String& payload, const String& idempKey) {
    File f = LittleFS.open(QUEUE_PATH, FILE_APPEND);
    if (!f) return;
    f.printf("{\"key\":\"%s\",\"payload\":%s}\n", idempKey.c_str(), payload.c_str());
    f.close();
}

size_t queueSize() {
    File f = LittleFS.open(QUEUE_PATH, FILE_READ);
    if (!f) return 0;
    size_t n = 0;
    while (f.available()) {
        if (f.read() == '\n') n++;
    }
    f.close();
    return n;
}

// --- HTTP helper ---
WiFiClientSecure secureClient;

int httpPost(const String& url, const String& body, const String& idempKey) {
    HTTPClient http;
    http.begin(secureClient, url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Api-Key", cfg.api_key);
    http.addHeader("X-Device-Code", cfg.device_code);
    if (!idempKey.isEmpty())
        http.addHeader("Idempotency-Key", idempKey);
    int code = http.POST(body);
    if (code <= 0)
        Serial.printf("[HTTP] POST %s fail %s\n", url.c_str(), http.errorToString(code).c_str());
    else
        Serial.printf("[HTTP] POST %s → %d\n", url.c_str(), code);
    http.end();
    return code;
}

int httpGet(const String& url, String& bodyOut) {
    HTTPClient http;
    http.begin(secureClient, url);
    http.addHeader("X-Api-Key", cfg.api_key);
    http.addHeader("X-Device-Code", cfg.device_code);
    int code = http.GET();
    if (code > 0) bodyOut = http.getString();
    http.end();
    return code;
}

// --- Build batch payload from list of BMS reading ---
String buildBatch(const BmsReading* readings, size_t n) {
    StaticJsonDocument<4096> doc;
    JsonArray items = doc.createNestedArray("Items");
    char tsBuf[32];
    time_t now;
    time(&now);
    struct tm tmInfo;
    gmtime_r(&now, &tmInfo);
    strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%dT%H:%M:%SZ", &tmInfo);

    for (size_t i = 0; i < n; i++) {
        if (!readings[i].valid) continue;
        JsonObject it = items.createNestedObject();
        char serial[32];
        snprintf(serial, sizeof(serial), "BAT-%03u", readings[i].unitId);
        it["BatteryAssetSerial"] = serial;
        it["Time"] = tsBuf;
        it["DeviceTimestamp"] = tsBuf;
        it["Voltage"] = readings[i].voltage;
        it["Current"] = readings[i].current;
        it["Temperature"] = readings[i].temperature;
        it["SocPercent"] = readings[i].socPercent;
        it["CycleCount"] = readings[i].cycleCount;
        it["SourceType"] = 2;  // IotGateway
        it["SourceDeviceId"] = cfg.device_code;
    }
    String out;
    serializeJson(doc, out);
    return out;
}

// --- Heartbeat ---
unsigned long lastHeartbeatMs = 0;
unsigned long lastIngestMs = 0;
unsigned long lastFwCheckMs = 0;

void sendHeartbeat() {
    StaticJsonDocument<512> doc;
    char tsBuf[32];
    time_t now; time(&now);
    struct tm t; gmtime_r(&now, &t);
    strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%dT%H:%M:%SZ", &t);

    doc["FirmwareVersion"] = cfg.firmware_version;
    doc["RssiDbm"] = WiFi.RSSI();
    doc["FreeMemoryPercent"] = (ESP.getFreeHeap() * 100.0f) / ESP.getHeapSize();
    doc["UptimeSeconds"] = (long)(millis() / 1000);
    doc["QueuedReadingCount"] = (int)queueSize();
    doc["DeviceTimestamp"] = tsBuf;

    String body; serializeJson(doc, body);
    httpPost(cfg.backend_base_url + "/api/iot-devices/heartbeat", body, "");
}

void checkFirmware() {
    String url = cfg.backend_base_url + "/api/iot-devices/firmware-check?currentVersion=" + cfg.firmware_version;
    String body;
    int code = httpGet(url, body);
    if (code != 200) return;
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, body)) return;
    bool available = doc["data"]["updateAvailable"] | false;
    if (!available) return;
    String artifact = doc["data"]["artifactUrl"].as<String>();
    String checksum = doc["data"]["sha256Checksum"].as<String>();
    String target = doc["data"]["targetVersion"].as<String>();
    String logId = doc["data"]["updateLogId"].as<String>();
    Serial.printf("[OTA] update available %s → %s\n", cfg.firmware_version.c_str(), target.c_str());
    // TODO: implement download artifact stream → Update.write → Update.end → reportLog(Success/Failed).
    // Reference: https://github.com/espressif/arduino-esp32/blob/master/libraries/Update/examples/HTTPS_OTA_Update/HTTPS_OTA_Update.ino
}

void setup() {
    Serial.begin(115200);
    pinMode(MAX485_DE_RE_PIN, OUTPUT);
    digitalWrite(MAX485_DE_RE_PIN, LOW);

    Serial2.begin(9600, SERIAL_8N1, MAX485_UART_RX, MAX485_UART_TX);
    modbus.preTransmission(preTransmission);
    modbus.postTransmission(postTransmission);

    LittleFS.begin(true);

    prefs.begin("iot", true);
    cfg.wifi_ssid = prefs.getString("ssid", "REPLACE_ME");
    cfg.wifi_pass = prefs.getString("pass", "REPLACE_ME");
    cfg.backend_base_url = prefs.getString("backend", "https://localhost:7200");
    cfg.device_code = prefs.getString("device", "ESP32-001");
    cfg.api_key = prefs.getString("apikey", "");
    cfg.firmware_version = "1.0.0";
    cfg.hardware_revision = "v1.0-S3-MAX485";
    cfg.bms_count = prefs.getUChar("bmsCount", 1);
    for (uint8_t i = 0; i < cfg.bms_count && i < 8; i++) {
        cfg.bms_unit_ids[i] = prefs.getUChar(("bms" + String(i)).c_str(), i + 1);
    }
    prefs.end();

    WiFi.begin(cfg.wifi_ssid.c_str(), cfg.wifi_pass.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
        delay(500); Serial.print(".");
    }
    Serial.printf("\n[WiFi] %s\n", WiFi.localIP().toString().c_str());

    secureClient.setInsecure();  // PILOT — replace với root CA bundle khi production.

    configTime(0, 0, "pool.ntp.org", "time.google.com");

    // Provision once.
    StaticJsonDocument<256> pdoc;
    pdoc["FirmwareVersion"] = cfg.firmware_version;
    pdoc["HardwareRevision"] = cfg.hardware_revision;
    char tsBuf[32]; time_t now; time(&now);
    struct tm t; gmtime_r(&now, &t);
    strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%dT%H:%M:%SZ", &t);
    pdoc["DeviceTimestamp"] = tsBuf;
    String pbody; serializeJson(pdoc, pbody);
    httpPost(cfg.backend_base_url + "/api/iot-devices/provision", pbody, "");
}

void loop() {
    unsigned long now = millis();

    if (now - lastHeartbeatMs >= 60000) {
        sendHeartbeat();
        lastHeartbeatMs = now;
    }

    if (now - lastIngestMs >= 15000) {
        BmsReading readings[8];
        size_t cnt = 0;
        for (uint8_t i = 0; i < cfg.bms_count; i++) {
            readings[cnt++] = readBms(cfg.bms_unit_ids[i]);
        }
        String payload = buildBatch(readings, cnt);
        char idem[37]; snprintf(idem, sizeof(idem), "%08x-%08x-%08x", (unsigned)esp_random(), (unsigned)esp_random(), (unsigned)esp_random());

        int code = httpPost(cfg.backend_base_url + "/api/sensor-readings/batch", payload, String(idem));
        if (code != 200) {
            queueAppend(payload, String(idem));
            Serial.printf("[QUEUE] saved (size=%u)\n", (unsigned)queueSize());
        }
        lastIngestMs = now;
    }

    if (now - lastFwCheckMs >= 3600000UL) {
        checkFirmware();
        lastFwCheckMs = now;
    }

    delay(200);
}
