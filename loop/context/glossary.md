# Từ điển — repo iot

| Từ | Trong code | Nghĩa chính xác |
|---|---|---|
| Danh tính thiết bị | `deviceCode` | Mã do backend cấp lúc provision, lưu NVS |
| Hàng đợi offline | `local_queue` | Bền vững trên **LittleFS**, không phải RAM |
| Nguồn BMS giả | `mock_bms` / `USE_MOCK_BMS` | Sinh số liệu khi chưa có phần cứng — KHÔNG phải test double |
| Sự cố môi trường | `environmental_incident` | Khí/rò nước/quá nhiệt. Khác "anomaly" của backend |
| Nạp cấu hình lần đầu | `provision` | Ghi danh tính + cấu hình vào NVS |
| Khoá chống trùng | `idempotency_key` | Backend dùng để khử bản ghi lặp khi gửi lại |
| Giãn thời gian thử lại | `backoff` | Có jitter; #914 nói mọi thiết bị đang dùng jitter giống nhau |

## Viết tắt

| | |
|---|---|
| BMS | Battery Management System — nguồn số đo chính, đọc qua Modbus RTU |
| NVS | Non-Volatile Storage của ESP32 — nơi giữ danh tính/cấu hình |
| LWT | Last Will and Testament — bản tin MQTT báo thiết bị rớt |
| OTA | Over-The-Air — cập nhật firmware từ xa |
| SOC / SOH | State of Charge / State of Health (%) |

## Luồng dữ liệu

```
cảm biến + BMS → payload → (MQTT chính | HTTPS dự phòng) → backend
                     ↓ mất mạng
                 LittleFS queue → đẩy bù khi có mạng
```

Cảm biến an toàn (MQ-2, rò nước) **không** phụ thuộc nhánh mạng — xem hiến pháp §2.
