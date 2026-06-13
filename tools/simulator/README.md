# ESP32 Simulator — Sprint IoT-1 (#250)

Mô phỏng ESP32 edge device đẩy data lên BatteryService — dùng để chạy end-to-end test mà không cần phần cứng thật.

## Cài

```bash
pip install requests
```

## Chạy

1. Tạo device qua admin endpoint (xem `/api/admin/iot-devices`) → lưu `rawApiKey` trả về (chỉ trả 1 lần).
2. Chạy simulator:

```bash
python3 esp32_simulator.py \
  --base-url https://localhost:7200 \
  --device-code ESP32-SIM-001 \
  --api-key iotk_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx \
  --battery-serials BAT-001,BAT-002 \
  --insecure
```

Hoặc dùng env vars:

```bash
export IOT_BASE_URL=https://localhost:7200
export IOT_DEVICE_CODE=ESP32-SIM-001
export IOT_API_KEY=iotk_...
export IOT_BATTERY_SERIALS=BAT-001,BAT-002
python3 esp32_simulator.py --insecure
```

## Chức năng

| Tính năng | Mô tả |
|-----------|-------|
| Provision | Gọi `POST /api/iot-devices/provision` ngay khi start. |
| Heartbeat | Tick mỗi 60s (config bằng `--heartbeat-interval`). |
| Ingest | Tick mỗi 15s, gửi batch 6 reading mock (sine voltage + random noise). |
| Local queue | Khi backend down → save batch vào `./queue.jsonl` với `Idempotency-Key`. Khi backend trở lại → flush. |
| Mock BMS | `SensorReading.mock_for()` — không cần BMS thật, đủ để test pipeline. |

## Test scenario

```bash
# 1. Start simulator
python3 esp32_simulator.py --insecure &

# 2. Stop backend → simulator queue lên
docker compose stop battery-service

# 3. Wait 2 min, check queue.jsonl size

# 4. Start backend → simulator auto-flush
docker compose start battery-service

# 5. Verify trong DB: SELECT COUNT(*) FROM sensor_readings WHERE source_type = 2 AND time > NOW() - INTERVAL '5 min';
```

## ESP32 hardware (Sprint IoT-1 #251)

Khi có phần cứng thật, port Python script này sang Arduino C++ — xem `../hardware/esp32-firmware/README.md`. Logic queue + retry với `Idempotency-Key` giống nhau.
