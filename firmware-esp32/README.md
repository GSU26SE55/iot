# firmware-esp32 — ESP32-S3 firmware

Sprint 0 chỉ tạo skeleton + 2 sketch demo:
- `examples/01-blink-hello/main.cpp` — S0-FW-02 (blink LED + Serial hello)
- `src/main.cpp` — S0-FW-03 (WiFi connect + NTP sync, in ISO8601 UTC mỗi giây)

Sprint 1 trở đi sẽ thay `src/main.cpp` thành firmware thật (mock BMS → HTTPS → MQTT → BMS thật).

## Yêu cầu môi trường

```bash
# macOS
brew install platformio
# hoặc
pipx install platformio
```

Hoặc dùng VSCode extension **PlatformIO IDE**.

Kiểm tra:
```bash
pio --version          # ≥ 6.x
```

## Chuẩn bị config (S0-FW-03)

```bash
cd firmware-esp32
cp include/config.example.h include/config.h
# sửa WIFI_SSID, WIFI_PASSWORD trong include/config.h
```

> `include/config.h` đã add vào `.gitignore` — không bao giờ commit WiFi password.

## Build & flash

### S0-FW-02 — Blink + hello
```bash
pio run -e example-blink                       # build
pio run -e example-blink -t upload             # flash
pio device monitor -b 115200                    # xem Serial
```

Mong đợi:
```
==============================
 Sprint 0 — S0-FW-02
 ...
==============================
[1] hello (uptime=1s)
[2] hello (uptime=2s)
...
```

### S0-FW-03 — WiFi + NTP (env mặc định)
```bash
pio run                                         # build
pio run -t upload                               # flash
pio device monitor -b 115200                    # xem Serial
```

Mong đợi (sau ~10s):
```
[wifi] connecting to "your-wifi" .....
[wifi] connected ip=192.168.1.42 rssi=-58dBm
[ntp] sync requested
[ntp] synced first time = 2026-06-12T08:15:42Z (utc)
[12s] isoNow=2026-06-12T08:15:43Z rssi=-58dBm wifi=OK
[13s] isoNow=2026-06-12T08:15:44Z rssi=-59dBm wifi=OK
```

So với watch / [time.is](https://time.is) — lệch ≤ 1s.

### Test reconnect (S1 sẽ chuẩn hơn — S0 chỉ smoke)
- Tắt WiFi router → Serial in `[wifi] reconnecting...` mỗi 5s
- Bật WiFi router lại → tự reconnect trong < 30s, in `[wifi] connected ip=...`

## Cấu trúc

```
firmware-esp32/
├── platformio.ini              ← S0-FW-01
├── include/
│   ├── config.example.h        ← template config (commit được)
│   └── config.h                ← cá nhân, KHÔNG commit
├── src/
│   └── main.cpp                ← S0-FW-03 (env mặc định)
├── examples/
│   └── 01-blink-hello/
│       └── main.cpp            ← S0-FW-02 (env example-blink)
├── lib/                         ← lib local (rỗng ở S0)
├── data/                        ← LittleFS payload (rỗng ở S0, S4 sẽ chứa CA cert)
└── test/                        ← unit test (rỗng ở S0)
```

## Library đã pin trong `platformio.ini`

| Lib | Version | Mục đích | Sprint dùng |
|-----|---------|----------|-------------|
| PubSubClient | ^2.8 | MQTT client | S4 |
| ArduinoJson | ^7.1 | JSON build/parse | S1 |
| ModbusMaster | ^2.0.1 | RS485 Modbus RTU | S5 |
| OneWire | ^2.3.8 | 1-Wire bus | S5 |
| DallasTemperature | ^3.11 | DS18B20 | S5 |
| INA226 (robtillaart) | ^0.6 | Current + voltage redundant | S5 |
| Adafruit SHT31 | ^2.2.2 | Ambient temp/humidity | S5 |
| Adafruit BusIO | ^1.16 | Transitive | S5 |

S0 acceptance chỉ cần `pio run` compile pass — không cần dùng hết.

## Troubleshooting

| Vấn đề | Cách xử |
|--------|---------|
| `pio device monitor` không có Serial | Đảm bảo board chọn ESP32-S3 (USB-CDC) và driver USB OK (macOS Sequoia thường tự nhận; Windows cài CP2102 driver) |
| Build báo `config.h: No such file` | Chưa copy `config.example.h` → `config.h` |
| NTP không sync (`waiting for ntp sync`) | Router chặn UDP port 123 hoặc firewall — đổi sang hotspot điện thoại để test |
| Lệch giờ > 1s | DNS pool chậm — sửa `NTP_SERVER_1` sang IP NTP nội bộ trường nếu có |
