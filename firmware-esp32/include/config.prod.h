// ==================================================================
// include/config.prod.h — Backup cấu hình PRODUCTION
// Copy đè file này thành config.h khi chuyển lên Production.
// ==================================================================
#pragma once

// --------- WiFi ----------
#define WIFI_SSID       ""
#define WIFI_PASS       ""

// --------- Browser setup portal ----------
#define CONFIG_PORTAL_PORT            8080
#define CONFIG_PORTAL_USER            "admin"
#define CONFIG_PORTAL_PASSWORD        "12345678"
#define CONFIG_PORTAL_HOSTNAME        "solar-gateway"
#define SETUP_AP_PASSWORD             "12345678"
#define CONFIG_PORTAL_AP_FALLBACK_MS  30000UL

// --------- NTP ----------
#define NTP_SERVER_1    "time.google.com"
#define NTP_SERVER_2    "time.cloudflare.com"
#define NTP_SERVER_3    "pool.ntp.org"
#define NTP_GMT_OFFSET_SEC      0
#define NTP_DAYLIGHT_OFFSET_SEC 0

// --------- Serial ----------
#define SERIAL_BAUD     115200

// ============== Backend ==============
#define BACKEND_URL         "https://api.solaris.io.vn"

#define BACKEND_INGEST_PATH    "/api/sensor-readings/batch"
#define BACKEND_PROVISION_PATH "/api/iot-devices/provision"
#define BACKEND_HEARTBEAT_PATH "/api/iot-devices/heartbeat"
#define HARDWARE_REVISION      "ESP32-S3-DevKitC-1-N16R8"

#define DEVICE_CODE         ""
#define API_KEY             ""

// --------- Ingest loop ---------
#define INGEST_INTERVAL_MS  1000UL
#define MOCK_BATTERY_COUNT  4
#define HTTP_TIMEOUT_MS     8000
#define HTTP_INGEST_TIMEOUT_MS  3000

// ============== MQTT — PRODUCTION TLS ==============
#define MQTT_BROKER_HOST    "mqtt.solaris.io.vn"
#define MQTT_BROKER_PORT    8883
#define MQTT_USE_TLS        1

#define MQTT_USERNAME       ""
#define MQTT_PASSWORD       ""
#define MQTT_TOPIC_PREFIX   "solar"
#define MQTT_CLIENT_ID      DEVICE_CODE
#define MQTT_KEEPALIVE_SEC  5
#define MQTT_CA_CERT_PATH   "/ca_cert.pem"
#define MQTT_MAX_PACKET_SIZE 4096
#define MQTT_RECONNECT_INTERVAL_MS 5000UL
#define MQTT_PUBLISH_FAIL_THRESHOLD 3

#ifndef TLS_ALLOW_INSECURE
#define TLS_ALLOW_INSECURE  0
#endif

// ============== Hardware ==============
#ifndef USE_MOCK_BMS
  #define USE_MOCK_BMS  0
#endif

// --------- Đường nối BMS ---------
#define BMS_RS485_TX_PIN     17
#define BMS_RS485_RX_PIN     18
#define BMS_RS485_DE_PIN     -1
#define BMS_RS485_BAUD       115200UL

#define BMS_MODEL            3

#define BMS_UNIT_ID_START    1
#define BMS_UNIT_ID_COUNT    1
#define BMS_POLL_TIMEOUT_MS  500UL
#define BMS_POLL_RETRY       1

// --------- I2C dùng chung INA226 + SHT3X ---------
#define I2C_SDA_PIN          8
#define I2C_SCL_PIN          9
#define I2C_FREQUENCY_HZ     100000UL

// --------- HS0724 microSD (SPI, 3.3V only) ---------
#define SD_CARD_ENABLED      0
#define SD_CS_PIN            14
#define SD_MOSI_PIN          5
#define SD_SCK_PIN           21
#define SD_MISO_PIN          7
#define SD_SPI_FREQUENCY_HZ  4000000UL

// --------- INA226 (HW-899) ---------
#define INA226_I2C_ADDRESS   0x40
#define INA226_SHUNT_OHM     0.000375f
#define INA226_MAX_CURRENT_A 200.0f

// --------- DS18B20 ---------
#define DS18B20_GPIO         4
#define DS18B20_RESOLUTION   12
#define DS18B20_MAX_SENSORS  8

// --------- SHT3X ---------
#define SHT31_ENABLED        0
#define SHT31_I2C_ADDRESS    0x44
#define SHT31_POLL_INTERVAL_MS 60000UL
#define BACKEND_AMBIENT_PATH "/api/ambient/readings/batch"

// ============== Sự cố môi trường ==============
#define BACKEND_ENV_INCIDENT_PATH "/api/environmental-incidents"

// --------- MQ-2 ---------
#define MQ2_ENABLED              1
#define MQ2_ADC_PIN              1
#define MQ2_WARMUP_MS            30000UL
#define MQ2_POLL_INTERVAL_MS     1000UL

// --------- Cảm biến mưa ---------
#define WATER_LEAK_ENABLED            1
#define WATER_LEAK_GPIO               2
#define WATER_LEAK_ACTIVE_HIGH        0
#define WATER_LEAK_POLL_INTERVAL_MS   100UL
#define WATER_LEAK_REARM_COOLDOWN_MS  300000UL

// ============== OTA ==============
#define OTA_ENABLED            1
#define BACKEND_FW_CHECK_PATH  "/api/iot-devices/firmware-check"
#define BACKEND_FW_LOG_PATH    "/api/iot-devices/firmware-update-log/"
#define OTA_CHECK_INTERVAL_MS  3600000UL
#define OTA_HEALTH_TIMEOUT_MS  120000UL
#define OTA_HTTP_TIMEOUT_MS    20000UL
#define OTA_MAX_BOOT_ATTEMPTS  5
#define OTA_MAX_VERSION_FAILS  3

// --------- Firmware metadata ---------
#ifndef FW_VERSION
  #define FW_VERSION "unknown"
#endif
#ifndef FW_BUILD_ENV
  #define FW_BUILD_ENV "unknown"
#endif
