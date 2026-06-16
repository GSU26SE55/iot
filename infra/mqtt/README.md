# infra/mqtt — MQTT broker (Mosquitto 2.0)

Sprint 4 hoàn tất broker production-ready: TLS 8883 + ACL per-device + LWT + downlink.
Sprint 0–3 chỉ dùng plain 1883 anonymous. Bridge subscribe `solar/#` (xem
`backend/services/BatteryService/src/BatteryService.Infrastructure/Mqtt/`).

## Topic schema (overall.md §52.14)

| Topic | Direction | QoS | Retain | Owner |
|-------|-----------|-----|--------|-------|
| `solar/{deviceCode}/{batterySerial}/telemetry` | device → backend | 1 | false | S4-FW-04 |
| `solar/{deviceCode}/heartbeat`                 | device → backend | 1 | false | S2-FW-03 (HTTPS hiện tại) |
| `solar/{deviceCode}/status`                    | device → backend | 1 | **true** (LWT) | S4-FW-02/03 |
| `solar/{deviceCode}/cmd`                       | backend → device | 1 | false | #IoT2-25 |
| `solar/{deviceCode}/cmd/ack`                   | device → backend | 1 | false | S4-FW-05 |

## Bootstrap (lần đầu)

```bash
# 1. Tạo user backend-bridge cho MqttBridgeBackgroundService
./infra/mqtt/mosquitto/bootstrap.sh
#    → in plaintext password ra stdout → copy vào backend/.env.Docker:
#        Mqtt__Password=<password>

# 2. Sinh CA + server cert (chỉ nếu cần TLS / S4)
./infra/mqtt/scripts/gen-certs.sh
#    Tùy chọn: MQTT_SAN_IP=10.0.0.10 ./infra/mqtt/scripts/gen-certs.sh

# 3. Mở comment block `listener 8883` trong mosquitto/config/mosquitto.conf
sed -i '' 's/^# listener 8883/listener 8883/' infra/mqtt/mosquitto/config/mosquitto.conf
sed -i '' 's|^# cafile|cafile|; s|^# certfile|certfile|; s|^# keyfile|keyfile|; s|^# require_certificate|require_certificate|; s|^# tls_version|tls_version|' infra/mqtt/mosquitto/config/mosquitto.conf

# 4. Start broker — chọn 1 trong 2:
docker compose -f infra/docker-compose.dev.yml up -d mosquitto   # broker + DB + Redis + MQ
docker compose -f infra/mqtt/docker-compose.yml  up -d           # broker stand-alone
```

## ⚠ BẮT BUỘC — Enable backend MQTT bridge

Backend `MqttBridgeBackgroundService` **default Mqtt__Enabled=false** (an toàn — không tự connect broker khi chưa config). Sau bootstrap.sh, PHẢI bật trong `backend/.env.Docker`:

```bash
# backend/.env.Docker (tạo nếu chưa có — xem backend/env.prod.example)
Mqtt__Enabled=true
Mqtt__Host=mosquitto                            # docker hostname trong solar-net
Mqtt__Port=1883                                  # dev plain | 8883 TLS
Mqtt__UseTls=false                               # dev | true cho TLS
Mqtt__AllowUntrustedCertificates=true            # dev với CA tự ký
Mqtt__Username=backend-bridge
Mqtt__Password=<plaintext-từ-bootstrap.sh>       # ⚠ thay placeholder
Mqtt__ClientId=battery-service-bridge
Mqtt__ReconnectIntervalSeconds=5
```

```bash
# Rebuild + restart backend
docker compose -f backend/docker-compose.yml up -d --force-recreate batteryservice

# Verify bridge connected — log phải có dòng:
docker logs solar-batteryservice 2>&1 | grep -i "MQTT bridge"
# Expected: "MQTT bridge connected to broker, 4 subscriptions"
# Nếu thấy: "MQTT bridge disabled (Mqtt:Enabled=false)" → chưa restart backend
```

**Lý do làm prominent step này:** thiếu `Mqtt__Enabled=true` thì **toàn bộ Sprint 4 deliverable fail silent**: FW MQTT publish OK lên broker, backend không subscribe → telemetry không tới DB; admin POST `/command` → backend `PublishCommandAsync` throw 503; FW không nhận downlink. Không có error message rõ ràng để debug — chỉ thấy "data không hiện trên dashboard".


## Verify

```bash
# Plain MQTT 1883 (dev) — bridge user
mosquitto_sub -h localhost -p 1883 -u backend-bridge -P "$MQTT_PASSWORD" \
  -t 'solar/#' -v &
mosquitto_pub -h localhost -p 1883 -u backend-bridge -P "$MQTT_PASSWORD" \
  -t solar/test-dev/heartbeat -m '{"ok":true}'

# TLS 8883 — cần CA cert
mosquitto_pub -h localhost -p 8883 \
  --cafile infra/mqtt/mosquitto/certs/ca.crt \
  -u backend-bridge -P "$MQTT_PASSWORD" \
  -t solar/test-dev/status -m online -r

# ACL test — device A KHÔNG được pub topic device B
mosquitto_pub -h localhost -p 1883 -u dev-a -P "..." \
  -t solar/dev-b/heartbeat -m oops   # → Connection Refused / Disconnect
```

## ESP32 firmware setup

```bash
# 1. Copy CA cert sang firmware data folder
cp infra/mqtt/mosquitto/certs/ca.crt firmware-esp32/data/ca_cert.pem

# 2. Đổi credential trong firmware-esp32/include/config.h:
#     MQTT_BROKER_HOST   "<host>"
#     MQTT_USERNAME      "<lower-case deviceCode>"
#     MQTT_PASSWORD      "<plaintext returned 1 time bởi POST /api/admin/iot-devices>"

# 3. Upload firmware + LittleFS (chứa ca_cert.pem)
cd firmware-esp32
pio run -t upload
pio run -t uploadfs
```

## Roadmap

| Sprint | Việc | Trạng thái |
|--------|------|-----------|
| S0     | EMQX dev anonymous 1883 → đổi Mosquitto | ✅ thay EMQX vì backend `Mqtt__Host=mosquitto` |
| S4     | TLS 8883 + ACL per-device + CA tự ký | ✅ S4-INF-01..04 |
| S4     | User `backend-bridge` cho bridge service | ✅ acl.conf line `user backend-bridge` |
| S4     | LWT mark device offline tức thì | ✅ S4-FW-02 + backend `LastWillHandler` |
| S4     | Downlink `solar/{dev}/cmd` + ack | ✅ S4-FW-05 + backend `PublishCommandAsync` |
| S7     | Cert rotation cron / Let's Encrypt | ⏳ |
| S7     | Grafana panel broker (online count, msg/s) | ⏳ |

## Troubleshooting

| Triệu chứng | Nguyên nhân thường gặp |
|-------------|------------------------|
| `Connection Refused: not authorized` | Sai password / ACL chặn topic / username KHÔNG match `solar/{username}/*` |
| ESP32 log `CA cert KHÔNG tồn tại tại /ca_cert.pem` | Chưa `pio run -t uploadfs` sau khi copy ca.crt vào data/ |
| Broker container restart loop | mosquitto.conf uncomment 8883 nhưng cert chưa có → xem `docker logs iot-mosquitto` |
| Bridge log `unknown device {DeviceCode}` | DeviceCode topic không match record trong DB (admin chưa tạo device) |
| ESP32 connect 8883 hang lâu | TLS handshake fail — verify CA cert match server cert + clock NTP đúng |
| FW connect MQTT OK nhưng không nhận `cmd` downlink | **Case mismatch**. Backend `IotApiKeyService` lowercase deviceCode thành `mqtt_username`, nhưng `AdminIotDevicesController.SendCommand` publish `solar/{device.DeviceCode}/cmd` dùng RAW case. ACL pattern `solar/%u/cmd` match lowercase username → topic mixed-case ≠ match → broker drop. **Workaround**: admin tạo deviceCode đã lowercase (vd `gw-esp32-001`). FW boot log `[mqtt] ⚠ MQTT_USERNAME=... KHÔNG phải lowercase(DEVICE_CODE)...` nếu phát hiện. Sprint 5+ backend fix bằng cách lowercase khi build topic. |
| FW MQTT publish OK nhưng telemetry không lên DB + admin POST `/command` → 503 | **Backend bridge disabled**. `Mqtt__Enabled` default false. Verify: `docker logs solar-batteryservice \| grep "MQTT bridge"` thấy "disabled (Mqtt:Enabled=false)" → thêm `Mqtt__Enabled=true` + `Mqtt__Password=<plaintext>` vào `backend/.env.Docker` → `docker compose up -d --force-recreate batteryservice`. Xem section "Enable backend MQTT bridge" ở trên. |
