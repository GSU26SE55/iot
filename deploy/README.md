# IoT production deployment

Production sử dụng `Jenkinsfile` để CI firmware và `deploy/jenkins/production.Jenkinsfile.example` cho job ký/deploy trung tâm. Mosquitto chạy bằng `infra/docker-compose.prod.yml` trên cùng VPS với backend K3s, nhưng không tham gia network Docker/K3s của backend.

Thứ tự lần đầu:

1. Deploy backend K3s để cert-manager cấp certificate `mqtt.solaris.io.vn`.
2. Chạy systemd MQTT TLS sync từ repository backend.
3. Điền `/opt/solar-iot/config/host.env` và `/opt/solar-iot/secrets/runtime.env` từ các file `.example`; MQTT password phải giống `Mqtt__Password` của backend.
4. Chạy pipeline `main`; CI xanh sẽ gọi job trung tâm `solar-iot-production`.

Firmware production dùng `https://api.solaris.io.vn` làm HTTPS fallback và
`mqtt.solaris.io.vn:8883` làm MQTT TLS broker. Đây là các endpoint public không bí mật; pipeline
sẽ dừng nếu `config.example.h` trôi về domain placeholder. `deviceCode`, API key, MQTT password,
Wi-Fi và mật khẩu AP/portal vẫn là cấu hình riêng từng thiết bị, phải provision qua NVS trước khi
đưa thiết bị vào vận hành.

Runbook đầy đủ được duy trì tại root repository backend trong `PRODUCTION_DEPLOYMENT_BACKEND_IOT.md`.
