# infra/mqtt — MQTT broker (EMQX)

Sprint 0 dựng broker **dev mode** (anonymous, port 1883) để các sprint sau có sẵn hạ tầng test. Cấu hình ACL + TLS + per-device credential sẽ làm ở **S4** (xem `tasksprint.md` S4-INF-01..04).

## Verify nhanh

```bash
# Dashboard
open http://localhost:18083    # login admin / public

# Pub/Sub thử (cần cài mosquitto-clients)
mosquitto_sub -h localhost -p 1883 -t 'test/hello' -v &
mosquitto_pub -h localhost -p 1883 -t 'test/hello' -m '{"msg":"sprint0"}'
```

## Roadmap

| Sprint | Việc | Folder |
|--------|------|--------|
| **S0** | Dùng EMQX default (anonymous, 1883) | — |
| **S4** | TLS 8883 + ACL per-device + CA tự ký | `emqx/etc/acl.conf`, `emqx/certs/` |
| **S4** | User `backend-bridge` cho bridge service | `emqx/etc/users.conf` |

## Fallback Mosquitto (nếu cần)

Folder `mosquitto/config/` để placeholder. Khi nào muốn so sánh EMQX vs Mosquitto:
- Mosquitto: nhẹ, ít RAM, không có dashboard.
- EMQX: dashboard sẵn, ACL/per-user dễ config qua UI, khuyên dùng cho capstone (NI §8.2).
