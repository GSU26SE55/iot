# infra/ — Hạ tầng dev (Sprint 0)

Toàn bộ service phụ trợ chạy bằng Docker compose. Dev không cần cài Postgres/Redis/RabbitMQ/EMQX local.

## Quick start

```bash
cd infra
./setup.sh                    # auto: tạo .env + check port + up + verify
```

`setup.sh` thay cho quy trình thủ công bên dưới — chạy 1 lệnh, idempotent (chạy lại không hỏng).

### Hoặc chạy thủ công

```bash
cd infra
cp env.example.txt .env       # ⚠ BẮT BUỘC trước khi up lần đầu (xem Troubleshooting #1)
docker compose -f docker-compose.dev.yml up -d
docker compose -f docker-compose.dev.yml ps
```

Đợi ~30s, tất cả service phải ở `(healthy)`:

```
NAME              STATUS                    PORTS
iot-emqx          Up X seconds (healthy)    0.0.0.0:1883->1883/tcp, ...
iot-rabbitmq      Up X seconds (healthy)    0.0.0.0:5672->5672/tcp, ...
iot-redis         Up X seconds (healthy)    0.0.0.0:6379->6379/tcp
iot-timescaledb   Up X seconds (healthy)    0.0.0.0:5432->5432/tcp
```

## Port map (acceptance S0-INF-02)

| Service | Port | Mục đích |
|---------|------|---------|
| TimescaleDB | 5432 | Postgres + extension timescaledb |
| Redis | 6379 | Cache + pub/sub |
| RabbitMQ | 5672 | AMQP |
| RabbitMQ Mgmt | 15672 | UI http://localhost:15672 (guest/guest) |
| EMQX MQTT | **11883** | Host port shift (tránh đụng Mosquitto local 1883). Container vẫn 1883. Override qua `EMQX_MQTT_PORT` |
| EMQX MQTT/TLS | **18883** | S4 — TLS sau. Override qua `EMQX_MQTTS_PORT` |
| EMQX WS | **18030** | Web client. Override qua `EMQX_WS_PORT` |
| EMQX Dashboard | 18083 | UI http://localhost:18083 (admin/public). Override qua `EMQX_DASHBOARD_PORT` |

## Verify từng service

```bash
# TimescaleDB
docker compose -f docker-compose.dev.yml exec timescaledb \
  psql -U postgres -d battery_service_dev -c '\dx'   # phải thấy timescaledb

# Redis
docker compose -f docker-compose.dev.yml exec redis redis-cli ping   # PONG

# RabbitMQ
curl -u guest:guest http://localhost:15672/api/overview | jq .rabbitmq_version

# EMQX
curl http://localhost:18083/api/v5/status   # 200 OK
```

## Tắt và dọn

```bash
docker compose -f docker-compose.dev.yml down        # giữ data
docker compose -f docker-compose.dev.yml down -v     # XÓA data (cần khi muốn rerun init SQL)
```

## Troubleshooting

### #1 — TimescaleDB báo `role "postgres" does not exist`

**Triệu chứng:** healthcheck fail liên tục, log `FATAL: role "postgres" does not exist`.

**Nguyên nhân:** Lần `docker compose up` đầu tiên chạy trước khi có `.env` → biến `POSTGRES_USER`/`POSTGRES_PASSWORD` rỗng → initdb bỏ qua tạo superuser. Postgres image chỉ chạy initdb **một lần** khi volume rỗng — các lần `up` sau sẽ skip dù bạn đã sửa env.

**Fix:**
```bash
docker compose -f docker-compose.dev.yml down -v    # XÓA volume → cho phép initdb chạy lại
./setup.sh                                          # bootstrap đúng thứ tự
```

**Phòng tránh:** Luôn chạy `./setup.sh` thay vì `docker compose up` thủ công — script đảm bảo `.env` tồn tại trước khi up.

### #2 — Port đã có process khác chiếm

**Triệu chứng:** `Error response from daemon: ports are not available: ... bind: address already in use`.

**Check process nào đang chiếm:**
```bash
lsof -nP -iTCP:1883 -sTCP:LISTEN     # đổi port theo lỗi
```

**Fix:**
- Tắt process kia (vd: `brew services stop mosquitto`, `brew services stop postgresql`)
- HOẶC đổi port host trong `.env` (xem các biến `*_PORT`), rerun `./setup.sh`

Default ports đã shift sẵn để tránh xung đột phổ biến:
- EMQX MQTT: **11883** (thay vì 1883 — tránh Mosquitto local)
- EMQX MQTT/TLS: **18883** (thay vì 8883)
- EMQX WS: **18030** (thay vì 8083)

### #3 — `setup.sh` báo "Port X đang bị chiếm" nhưng service vẫn healthy

False positive khi container của chính `iot-dev` đang chạy: `lsof` thấy `com.docker` giữ port → cảnh báo. Có thể bỏ qua.

Kiểm tra thật bằng:
```bash
docker compose -f docker-compose.dev.yml ps    # nếu service healthy → OK
```

## Cấu trúc

```
infra/
├── docker-compose.dev.yml
├── env.example.txt
├── db/
│   ├── init/                  ← SQL auto-run khi initdb (S0-INF-03)
│   │   ├── 01-create-databases.sql
│   │   └── 02-enable-timescaledb.sql
│   └── README.md
└── mqtt/
    ├── README.md
    ├── emqx/etc/              ← placeholder S4 (acl, users, certs)
    └── mosquitto/config/      ← placeholder fallback
```
