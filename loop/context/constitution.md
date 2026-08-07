# Hiến pháp — repo iot (firmware ESP32 + simulator)

> Tầng T0. Nạp vào MỌI lần gọi agent.

---

## 1. Dự án này làm gì

Firmware ESP32-S3 đặt tại site điện mặt trời: đọc BMS (Modbus RTU) + cảm biến rời
(INA226 dòng/áp, DS18B20 nhiệt độ pin, SHT31 nhiệt/ẩm môi trường, MQ-2 khí, cảm biến rò nước),
đóng gói telemetry rồi đẩy về backend qua **MQTT (chính)** hoặc **HTTPS (dự phòng)**. Mất mạng
thì xếp hàng xuống LittleFS, có mạng thì đẩy bù. Nhận lệnh từ xa và cập nhật firmware OTA.

**Hậu quả khi sai:** pin lithium-ion **cháy nổ được**. Bỏ sót cảnh báo quá nhiệt / rò khí /
rò nước là rủi ro tính mạng. Thiết bị nằm ngoài hiện trường — sai firmware nghĩa là phải cử
người tới tận nơi. **Thận trọng ở mức cao nhất.**

---

## 2. Bất biến

| # | Bất biến | Vì sao |
|---|---|---|
| 1 | Cảm biến an toàn (MQ-2 khí, rò nước) phải **tiếp tục lấy mẫu kể cả khi mất Wi-Fi/NTP** | Mất mạng không làm pin bớt cháy. Gắn việc đọc cảm biến vào trạng thái mạng là biến sự cố mạng thành sự cố an toàn |
| 2 | Telemetry **không được mất** khi mất mạng | Xếp hàng xuống LittleFS; chỉ xoá khỏi hàng đợi SAU khi backend xác nhận nhận |
| 3 | Đã đẩy MQTT thành công thì **KHÔNG** đẩy lại qua HTTPS | Backend sẽ nhận trùng. Chỉ fallback cho phần THỰC SỰ chưa gửi được |
| 4 | Mọi số đo publish ra phải là số đo **THẬT**. Đọc lỗi ⇒ bỏ qua/đánh dấu lỗi, **KHÔNG** publish 0 | Publish 0 khi I2C lỗi làm backend tưởng pin mất điện — cảnh báo giả, hoặc tệ hơn là che mất cảnh báo thật |
| 5 | Lỗi 4xx vĩnh viễn **không được retry vô hạn** | Vòng lặp chặt làm treo main loop, kéo theo cả cảm biến an toàn |
| 6 | So sánh mốc thời gian `millis()` phải chịu được **tràn số sau 49,7 ngày** | Dùng `(long)(now - deadline) >= 0` chứ không `now >= deadline` |
| 7 | Định danh thiết bị (`deviceCode`, apiKey, MQTT credential) chỉ ghi NVS khi **ghi trọn vẹn** | Ghi dở làm thiết bị mất danh tính, phải mang về xưởng |

---

## 3. Điều TUYỆT ĐỐI KHÔNG làm

- **KHÔNG** tắt xác minh chứng chỉ TLS (`setInsecure()`) trên đường ingest/OTA.
- **KHÔNG** in bí mật ra Serial/log: apiKey, mật khẩu MQTT, token. Chỉ in 4 ký tự cuối.
- **KHÔNG** chặn main loop bằng thao tác I/O dài. Thiết bị còn phải đọc cảm biến an toàn.
- **KHÔNG** publish giá trị mặc định/0 khi cảm biến đọc lỗi (xem bất biến #4).
- **KHÔNG** sửa `platformio.ini` để né lỗi biên dịch — nó là thước đo của CI.
- **KHÔNG** commit. Người dùng tự commit sau khi review.
- **KHÔNG** sửa test đang đỏ để nó xanh. Thêm test thì được.
- **KHÔNG** dùng `#ifdef USE_MOCK_BMS` — cờ này được định nghĩa **bằng 0** ở env real, nên
  `#ifdef` luôn đúng. Phải dùng `#if USE_MOCK_BMS`.

---

## 4. Ranh giới

```
src/sensor/*   đọc phần cứng, KHÔNG biết gì về mạng
src/core/*     đóng gói payload, khoá idempotency — logic thuần, test được ở env native
src/net/*      wifi / http / mqtt / ntp
src/queue/*    hàng đợi offline trên LittleFS
src/cmd/*      xử lý lệnh từ xa
src/ota/*      cập nhật firmware
```

Logic thuần (payload, backoff, queue, cmd, idempotency) **phải test được ở `env:native`** —
đó là lý do chúng tách khỏi lớp phần cứng. Thêm logic mới thì đặt vào chỗ test được.

---

## 5. Test

- `pio test -e native` — **118 test là baseline hiện tại, không được giảm.**
- Thêm test mới: được, và thường bắt buộc.
- **KHÔNG làm yếu test đã có**: không xoá assertion, không nới ngưỡng, không comment out.
- Test hồi quy phải **đỏ trên code cũ, xanh trên code mới**.
- Không có phần cứng thật ⇒ logic nào cần kiểm phải tách khỏi lớp driver.

---

## 6. Bẫy đã trả giá

- `include/config.h` **không nằm trong git** — sinh từ `config.example.h`. Thiếu nó thì lỗi là
  MÔI TRƯỜNG, không phải firmware sai.
- Env `esp32-s3-real` khai `-UUSE_MOCK_BMS` rồi `-DUSE_MOCK_BMS=0`: cờ **được định nghĩa, giá
  trị 0**. Code kiểm bằng `#ifdef` sẽ chạy nhầm nhánh mock trên bản real.
- `pio run` chỉ báo lỗi biên dịch, **không** chứng minh chạy đúng trên thiết bị. Xanh ở đây
  KHÔNG có nghĩa là đã kiểm chứng hành vi.
- Simulator (`tools/simulator/`) là Python, không dùng chung code với firmware — sửa một bên
  không tự động đúng bên kia.

---

## 7. Từ ngữ dễ hiểu nhầm

- **"queue"** ở đây là hàng đợi **bền vững trên LittleFS**, không phải hàng đợi trong RAM.
- **"mock"** = nguồn BMS giả sinh số liệu, dùng khi chưa có phần cứng; **không phải** test double.
- **"incident"** = sự cố môi trường (khí/rò nước/quá nhiệt), khác **"anomaly"** của backend.
- **"provision"** = nạp danh tính + cấu hình vào NVS lần đầu, không phải "cấp phát tài nguyên".
