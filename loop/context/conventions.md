# Quy ước — repo iot

## Thời gian

- So sánh `millis()` phải chịu được tràn số (49,7 ngày):
  `if ((long)(millis() - deadline) >= 0)` — **KHÔNG** `if (millis() >= deadline)`.
- Thời gian thực lấy từ NTP; NTP chưa đồng bộ **không được** chặn việc đọc cảm biến.

## Đọc cảm biến

- Đọc lỗi ⇒ trả về trạng thái lỗi tường minh, **KHÔNG** trả 0/giá trị mặc định.
- Một cảm biến hỏng **không được** làm hỏng vòng đọc của các cảm biến còn lại.

## Mạng

- Không `setInsecure()`. Chứng chỉ CA nằm trong `data/` (LittleFS).
- Retry phải có backoff + jitter **riêng theo thiết bị** (nếu không, cả đàn cùng gọi một lúc).
- 4xx vĩnh viễn: dừng, ghi log, **không** thử lại vô hạn.

## Log

- KHÔNG in apiKey / mật khẩu MQTT / token. Cần nhận diện thì in 4 ký tự cuối.

## Bộ nhớ

- Ghi NVS phải trọn vẹn — ghi dở làm mất danh tính thiết bị.
- Mount LittleFS thất bại: **không** format tự động khi chưa cân nhắc — format là xoá sạch
  hàng đợi offline lẫn chứng chỉ CA (#936).

## Test

- Logic thuần đặt ở `src/core`, `src/cmd`, `src/net/backoff`, `src/queue` để test được ở
  `env:native`.
- Thêm test: được. Làm yếu test đã có: không.
- `pio test -e native` phải giữ **≥ 118** test case.

## Git

- **KHÔNG commit/push.** Người dùng tự commit.
- `include/config.h` không vào git — sinh từ `config.example.h`.
