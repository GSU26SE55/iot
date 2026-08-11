# IoT Wiring Diagram — ESP32-S3 Node

> **Document type:** Sơ đồ đấu nối phần cứng (wiring) + bảng gán chân GPIO cho node ESP32-S3.
> **Hệ thống:** ESP32-S3 + RS485/Modbus (multi-drop) + sensor phụ + môi trường + nguồn.
> **Liên quan:** `overall.iot.md` (BOM + luồng), `newiot.md` (thiết kế tổng thể), `hardware-bom-budget.csv`.
> **Cập nhật:** 2026-05-30

> ⚠️ **Nguyên tắc vàng — COMMON GROUND:** Mọi module (MAX485, INA226, DS18B20, SHT31, MQ-2, CAN, nguồn) và BMS **phải nối chung GND** với ESP32. Đây là lỗi đấu dây phổ biến nhất khiến đọc sai hoặc không đọc được.

> ⚠️ **Mức điện áp logic:** ESP32-S3 chạy **3.3V logic**. Cấp VCC module ưu tiên **3V3**. Module nào chỉ chạy 5V (vd vài loại MAX485, MQ-2) thì cấp 5V cho VCC nhưng **chân tín hiệu phải qua bộ chia áp / level shifter** về 3.3V trước khi vào GPIO ESP32 — nếu không sẽ hỏng chân.

---

## 1. Bảng gán chân GPIO (ESP32-S3 DevKitC-1)

| Chức năng | Chân ESP32-S3 | Nối tới | Ghi chú |
|-----------|---------------|---------|---------|
| **RS485 TX** | GPIO17 (U1TXD) | MAX485 `DI` | UART1 |
| **RS485 RX** | GPIO18 (U1RXD) | MAX485 `RO` | UART1 |
| **RS485 DE/RE** | GPIO16 | MAX485 `DE`+`RE` nối chung | Bỏ qua nếu dùng module auto-direction |
| **I2C SDA** | GPIO8 | INA226 `SDA`, SHT31 `SDA` | Bus I2C chung |
| **I2C SCL** | GPIO9 | INA226 `SCL`, SHT31 `SCL` | Bus I2C chung |
| **1-Wire** | GPIO4 | DS18B20 `DATA` | + trở 4.7kΩ kéo lên 3V3 |
| **CAN TX (TWAI)** | GPIO5 | SN65HVD230 `CTX` | Chỉ khi dùng CAN |
| **CAN RX (TWAI)** | GPIO6 | SN65HVD230 `CRX` | Chỉ khi dùng CAN |
| **MQ-2 analog** | GPIO1 (ADC1) | MQ-2 `AO` | Đọc nồng độ khói |
| **Water leak** | GPIO2 | Water sensor `DO/AO` | Digital hoặc analog |
| **SD CS** | GPIO14 | SD module `CS` | SPI (tránh GPIO 10-13 do trùng Flash/PSRAM Octal) |
| **SD MOSI** | GPIO15 | SD module `MOSI` | SPI |
| **SD SCK** | GPIO21 | SD module `SCK` | SPI |
| **SD MISO** | GPIO47 | SD module `MISO` | SPI |
| **Status LED** | GPIO48 | LED RGB onboard | Báo trạng thái (online/queue/error) |

> Các GPIO trên là **gợi ý** — có thể đổi, nhưng phải khớp với `config.h` trong firmware. Tránh dùng GPIO strapping (0, 3, 45, 46) cho tín hiệu quan trọng.

---

## 2. Sơ đồ tổng thể 1 node (block diagram)

```
                         ┌───────────────────────────────────────────┐
                         │              ESP32-S3 DevKitC              │
                         │                                            │
   [Nguồn 5V/12V]        │  5V/VIN ◄── nguồn                          │
        │                │  GND    ◄── GND chung (COMMON GROUND)       │
        ▼                │                                            │
   [LM2596 buck 12V→5V]──┤  3V3    ──► cấp VCC cho các module 3.3V    │
        │                │                                            │
   [TP4056+18650 UPS]────┘  GPIO17(TX) ─────► MAX485 DI               │
                         │  GPIO18(RX) ◄───── MAX485 RO               │
                         │  GPIO16     ─────► MAX485 DE+RE  (nếu cần)  │
                         │  GPIO8(SDA) ◄────► INA226 / SHT31 (I2C)     │
                         │  GPIO9(SCL) ─────► INA226 / SHT31 (I2C)     │
                         │  GPIO4      ◄────► DS18B20 (1-Wire +4.7kΩ)  │
                         │  GPIO5(CTX) ─────► SN65HVD230 (CAN)         │
                         │  GPIO6(CRX) ◄───── SN65HVD230 (CAN)         │
                         │  GPIO1(ADC) ◄───── MQ-2 (khói)              │
                         │  GPIO2      ◄───── Water leak sensor        │
                         │  GPIO10/11/12/13 ◄► SD card module (SPI)    │
                         │                                            │
                         │  WiFi/4G ──► MQTT broker + HTTPS backend    │
                         └───────────────────────────────────────────┘
```

---

## 3. RS485 / Modbus — multi-drop (đọc nhiều pin)

```
ESP32-S3                  MAX485 (XY-017)              RS485 BUS (đôi xoắn shielded)
┌────────┐                ┌──────────┐
│ GPIO17 │───── DI ──────►│          │ A ●───────┬─────────┬─────────┬─────────┐
│ GPIO18 │◄──── RO ───────│  MAX485  │           │         │         │         │
│ GPIO16 │──DE+RE(opt)───►│          │ B ●───────┼──┬──────┼──┬──────┼──┬──────┼──┐
│  3V3   │───── VCC ──────│          │           │  │      │  │      │  │      │  │
│  GND   │───── GND ──────│          │          [120Ω]    BMS1      BMS2      BMS3...
└────────┘                └──────────┘           ▲ A B     A B       A B       A B
                                                 │ unitId=1 unitId=2  unitId=3  ...
                              [120Ω] ◄────────────┘
                              (terminating 2 đầu bus)
```

**Quy tắc multi-drop:**
- Tất cả BMS nối song song lên cùng cặp dây A/B.
- Mỗi BMS đặt **unitId khác nhau** (1, 2, 3...).
- Gắn **điện trở 120Ω** ở **2 đầu xa nhất** của bus.
- Dùng **cáp đôi xoắn có shield**, shield nối GND 1 đầu.
- 1 bus ≤ 32 thiết bị (thực tế ≤ 10–20), cùng 1 baud rate.

---

## 4. Sensor phụ (redundant + môi trường)

### 4.1. DS18B20 — nhiệt độ thân pin (1-Wire)

```
ESP32 3V3 ──┬──────────────► DS18B20 VCC (đỏ)
            │
          [4.7kΩ]
            │
ESP32 GPIO4 ┴──────────────► DS18B20 DATA (vàng)
ESP32 GND ─────────────────► DS18B20 GND (đen)

(nhiều DS18B20 có thể chung 1 dây DATA — mỗi con có địa chỉ 64-bit riêng)
```

### 4.2. INA226 (V/I redundant) + SHT31 (ambient) — chung bus I2C

```
ESP32 3V3 ───┬─────────────► INA226 VCC ───┬───► SHT31 VCC
ESP32 GND ───┼─────────────► INA226 GND ───┼───► SHT31 GND
ESP32 GPIO8 ─┼─(SDA)───────► INA226 SDA ───┼───► SHT31 SDA
ESP32 GPIO9 ─┴─(SCL)───────► INA226 SCL ───┴───► SHT31 SCL

INA226 đo dòng: nối shunt nối tiếp với tải pin (IN+ / IN-)
(địa chỉ I2C khác nhau: INA226 0x40, SHT31 0x44 → không đụng nhau)
```

### 4.3. MQ-2 (khói) + Water leak

```
ESP32 5V ─────► MQ-2 VCC      |  ESP32 3V3 ──► Water VCC
ESP32 GND ────► MQ-2 GND      |  ESP32 GND ──► Water GND
ESP32 GPIO1 ◄── MQ-2 AO       |  ESP32 GPIO2 ◄── Water DO/AO
(MQ-2 chạy 5V — chân AO phải qua chia áp về 3.3V trước khi vào GPIO1)
```

---

## 5. CAN bus (nếu BMS dùng CAN)

```
ESP32-S3                 SN65HVD230               CAN BUS
┌────────┐               ┌──────────┐
│ GPIO5  │──── CTX ──────│          │ CANH ●──────┬────────── BMS CANH
│ GPIO6  │◄─── CRX ──────│ SN65HVD  │             │
│  3V3   │──── VCC ──────│  230     │ CANL ●──────┼────────── BMS CANL
│  GND   │──── GND ──────│          │            [120Ω]
└────────┘               └──────────┘         (terminating 2 đầu)
```

---

## 6. Hệ thống nguồn (power subsystem)

```
   ┌─────────────────────────────────────────────────────────┐
   │ Phương án A — cấp nguồn trực tiếp:                        │
   │   Adapter 5V/3A ──► ESP32 5V/VIN + GND                    │
   ├─────────────────────────────────────────────────────────┤
   │ Phương án B — lấy nguồn từ pin 12V (thực địa):           │
   │   Pin 12V ─[cầu chì]─► LM2596 (buck 12V→5V) ─► ESP32 5V   │
   ├─────────────────────────────────────────────────────────┤
   │ Phương án C — có UPS chống cúp điện (offline báo đúng):  │
   │   Nguồn ─► TP4056 (sạc) ─► 18650 ─► boost 5V ─► ESP32     │
   │   (khi mất điện lưới, 18650 giữ ESP32 sống để báo offline)│
   └─────────────────────────────────────────────────────────┘
```

**Lưu ý nguồn:**
- ESP32-S3 đỉnh dòng ~500mA khi WiFi/TLS phát → dùng adapter ≥2A để tránh reset.
- Tụ 470–1000µF gắn gần chân nguồn ESP32 giúp ổn định khi WiFi spike.
- Cầu chì bảo vệ khi lấy nguồn từ pin công suất lớn.

---

## 7. Sơ đồ đấu dây đầy đủ 1 node (full assembly)

```
                              ┌──────── enclosure IP65 ────────┐
   [Pin1 BMS u1]──┐           │                                │
   [Pin2 BMS u2]──┤RS485 A/B  │   ┌────────────┐               │
   [Pin3 BMS u3]──┼───────────┼──►│  MAX485    │──UART──┐      │
   [Pin4 BMS u4]──┘ +120Ω×2   │   └────────────┘        │      │
                              │                          ▼      │
   [DS18B20]──1-Wire──────────┼──────────────────► ┌──────────┐│
   [INA226 ]──I2C─────────────┼──────────────────► │ ESP32-S3 ││──WiFi/4G──► Broker/Backend
   [SHT31  ]──I2C─────────────┼──────────────────► │  (N16R8) ││
   [MQ-2   ]──ADC─────────────┼──────────────────► │          ││
   [Water  ]──GPIO────────────┼──────────────────► └────┬─────┘│
   [SD card]──SPI─────────────┼───────────────────────┘ │      │
                              │                          │      │
   [Adapter 5V]──┐            │   ┌──────────┐           │      │
   [Pin 12V]──[cầu chì]──────►├──►│ LM2596   │──5V───────┘      │
   [TP4056+18650 UPS]─────────┤   └──────────┘                  │
                              │   ── tất cả GND nối chung ──     │
                              └────────────────────────────────┘
```

---

## 8. Checklist đấu dây (kiểm tra trước khi cấp nguồn)

- [ ] Tất cả GND đã nối chung (ESP32 + mọi module + BMS).
- [ ] MAX485 A↔A, B↔B đúng chiều với mọi BMS (không đảo A/B).
- [ ] Điện trở 120Ω gắn đúng 2 đầu bus RS485 (và CAN nếu dùng).
- [ ] DS18B20 có trở pull-up 4.7kΩ giữa DATA và 3V3.
- [ ] Module 5V (MQ-2...) có chia áp/level shifter trước khi vào GPIO 3.3V.
- [ ] Mỗi BMS đã đặt unitId khác nhau.
- [ ] Nguồn ≥2A, có cầu chì khi lấy từ pin công suất.
- [ ] Không cấp 5V trực tiếp vào chân GPIO/3V3 của ESP32.
- [ ] Kiểm tra ngắn mạch VCC-GND bằng đồng hồ trước khi cắm điện.
```
