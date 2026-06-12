# Glossary — Thuật ngữ dự án IoT Capstone

> **Mục đích:** Member mới đọc 10 phút hiểu được vocab cơ bản trước khi vào sprint code.
> **Acceptance S0-QA-01:** ≥ 15 thuật ngữ.
> **Cập nhật:** Bất cứ ai gặp thuật ngữ mới chưa có ở đây → thêm vào ngay (PR `docs:`).

---

## 1. BMS — Battery Management System
Bo mạch điện tử gắn kèm pin lithium/LiFePO4, đo voltage / current / nhiệt độ / SOC / SOH và bảo vệ pin (over-voltage, over-current, over-temp). Dự án này đọc dữ liệu BMS qua **Modbus RTU over RS485**. Model tham khảo: Daly, JBD, JK-BMS.

## 2. Modbus RTU
Giao thức công nghiệp dạng master/slave chạy trên RS485. ESP32 là **master** (poll), BMS là **slave**. Mỗi slave có một **unitId** (1..247) để phân biệt khi đấu **multi-drop** (nhiều slave chung 1 bus A/B).

## 3. unitId (Modbus slave ID)
Địa chỉ logic của slave trên bus Modbus. Mua 4 pin BMS giống nhau → **bắt buộc đổi unitId** thành 1, 2, 3, 4 bằng phần mềm hãng trước khi đấu chung bus. Nếu không, 2 BMS cùng id sẽ tranh đáp khi master poll → đọc lỗi.

## 4. RS485 — Half-duplex differential bus
Chuẩn vật lý truyền 2 dây A/B + GND chung, chống nhiễu tốt, đi xa tới 1.2km. Bus phải có **điện trở terminator 120Ω** ở 2 đầu xa nhất để chống dội tín hiệu. Module phổ biến: **MAX485** (XY-017 auto-direction tiện hơn vì không cần điều khiển DE/RE).

## 5. DE/RE (Driver Enable / Receiver Enable)
Hai chân điều khiển hướng truyền trên MAX485 (KHÔNG phải auto-direction). Phải set HIGH **trước khi** ESP32 gửi, LOW **sau khi** gửi xong để chuyển sang nhận. Quên là Modbus reply không bao giờ về. Tham khảo `wiring-diagram.md §3`.

## 6. Common ground (chung GND)
Mọi thiết bị nối chung dây GND ở 1 điểm — nếu không, điện áp tín hiệu sẽ trôi → đọc voltage sai hoặc bằng 0. Quy tắc bắt buộc của `wiring-diagram.md §8`.

## 7. LWT — Last Will and Testament (MQTT)
Cơ chế MQTT: client (ESP32) khai báo với broker "nếu tao mất kết nối đột ngột thì hãy publish message này lên topic kia với QoS 1, retain". Dự án dùng `solar/{deviceCode}/status` payload `offline` — backend nhận tức thì, mark device Offline trong ≤ 90s thay vì đợi job heartbeat 5 phút.

## 8. QoS (Quality of Service) MQTT
- **QoS 0** — fire-and-forget, không đảm bảo
- **QoS 1** — at-least-once (broker ack, có thể trùng)
- **QoS 2** — exactly-once (handshake 4 bước, chậm)

Dự án dùng **QoS 1** cho telemetry + LWT. Idempotency-Key chống trùng ở backend.

## 9. Retained message (MQTT)
Broker giữ lại message cuối cùng trên 1 topic, gửi ngay cho client mới subscribe. Dự án dùng cho `status` topic để client mới biết trạng thái device hiện tại không cần đợi message tiếp theo.

## 10. ACL — Access Control List (MQTT)
Quy tắc per-user trên broker: device A chỉ được publish topic `solar/+/{deviceCodeA}/...`, không thể giả mạo device B. Config trong `acl.conf` của EMQX/Mosquitto. Sprint 4 sẽ làm.

## 11. mTLS — mutual TLS
Cả server (broker) và client (ESP32) đều verify cert nhau. Client cũng cần cert riêng (không chỉ password). Dự án S4 sẽ làm bản đơn giản: broker có server cert (CA tự ký), ESP32 verify CA + auth bằng username/password.

## 12. Hypertable (TimescaleDB)
Bảng "ảo" trên Postgres tự chia thành nhiều **chunk** theo thời gian, cho phép insert/query time-series triệu rows mượt. Dự án dùng cho `sensor_readings`, `ambient_readings`, `heartbeat`. Vẫn query bằng SQL bình thường.

## 13. Chunk (TimescaleDB)
Mỗi mảnh nhỏ của hypertable, mặc định 7 ngày. Drop chunk cũ = drop partition, rất nhanh — đây là cách dự án xóa dữ liệu cũ thay vì `DELETE WHERE timestamp < ...`.

## 14. Idempotency-Key
Header HTTP `Idempotency-Key: <uuid>` đính kèm POST batch readings. Backend lưu key này; nếu cùng key gửi lại 2 lần (do retry sau timeout) → chỉ ghi DB 1 lần. **Quan trọng** với firmware: queue mỗi batch sinh 1 key cố định, retry bao nhiêu lần cũng dùng key đó (xem `tasksprint.md S3-FW-02`).

## 15. Clock skew
Lệch giờ giữa ESP32 và backend. Nếu ESP32 không sync NTP → gửi `deviceTimestamp=1970-01-01` → backend reject (out-of-window). Quy tắc: NTP sync lúc boot + retry mỗi 5 phút; lệch > 5 phút thì pause publish. Tham khảo `newiot.md §12` bẫy #1.

## 16. NTP — Network Time Protocol
ESP32 đồng bộ giờ với pool server qua UDP port 123. `vn.pool.ntp.org` ưu tiên, fallback `pool.ntp.org`. Sau sync, ESP32 lưu giờ UTC trong RTC nội bộ.

## 17. NVS — Non-Volatile Storage (ESP32)
Vùng flash key/value để lưu config (apiKey, deviceCode, pollingInterval) qua các lần reboot. Khác với LittleFS — NVS dành cho key/value nhỏ, LittleFS dành cho file (CA cert).

## 18. OTA — Over-The-Air firmware update
Cập nhật firmware từ xa qua HTTPS. ESP32 có 2 partition (`ota_0`, `ota_1`) — flash xuống partition kia, verify sha256, set boot partition, reboot. Boot fail health → tự rollback partition cũ. Sprint 7.

## 19. SOH — State of Health (%)
Phần trăm dung lượng pin còn lại so với khi xuất xưởng. SOH 80% nghĩa là pin chỉ còn 80% dung lượng so với mới. Pin EOL (end-of-life) ≈ SOH < 80%. AI module dự đoán SOH bằng LSTM/CNN-LSTM.

## 20. SOC — State of Charge (%)
Phần trăm năng lượng hiện đang chứa trong pin. SOC 60% = pin đang đầy 60%. BMS cung cấp số này. Lưu ý: SOC ≠ SOH.

## 21. Cross-source validation (SensorMismatch)
Mỗi pin có ≥ 2 nguồn đo voltage: BMS (Modbus) + INA226 (analog/I2C). Nếu lệch > ngưỡng → tạo `AnomalyType=SensorMismatch`. Đảm bảo 1 nguồn cảm biến hỏng vẫn phát hiện được. Tham khảo `newiot.md §7.6`.

## 22. sensorSourceCode + sourceType
Mỗi `SensorReading` tag rõ nguồn vật lý:
- `sourceType=Bms (1)`, `sensorSourceCode="primary"` — BMS Modbus
- `sourceType=IotGateway (2)`, `sensorSourceCode="redundant"` — INA226
- `sourceType=IotGateway (2)`, `sensorSourceCode="external-temp"` — DS18B20

**Sai sourceType** → cross-source validation không trigger (cảnh báo lớn ở risk register `tasksprint.md §7`).

## 23. Heartbeat
Message device gửi định kỳ (60s) báo "tao còn sống" + metric runtime (chip temp, free heap, RSSI, queue depth). Backend `LastSeenAt` dùng để detect offline (no heartbeat > 5 phút → Offline).

## 24. Provisioning
Lần đầu device boot với apiKey hard-coded (status=Provisioning trong backend) → gọi `/provision` 1 lần để nhận config (pollingInterval, mapping battery), lưu NVS, chuyển status Active. Sprint 2.

## 25. Outbox pattern
Khi service vừa cần ghi DB vừa cần publish event RabbitMQ, dùng bảng `outbox`: ghi event vào DB cùng transaction, background job đọc bảng đẩy lên broker. Đảm bảo atomic — không mất event khi crash giữa 2 thao tác.

## 26. Saga (Alert–Ticket)
Workflow distributed: BatteryService publish `BatteryAnomalyDetectedEvent` → TicketService consume tạo Ticket. Trước có race condition (Ticket trùng) → Sprint 5B backend đã active Alert–Ticket Saga để dedup. IoT track CHỈ emit event đúng schema, không động vào TicketService.

## 27. ADR — Architecture Decision Record
File `docs/adr/000X-...md` ghi quyết định kiến trúc + lý do + alternatives. Ví dụ `ADR-017` cấm Energy/CO2 analytics trong scope capstone.

## 28. Scope guard (CI)
Hook backend chặn keyword `EnergySession|EnergyKwh|CapacityKw|CarbonEmission|...`. Mục đích: tránh scope creep dashboard kWh/CO2 — vi phạm ADR-017. Bất cứ ai muốn thêm phải mở ADR mới.

## 29. Cấp 0/1/2/3/4 (procurement)
Phân nhóm linh kiện theo thứ tự lead time + sprint cần:
- **Cấp 0** — ESP32 + cáp, có ngay đầu S0
- **Cấp 1** — MAX485, cáp xoắn, RS485 accessories, dùng S5
- **Cấp 2/3** — sensor phụ INA226/DS18B20/SHT31/MQ-2
- **Cấp 4** — enclosure IP65, UPS, 4G fallback, dùng S8

Đặt sớm hơn 1-2 sprint vì lead time.

## 30. EMQX vs Mosquitto
2 MQTT broker phổ biến. **EMQX** có dashboard sẵn (UI quản lý device/topic dễ), khuyến nghị cho capstone. **Mosquitto** nhẹ hơn, config file, không có UI — fallback nếu EMQX nặng.

---

## Xem thêm
- `newiot.md` §0 — tổng quan thuật ngữ ngắn
- `overall.iot.md` §A — BOM linh kiện chi tiết
- `wiring-diagram.md` §3,4 — đấu nối RS485 + sensor
