# mock-backend — Sprint 1 helper

HTTPS server giả mô phỏng endpoint `POST /api/sensor-readings/batch` của
BatteryService. Cho phép test firmware ESP32 (S1-FW-06, S1-FW-07) khi backend
thật chưa sẵn sàng.

## Yêu cầu
- Python 3.10+
- `openssl` (cho self-signed cert — chạy 1 lần)
- Không cần `pip install` gì cả — chỉ stdlib

## Chạy

```bash
cd tools/mock-backend
python3 mock_backend.py
```

Output:
```
[cert] Sinh self-signed cert tại certs/ (1 lần)...
[cert] OK (server.crt, server.key)

  mock-backend listening on https://0.0.0.0 (all LAN):7200
  dashboard:  https://localhost:7200/
  readings:   https://localhost:7200/readings?limit=10
  healthz:    https://localhost:7200/healthz
  Ctrl-C để dừng
```

Mỗi POST batch in 1 dòng:
```
08:15:42  200 POST /api/sensor-readings/batch device=GW-ESP32-MVP-001 batch items=4 v=12.51V t=26.5°C soc=81.5%
```

Dashboard tự refresh 2s — xem live qua trình duyệt.

## Cho ESP32 trỏ về

Trong `firmware-esp32/include/config.h`:

```c
#define BACKEND_BASE_URL  "https://192.168.1.50:7200"  // IP LAN của laptop chạy mock-backend
```

ESP32 đã `setInsecure()` nên tự bỏ qua self-signed cert.

## Flags

| Flag | Mặc định | Mục đích |
|------|----------|---------|
| `--port N` | 7200 | Đổi port |
| `--no-tls` | (off) | HTTP plain — debug nhanh bằng curl không cần `-k` |
| `--fail-rate 0.3` | 0 | 30% request trả 500 — test retry firmware (Sprint 3) |
| `--require-api-key` | (off) | 401 nếu thiếu header `X-Api-Key` — giả lập Sprint 2 |
| `--api-key K` | — | Khi --require-api-key, so khớp exact với K |

## Endpoint cài sẵn

| Method | Path | Sprint | Hành vi |
|--------|------|--------|---------|
| POST | `/api/sensor-readings/batch` | 1 | Validate JSON shape + return 200 + log |
| POST | `/api/iot-devices/provision` | 2 | Stub trả `pollingIntervalSeconds=5` |
| POST | `/api/iot-devices/heartbeat` | 2 | Stub 200 |
| GET | `/` | — | Dashboard (auto refresh 2s) |
| GET | `/readings?limit=N` | — | N batch gần nhất dạng JSON |
| GET | `/healthz` | — | Liveness |

## Validation rule (khớp `tasksprint.md` Sprint 1 + `newiot.md §7.4` legacy)

| Field | Type | Required | Validation |
|-------|------|----------|------------|
| `items` | array | ✓ | 1..1000 phần tử |
| `items[].batteryAssetId` | string | ✓ | Guid format |
| `items[].time` | string | ✓ | ISO8601 UTC `YYYY-MM-DDTHH:MM:SSZ` |
| `items[].voltage` | number | ✓ | **0..1000** (NI §7.4) |
| `items[].current` | number | ✓ | — |
| `items[].temperature` | number | ✓ | **-50..150** (NI §7.4) |
| `items[].socPercent` | number | ✓ | 0..100 |
| `items[].cycleCount` | int | — | ≥ 0 |

> Sprint 3 (`S3-FW-04`) sẽ thêm: `deviceTimestamp` wrapper, `batteryAssetSerial`, `sourceType`, `sensorSourceCode`, `bmsErrorCode`, header `X-Device-Code` + `Idempotency-Key`.

Sai → **400** với `listErrors: [{field, detail}]`.

**Response:**
- Success: **200 OK** + `{isSuccess: true, data: {accepted: N}}` (theo S1-FW-06 AC)
- Validation fail: **400** + `{isSuccess: false, listErrors: [...]}`
- Bad JSON: **400**

## Test bằng curl (không cần ESP32)

```bash
python3 mock_backend.py --no-tls --port 7200 &

# Valid → 200
curl -s -X POST http://localhost:7200/api/sensor-readings/batch \
  -H 'Content-Type: application/json' \
  -H 'X-Api-Key: iotk_test' \
  -d '{"items":[{
    "batteryAssetId":"11111111-1111-4111-8111-000000000001",
    "time":"2026-06-13T08:15:42Z",
    "voltage":12.51,"current":-3.5,"temperature":26.5,"socPercent":81.5,
    "cycleCount":100
  }]}'
# → {"isSuccess": true, "data": {"accepted": 1}}

# Invalid temperature (out of NI §7.4 range -50..150) → 400
curl -s -X POST http://localhost:7200/api/sensor-readings/batch \
  -H 'Content-Type: application/json' \
  -d '{"items":[{"batteryAssetId":"11111111-1111-4111-8111-000000000001","time":"2026-06-13T08:15:42Z","voltage":12.5,"current":0,"temperature":999,"socPercent":50}]}'
# → 400 + listErrors[{field: "items[0].temperature", detail: "out of range [-50, 150] (got 999)"}]

# Voltage > NI §7.4 hard-limit 1000 → 400
curl -s -X POST http://localhost:7200/api/sensor-readings/batch \
  -H 'Content-Type: application/json' \
  -d '{"items":[{"batteryAssetId":"11111111-1111-4111-8111-000000000001","time":"2026-06-13T08:15:42Z","voltage":9999,"current":0,"temperature":25,"socPercent":50}]}'
# → 400 + listErrors[{field: "items[0].voltage", detail: "out of range [0, 1000] (got 9999)"}]
```
