# Solar BMS Setup (Android)

Ứng dụng hỗ trợ kỹ thuật viên ghép Solar Gateway với hệ thống production. Điện thoại chỉ dùng
trong bước thiết lập; sau đó ESP32 tự kết nối Wi‑Fi khách hàng, provision qua HTTPS và publish
MQTT trực tiếp.

## Luồng sử dụng

1. Mở app, chọn **Mở Wi‑Fi và kết nối**.
2. Kết nối mạng `SolarGW-xxxx` bằng mật khẩu `12345678`. Nếu Android cảnh báo mạng không có
   Internet, chọn giữ kết nối.
3. Quay lại app; portal `http://192.168.4.1:8080` được tải tự động.
4. Chọn Wi‑Fi 2.4 GHz của khách hàng và nhập mật khẩu.
5. Quét QR provisioning từ trang Admin. Camera chỉ nhận QR có `deviceCode` và `apiKey` hợp lệ.
6. ESP32 lưu cấu hình, khởi động lại và lấy MQTT credentials từ backend production.

App chỉ mở origin cục bộ của gateway, tự xác thực portal và không chụp hoặc lưu ảnh QR.

## Build

```powershell
$env:ANDROID_HOME='C:\Users\NEO\AppData\Local\Android\Sdk'
gradle :app:assembleDebug
```

APK debug: `app/build/outputs/apk/debug/app-debug.apk`.
