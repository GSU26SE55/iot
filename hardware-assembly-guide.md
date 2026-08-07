# Hướng dẫn lắp ráp Hardware — Node giám sát pin LFP 24V 30Ah + JK-BD6A24S10P + ESP32-S3

> **Ngày viết:** 2026-07-11 · **Rig:** 1 pack LiFePO4 8S 24V 30Ah + JK BMS **JK-BD6A24S10P** vỏ nhôm (xác nhận từ nhãn: 100A liên tục / 200A đỉnh / balance 0.6A / 8-24S / Bluetooth) + 1 node ESP32-S3 DevKitC-1
> **Tài liệu liên quan:** [hardware.csv](hardware.csv) (danh sách mua) · [wiring-diagram.md](wiring-diagram.md) (bảng GPIO chuẩn của repo) · [iot-backend-contract-gaps.md](iot-backend-contract-gaps.md) (checklist backend) · `firmware-esp32/include/config.example.h`
> **Nguồn kỹ thuật:** manual/guide chính hãng JK (shop.jkbms.com installation guide), lifepo4oz.com JK wiring guide, syssi/esphome-jk-bms, diysolarforum — đã đối chiếu chéo nhiều nguồn ngày 2026-07-11.
>
> ⚠️ Những chỗ đánh dấu **[PHẢI TỰ KIỂM CHỨNG]** là điểm tài liệu công khai chưa xác nhận 100% cho đúng lô hàng của bạn — bắt buộc verify bằng manual giấy trong hộp / đồng hồ đo / app trước khi làm.

---

## 0. Sơ đồ tổng thể — lắp xong sẽ như thế này

```
  PACK LFP 8S (24V 30Ah)
  ┌──────────────────────────┐
  │ cell1 cell2 ... cell8    │
  │   │     │        │       │  harness balance (B-, B1..B8, B+)
  │   └─────┴────────┴──────►│──────────────┐
  │                          │              ▼
  │  (−)pack   (+)pack       │      ┌───────────────┐   2 dây xanh B- / 2 dây đen P-
  └────┬──────────┬──────────┘      │ JK-BD6A24S10P │
       │          │                 │   (vỏ nhôm)   │
       │B- (xanh×2)│                └──┬────────┬───┘
       ▼          │                    │CAN&RS485│UART (dự phòng)
  [BMS] P-(đen×2) │                    ▼
       │          │                 [MAX485] ──A/B──► GPIO17/18/16
       ▼          ▼                    │
   (−)tải/sạc  (+)tải/sạc          ┌───┴──────────────────────┐
                                   │        ESP32-S3          │
   Pack(+) ─[cầu chì 2-5A]─►LM2596─►│ VIN 5V  (+ tụ 470-1000µF)│──WiFi──► MQTT/HTTPS ──► Backend
                                   │ GPIO8/9: INA226 + SHT31  │
                                   │ GPIO4:   DS18B20 (+4.7kΩ)│
                                   │ GPIO1:   MQ-2 (qua chia áp)│
                                   │ GPIO2:   Water leak      │
                                   └──────────────────────────┘
              ═══ TẤT CẢ GND NỐI CHUNG (common ground) ═══
```

Lộ trình lắp gồm **4 giai đoạn**, mỗi giai đoạn xong phải PASS phần kiểm tra của nó rồi mới sang giai đoạn sau:
1. Lắp BMS vào pack + cấu hình app (§2)
2. Đọc thử BMS bằng **laptop** (§3) — chưa đụng ESP32
3. Lắp node ESP32 + sensor trên breadboard (§4)
4. Nạp firmware + verify end-to-end lên backend (§5)

---

## 1. An toàn & chuẩn bị

Pack 24V không giật chết người, nhưng **dòng ngắn mạch của pin LFP 30Ah lên tới hàng trăm ampe** — đủ làm chảy dây, nổ tia lửa, cháy tool. Quy tắc:

- **Tháo nhẫn, đồng hồ, vòng tay kim loại** trước khi thao tác trên pack.
- Dụng cụ (kìm, tua vít) quấn băng keo cách điện phần thân kim loại lộ ra.
- Không bao giờ để 2 đầu dây lực chạm nhau hoặc cùng chạm 1 vật kim loại.
- Làm việc trên mặt bàn khô, không thảm kim loại, có đồng hồ vạn năng bên cạnh.
- **Đo trước khi cắm, cắm khi đã hiểu** — mọi connector chỉ cắm sau khi đo điện áp đúng kỳ vọng.
- BMS không phải đồ chơi hot-plug: **không rút/cắm harness balance khi đang có tải hoặc đang sạc**.

**Vật tư cần cho từng giai đoạn** (STT theo [hardware.csv](hardware.csv)):
- Giai đoạn 1: chỉ cần BMS + pack + đồng hồ (STT 23) + app điện thoại
- Giai đoạn 2: pigtail JST (STT 2) + USB-RS485 (STT 33) + laptop; tùy chọn USB-TTL (STT 34)
- Giai đoạn 3: ESP32 (1), MAX485 (3), breadboard (5), Dupont (6), USB-C (7), adapter 5V (8), tụ (13), sensor N3/N4 (14–21)
- Giai đoạn 4: (không cần mua thêm) — firmware + backend

---

## 2. Giai đoạn 1 — Lắp JK-BD6A24S10P vào pack 8S

### 2.1 Kiểm tra pack trước

Pack phải đã đấu series 8 cell hoàn chỉnh. Đo bằng đồng hồ:
- Từng cell: **3.0–3.4V** (LFP nghỉ). Cell nào lệch hẳn (< 2.8V hoặc > 3.6V) → xử lý cell đó trước, chưa lắp BMS.
- Tổng pack: **~25.6–26.4V**.
- Ghi lại 8 số đo — lát nữa so với app.

### 2.2 Đấu dây lực (dây to) — làm TRƯỚC harness

BMS này là **common port** (nạp và xả chung đường P-):

| Dây trên BMS | Nối tới | Bắt buộc |
|---|---|---|
| **B-** — 2 dây **xanh** to (7AWG) | Cực **âm tổng của pack** | Dùng **CẢ HAI** dây (chia dòng ~50A/dây) |
| **P-** — 2 dây **đen** to | Cực **âm chung** của tải **và** bộ sạc | Dùng **CẢ HAI** dây |
| Cực **dương pack** | Nối **thẳng** tới cực dương tải + cực dương sạc (không đi qua BMS) | — |

⚠️ **Tuyệt đối không bỏ bớt 1 dây trong cặp** — mỗi dây chỉ được thiết kế chịu ~50–60A; dồn 100A vào 1 dây là chảy vỏ.
⚠️ Sạc phải cắm vào **P-**, không cắm thẳng vào B- — cắm nhầm vào B- là sạc không qua bảo vệ của BMS.
Lúc này **chưa nối tải, chưa nối sạc** — chỉ bắt chặt đầu cos vào pack.

### 2.3 Harness balance — bước quan trọng nhất toàn bộ guide ⚠️⚠️

Connector balance trên BMS in 2 hàng: `温度 B+ B23 B21 … B3 B1` / `B24 B22 … B2 B-`. Với pack **8S**, quy tắc (đối chiếu từ installation guide chính hãng shop.jkbms.com + lifepo4oz.com + diysolarforum, các nguồn nhất quán):

| Dây | Nối tới | Ghi chú |
|---|---|---|
| **B-** (dây đen của harness) | Cực **âm tổng pack** (âm cell 1) | Mốc 0V — nối đầu tiên |
| **B1** | Cực **dương cell 1** | ~3.2V so với B- |
| **B2** | Cực **dương cell 2** | ~6.4V |
| **B3…B7** | Cực dương cell 3…7, tuần tự | mỗi bước +3.2V |
| **B8** | Cực **dương cell 8** = dương tổng pack | ~25.6V |
| **B+** (dây riêng trong harness) | **Cực dương tổng pack** (cùng điểm với B8) | **BẮT BUỘC** — đây là dây cấp nguồn cho mạch balancer; **không nối B+ = BMS không lên nguồn** |
| **B9 → B24** | **KHÔNG NỐI ĐI ĐÂU CẢ** | Để hở, cuộn gọn + dây rút, **KHÔNG cắt** (sau này lên 16S/24S còn dùng), **KHÔNG** chập vào nhau, **KHÔNG** nối vào pack |
| **温度** (dây NTC) | Probe dán lên **mặt cell** (giữa pack) | Dùng băng keo nhiệt/kapton, không kẹp vào cực |

**Trình tự thao tác chuẩn:**
1. Bắt các đầu cos/dây của harness vào pack theo bảng trên — **lúc này CHƯA cắm connector vào BMS**.
2. **Đo kiểm tra tại đầu connector** (que đen vào chân B-): B1 ≈ 3.2V → B2 ≈ 6.4V → … → B8 ≈ 25.6V → B+ ≈ 25.6V. Sai một nấc bất kỳ (nhảy 2 nấc, âm, hay 0V) = đấu nhầm thứ tự → sửa trước khi cắm. **Đây là phép đo cứu BMS — đừng bỏ qua.**
3. Đo xong đúng hết → cắm connector harness vào BMS **một lần, dứt khoát** (cắm rề rề dễ phóng tia lửa nhỏ ở chân — bình thường nếu có tia nhỏ lúc cắm).
4. BMS lên nguồn (có B+ là balancer có điện). Nếu hộp có kèm **dây nút nhấn (switch)** thì nối vào cổng tương ứng — một số firmware cần bấm nút để bật lần đầu. **[PHẢI TỰ KIỂM CHỨNG]** theo manual giấy trong hộp.

> **[PHẢI TỰ KIỂM CHỨNG] trước khi bắt dây:** hình dạng harness thực tế trong hộp (1 hay 2 connector, dây B+ nằm trong loom hay là dây đỏ rời) — đối chiếu nhãn in trên vỏ BMS với sơ đồ trong manual giấy. Nguyên tắc bất biến: **đo điện áp từng chân connector trước khi cắm** thì đấu kiểu gì cũng không hại BMS.

### 2.4 Cấu hình app JK BMS (bắt buộc, ngay sau khi BMS lên nguồn)

App: **JK BMS** (Android `com.jktech.bms` / iOS "极空BMS"). Mật khẩu kết nối BLE: **1234** · mật khẩu sửa thông số: **123456**.

1. Scan → kết nối đúng thiết bị (đối chiếu MAC trên nhãn: `28:D4:1E:6A:F6:78`).
2. Vào **Device Info** → ghi lại **Hardware Version + Software Version** (quyết định protocol: HW < 11 → `JK02_24S`, HW ≥ 11 → `JK02_32S`). Số "11" khoanh tròn trên nhãn QC **có thể chỉ là mã kiểm phẩm**, không được coi là hardware version.
3. Bấm preset **LFP** (nút "One-Click LFP" nếu có) rồi chỉnh tay:

| Thông số | Giá trị cho pack 8S LFP 30Ah | Vì sao |
|---|---|---|
| **Cell Count** | **8** | ⚠️ Xuất xưởng mặc định 24 — không đổi là báo lỗi "cell count not equal" nhấp nháy |
| Battery Capacity | 30 Ah | SOC đếm Coulomb mới đúng |
| Cell OVP / OVPR | 3.65 V / 3.55 V | Trần sạc LFP |
| Cell UVP / UVPR | 2.85 V / 3.05 V | Sàn xả an toàn (chặt hơn 2.5V) |
| Power-Off Voltage | 2.50 V | BMS tự ngắt cứu pin |
| Charge OCP | 30 A | ≈1C của pack 30Ah — sạc thực tế nên ≤15A (0.5C) |
| Discharge OCP | ≤100 A | Trần của BMS; đặt theo tải thật của bạn |
| Balance Start Voltage | 3.20 V | Chỉ balance khi cell đã lên vùng knee |
| Balance Trigger Delta | 0.010 V | Chênh ≥10mV mới kích |

4. So sánh **điện áp từng cell trên app với 8 số đã đo tay** ở §2.1 — lệch ≤ ±0.01V là đạt. Lệch lớn ở 1 kênh = dây balance kênh đó tiếp xúc kém.

**PASS giai đoạn 1 khi:** app hiển thị đủ 8 cell đúng điện áp, không cảnh báo đỏ, nhiệt độ hiển thị hợp lý.

> Lưu ý tính năng **Smart Sleep**: dòng nạp/xả < 1A liên tục ~26h → BMS tự ngủ để tiết kiệm điện. Đánh thức bằng nút switch hoặc cắm sạc. Khi test bench nhiều ngày, đừng hoảng nếu BMS "biến mất" khỏi app.

---

## 3. Giai đoạn 2 — Đọc BMS bằng laptop (cô lập lỗi trước khi đụng ESP32)

Mục tiêu: chứng minh **đường data RS485-Modbus hoạt động** bằng công cụ chuẩn, để sau này nếu ESP32 đọc không được thì biết chắc lỗi ở phía ESP32/firmware chứ không phải BMS.

### 3.1 Bật Modbus slave trong app

Vào Parameter Settings → **Device Address** → đặt = **1** (dải hợp lệ 1–15).
⚠️ **Address 0 = master mode** — BMS tự phát broadcast chứ KHÔNG trả lời Modbus request. Để 0 là cổng RS485 "im lặng" với mọi câu hỏi → triệu chứng giống chết cổng. Đây là bẫy số 1.

### 3.2 Đấu dây RS485 ra laptop

1. Cắm pigtail JST vào cổng **CAN&RS485** (đo pitch connector thực tế trước khi mua pigtail — 1.25mm hay 2.0mm **[PHẢI TỰ KIỂM CHỨNG]**).
2. Xác định chân **A / B / GND** của cổng này: xem tờ manual trong hộp hoặc hỏi seller. **[PHẢI TỰ KIỂM CHỨNG — pinout cổng này trên bản vỏ nhôm chưa có tài liệu công khai.]** Nếu không có tài liệu: RS485 idle đo được A–B chênh ~+0.2–0.5V (A cao hơn B); và đấu thử sai A/B **không cháy gì** — chỉ không nhận frame, đảo lại là được.
3. Nối A→A, B→B, GND→GND vào adapter **USB-RS485** → laptop.

### 3.3 Đọc thử

- Cách 1 — **JK PC Monitor** v3.4.0 (tải từ jkbms.com/support): chọn cổng COM, baud **115200**, thấy dashboard số liệu là PASS.
- Cách 2 — **modpoll** (kiểm tra "sạch" theo đúng contract firmware sẽ dùng):
  ```
  modpoll -m rtu -b 115200 -p none -a 1 -t 4 -r 0x1200 -c 10 COM3
  ```
  (function 0x03, thanh ghi nhóm `0x1200` = realtime data). Nhận về dãy số thay đổi theo điện áp pack là PASS.

**PASS giai đoạn 2 khi:** laptop đọc được voltage/current/SOC qua RS485 khớp với app BLE.

*(Tùy chọn — đường dự phòng UART: cắm USB-TTL 3.3V vào cổng **UART**, chỉ 3 dây GND/RX/TX chéo nhau, **chân VBAT ~25.6V BỎ TRỐNG — nối vào laptop/ESP32 là hỏng ngay**, mở serial 115200 thấy frame bắt đầu `4E 57` là cổng sống. Cổng này nói JK-native, không phải Modbus.)*

---

## 4. Giai đoạn 3 — Lắp node ESP32-S3 + sensor (breadboard)

Bám đúng bảng GPIO của [wiring-diagram.md](wiring-diagram.md) (khớp `config.example.h` của firmware). Lắp và test **từng khối một** — cắm hết một lượt rồi mới test là tự làm khó mình khi debug.

### 4.0 Nguyên tắc vàng
- **COMMON GROUND**: GND của ESP32 = GND mọi module = **chân GND của cổng CAN&RS485 trên BMS**. Đây là lỗi đấu dây phổ biến nhất của toàn hệ.
- ESP32-S3 là **logic 3.3V** — không đưa bất kỳ tín hiệu 5V nào thẳng vào GPIO.

### 4.1 Nguồn (giai đoạn dev)
- Cấp bằng USB-C từ laptop hoặc adapter **5V ≥ 2A** vào VIN.
- Hàn/cắm **tụ 470–1000µF** giữa VIN–GND, sát ESP32 (chống sụt áp khi WiFi/TLS phát → hết reset vặt).

### 4.2 Khối RS485 (đường đọc BMS chính)

| ESP32-S3 | MAX485 | Ghi chú |
|---|---|---|
| 3V3 | VCC | XY-017 chạy được 3.3V; nếu module của bạn là bản 5V-only thì cấp 5V nhưng RO phải qua chia áp về 3.3V **[kiểm tra datasheet module khi mua]** |
| GND | GND | chung ground |
| GPIO17 | DI | `BMS_RS485_TX_PIN` |
| GPIO18 | RO | `BMS_RS485_RX_PIN` |
| GPIO16 | DE + RE (nối chung) | bản auto-direction thì bỏ, set `BMS_RS485_DE_PIN = -1` |
| — | A | → chân A cổng CAN&RS485 (đã xác định ở §3.2) |
| — | B | → chân B |

Bus ngắn (<2m, 1 thiết bị) **không cần** 120Ω; chỉ gắn nếu sau này thấy lỗi CRC/frame.

### 4.3 Khối sensor đo chéo (SensorMismatch — S5-FW-04/05)

**INA226** (I2C `0x40`): VCC→3V3, GND→GND, SDA→GPIO8, SCL→GPIO9. Đo điện áp pack: chân VBUS → **dương pack qua 1 cầu chì nhỏ/điện trở bảo vệ**; 8S đầy ~29.2V < 36V max của INA226 → an toàn.
⚠️ **Chưa gắn shunt ngoài (STT 17) thì KHÔNG bật đo dòng trong firmware** — số current rác sẽ bị backend đếm là outlier, **>50 outlier/giờ là device bị tự động vô hiệu hóa** (xem [iot-backend-contract-gaps.md](iot-backend-contract-gaps.md) mục #5).

**DS18B20** (1-Wire): VCC→3V3, GND→GND, DATA→GPIO4 + điện trở **4.7kΩ kéo từ DATA lên 3V3** (thiếu trở là không đọc được). Đầu probe dán lên vỏ cell cạnh probe NTC của BMS (để so nhiệt 2 nguồn).

### 4.4 Khối môi trường (S5-FW-06, S6-FW-01/02)

**SHT31** (I2C `0x44`): cắm chung bus GPIO8/9 với INA226 — khác địa chỉ nên không đụng nhau.
**MQ-2**: VCC→**5V**, GND→GND, **AO → chia áp 10kΩ+20kΩ → GPIO1** (AO mức 5V, đưa thẳng vào GPIO là hỏng chân ADC). Cần ~30s warm-up sau cấp nguồn — firmware đã xử lý.
**Water leak (LM393)**: VCC→3V3, GND→GND, DO→GPIO2 (INPUT_PULLUP, ướt→LOW — đúng mặc định firmware).

### 4.5 Nguồn thực địa (khi rời bàn dev)

```
Pack(+) ─► [Cầu chì 2–5A] ─► LM2596 IN+          LM2596 OUT+ ─► ESP32 VIN
Pack(−) ─────────────────► LM2596 IN−            LM2596 OUT− ─► ESP32 GND
```
**Chỉnh LM2596 ra đúng 5.0V bằng đồng hồ TRƯỚC khi cắm vào ESP32** (biến trở trên module vặn được ra tới ~30V — cắm mù là chết board). Cầu chì là bắt buộc khi lấy nguồn từ pin. UPS TP4056+18650 (STT 31) là tùy chọn cho demo LWT offline.

### 4.6 Checklist trước khi cấp nguồn (in ra, tick từng dòng)

- [ ] Tất cả GND nối chung (ESP32 + MAX485 + INA226 + SHT31 + MQ-2 + water + GND cổng RS485 của BMS)
- [ ] Không có dây 5V nào cắm vào chân GPIO/3V3
- [ ] MQ-2 AO đã qua chia áp; đo đầu ra chia áp ≤3.3V trước khi nối vào GPIO1
- [ ] DS18B20 có trở 4.7kΩ DATA↔3V3
- [ ] Chân VBAT của cổng UART BMS (nếu có đấu pigtail) đang **bỏ trống, bọc cách điện**
- [ ] LM2596 đã chỉnh đúng 5.0V (đo khi chưa tải)
- [ ] Đồng hồ đo thông mạch VCC–GND trên breadboard: **không** kêu (không ngắn mạch)
- [ ] App JK: Device Address = 1, Cell Count = 8

---

## 5. Giai đoạn 4 — Firmware + kết nối backend

### 5.1 Cấu hình firmware

Copy `firmware-esp32/include/config.example.h` → `config.h` (file này gitignored), sửa:

```c
// WiFi + backend
#define WIFI_SSID   "..."
#define WIFI_PASS   "..."
#define BACKEND_URL "https://<ip-laptop>:7200"
#define DEVICE_CODE "gw-esp32-mvp-001"        // BẮT BUỘC chữ thường
#define API_KEY     "iotk_..."                 // lấy từ bước provision backend

// MQTT
#define MQTT_BROKER_HOST "<ip-laptop>"
#define MQTT_BROKER_PORT 8883                  // TLS; dev có thể 1883 + MQTT_USE_TLS 0

// BMS — JK-BD6A24S10P qua RS485-Modbus
#define USE_MOCK_BMS     0
#define BMS_MODEL        3                     // 3 = JK-BMS preset
#define BMS_RS485_BAUD   115200UL              // ⚠️ đổi từ 9600 mặc định
#define BMS_RS485_TX_PIN 17
#define BMS_RS485_RX_PIN 18
#define BMS_RS485_DE_PIN 16                    // -1 nếu MAX485 auto-direction
#define BMS_UNIT_ID_START 1                    // = Device Address đã đặt trong app
#define BMS_UNIT_ID_COUNT 1                    // rig của bạn chỉ có 1 BMS
```

⚠️ **Trước khi tin số liệu:** preset JK trong `firmware-esp32/src/bms/bms_register_map.h` (nhóm `0x1200`) được viết từ tài liệu chung — phải **đối chiếu từng offset/scale** với PDF chính thức `BMS RS485 Modbus V1.1` (trong repo [syssi/esphome-jk-bms](https://github.com/syssi/esphome-jk-bms/tree/main/docs/pb2a16s20p)) và với giá trị thật trên app. Sai scale là voltage 25.6V thành 2.56V mà không ai báo lỗi.

Build & flash: env `esp32-s3-real` (và chạy test env `native` + build cả `esp32-s3-devkitc-1` theo quy trình verify của repo).

### 5.2 Checklist backend (từ [iot-backend-contract-gaps.md](iot-backend-contract-gaps.md) — cả 2 bẫy đều fail im lặng)

- [ ] `Mqtt__Enabled=true` cho BatteryService (mặc định **false** → firmware tưởng gửi OK nhưng backend không nhận gì)
- [ ] API key của device có scope `EnvironmentalIngest` nếu test SHT31/MQ-2/rò nước (default scope **không có** → 403 → firmware drop luôn)
- [ ] DB dev không còn row rác `id = Guid.Empty` trong bảng idempotency
- [ ] Site đã có lat/lon nếu muốn WeatherSync (Open-Meteo) chạy
- [ ] Provision device trước → lấy `API_KEY` + MQTT credentials điền vào `config.h`

### 5.3 Nghiệm thu end-to-end

1. Serial monitor: `[ingest] MQTT posted N readings...` mỗi 5s, không lỗi poll BMS.
2. Số liệu ESP32 đọc được (V/I/SOC) **khớp app JK BMS** (±0.05V).
3. Query TimescaleDB: `SELECT * FROM sensor_readings ORDER BY time DESC LIMIT 10;` — có row mới, `source_type=1`, đúng serial.
4. So `totalReceived` vs `inserted` trong response backend — `skipped > 0` nghĩa là có reading bị loại im lặng (xem gaps #6).
5. Rút dây mạng/tắt WiFi 2 phút → bật lại → queue LittleFS tự gửi bù, không mất data.

---

## 6. Sự cố thường gặp (troubleshooting)

| Triệu chứng | Nguyên nhân khả dĩ nhất | Cách xử lý |
|---|---|---|
| BMS không lên nguồn sau khi cắm harness | Dây **B+ chưa nối** vào dương pack (balancer không có điện) | Kiểm tra B+ theo §2.3; thử bấm nút switch nếu có |
| App báo lỗi cell count / LED nhấp nháy | **Cell Count vẫn = 24** (mặc định) | App → đặt Cell Count = 8 |
| App hiện 1 cell = 0V hoặc điện áp lộn xộn | Dây balance kênh đó tiếp xúc kém / đấu sai thứ tự | Đo lại từng chân connector theo §2.3 bước 2 |
| Cổng RS485 hoàn toàn im lặng | **Device Address = 0** (master mode) — bẫy số 1 | App → Device Address = 1 |
| RS485 có tín hiệu nhưng toàn lỗi CRC | Đảo A/B, sai baud (phải 115200), hoặc thiếu GND chung | Đảo A/B; check baud; nối GND |
| ESP32 đọc được nhưng số liệu sai x10/x100 | Scale trong register map chưa khớp bản BMS này | Đối chiếu PDF Modbus V1.1 §5.1 |
| ESP32 reset ngẫu nhiên khi WiFi bật | Nguồn yếu / thiếu tụ lọc | Adapter ≥2A + tụ 470–1000µF sát VIN |
| App điện thoại không kết nối BLE được | Ai đó/thiết bị khác đang giữ kết nối BLE (BMS chỉ nhận 1) | Ngắt kết nối kia; đứng gần hơn (vỏ nhôm làm yếu sóng) |
| BMS "biến mất" sau vài ngày để không | **Smart Sleep** (dòng <1A trong ~26h) | Bấm nút switch hoặc cắm sạc để đánh thức |
| Firmware log "MQTT posted OK" nhưng DB trống | Backend `Mqtt__Enabled=false` — mất data im lặng | Bật flag, restart BatteryService |
| Ambient/incident trả 403 | API key thiếu scope `EnvironmentalIngest` | Cấp lại scope (mặc định không có) |
| Gửi N reading, DB chỉ có <N | Backend "skip" outlier im lặng (thường do current rác từ INA226 chưa có shunt) | Tắt đo dòng INA226 / gắn shunt; coi chừng auto-decommission >50/h |

---

## 7. Việc còn mở — phải chốt khi cầm đủ đồ

| # | Việc | Chốt bằng |
|---|---|---|
| 1 | **Hardware Version thật** của BMS (quyết định JK02_24S vs JK02_32S nếu dùng đường UART/BLE) | App → Device Info |
| 2 | **Pinout A/B/GND cổng CAN&RS485** trên bản vỏ nhôm | Manual giấy trong hộp / hỏi seller / đo theo §3.2 |
| 3 | **Pitch connector JST** (1.25 vs 2.0mm) để mua đúng pigtail | Đo bằng thước kẹp khi cầm BMS |
| 4 | Harness balance thực tế (1 hay 2 connector, vị trí dây B+ / 温度) | Đối chiếu nhãn in trên vỏ + manual giấy |
| 5 | Hộp có kèm cáp màn hình + mấy probe NTC | Mở hộp kiểm kê |
| 6 | Register map `0x1200` khớp bản BMS này từng field | modpoll đối chiếu app (§3.3) trước khi sửa firmware |

---

*Guide này viết cho đúng rig cá nhân 1 pack + 1 node; bản tham chiếu team (multi-drop 4 pack) xem [wiring-diagram.md](wiring-diagram.md). Khi có thay đổi phần cứng (đổi BMS, thêm pack), cập nhật guide này cùng lúc với hardware.csv.*
