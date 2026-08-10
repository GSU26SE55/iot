# Solar BMS Setup (Android)

Ứng dụng mở portal `http://192.168.4.1:8080`, tự xác thực bằng tài khoản setup
mặc định và chỉ cấp camera cho đúng origin của Solar Gateway. Camera được dùng bởi
`jsQR` ngay trong portal; app không chụp hoặc lưu ảnh.

## Flow

1. Mở app và cấp quyền Camera.
2. Bấm **Wi-Fi**, kết nối `SolarGW-xxxx` bằng mật khẩu `12345678`.
3. Quay lại app, bấm **Tải lại**.
4. Chọn Wi-Fi 2.4 GHz, nhập mật khẩu, rồi quét QR từ trang Admin ngay trong app.

## Build

```powershell
$env:ANDROID_HOME='C:\Users\NEO\AppData\Local\Android\Sdk'
gradle :app:assembleDebug
```

APK debug nằm tại `app/build/outputs/apk/debug/app-debug.apk`.
