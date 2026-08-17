# Vận hành & lắp đặt gateway IoT tại nhà khách

> Sprint IoT-3 · IOT3-94 → IOT3-99
> Áp dụng từ khi thiết bị chạy bằng **WiFi của khách hàng** (quyết định của sprint).
>
> Tài liệu kỹ thuật đi kèm: `backend/iot-co-che-hoat-dong.md` (cơ chế) ·
> `backend/docs/runbooks/11-mqtt-bridge-password.md` (mật khẩu cầu nối MQTT)

---

## Vì sao có tài liệu này

Trước IoT-3, mọi thông tin của thiết bị đều nhúng trong firmware: mỗi thiết bị một bản build, và
khách đổi mật khẩu WiFi là phải cầm cáp ra tận nơi nạp lại. Từ IoT-3, thiết bị nhận cấu hình lúc
chạy — nhưng chỉ khi **người lắp đặt và khách hàng biết phải làm gì**. Bốn tài liệu dưới đây là
phần đó.

---

## 1. Nhãn dán thiết bị (IOT3-94)

### Khổ và nội dung

**50 × 30 mm**, dán mặt trước thiết bị, ở chỗ **nhìn thấy được mà không phải tháo tủ**.

```
┌────────────────────────────────────────┐
│  ███████        GW-ESP32-001           │
│  ██ ▄▄ ██       Gateway Trạm A         │
│  ██ ██ ██                              │
│  ███████        Setup AP: SolarGW-4F2A │
│                 Mật khẩu: ••••••••     │
└────────────────────────────────────────┘
   QR 26mm         chữ 5,5–8 pt
```

| Trường | Lấy từ đâu | Vì sao có trên nhãn |
|---|---|---|
| **QR** `iot://provision?dc=…&key=…` | Trang admin → thiết bị → *Xem lại thông tin* → **In nhãn 50×30 mm** | Nạp lại danh tính thiết bị mà không phải gõ tay chuỗi 47 ký tự |
| **deviceCode** | như trên | Tra cứu trên app/web; đọc qua điện thoại cho tổng đài |
| **Tên AP setup** `SolarGW-XXXX` | 4 ký tự cuối MAC — thiết bị tự sinh | Nhiều thiết bị cùng bật thì phân biệt được cái nào |
| **Mật khẩu AP** | `SETUP_AP_PASSWORD` trong `include/config.h` | Không có thì khách không vào được trang cấu hình |

### In như thế nào

Trang admin → **IoT Devices** → chọn thiết bị → **Xem lại thông tin** → **In nhãn 50×30 mm**.
Nút đó mở cửa sổ in riêng đã đặt sẵn `@page { size: 50mm 30mm }` — máy in nhãn nhận đúng khổ,
không tự co về A4.

> ⚠️ **Vẫn phải có nhãn giấy dù đã có trang admin.** Lúc khách đổi mật khẩu WiFi, kỹ thuật viên
> đứng trước tủ pin **có thể không có mạng** để mở web admin — mà chính lúc đó mới cần QR nhất.
> Trang admin là để **in ra** nhãn, không phải để thay thế nhãn.

> ⚠️ QR chứa **API key**. Nhãn nằm bên trong tủ pin có khoá, không dán ra mặt ngoài tủ.

---

## 2. Hướng dẫn A4 dán trong nắp tủ (IOT3-95)

Đây là chỗ **tiết kiệm chi phí lớn nhất**: khách đổi mật khẩu WiFi là chuyện xảy ra thường xuyên,
và nếu mỗi lần đều phải cử người đi thì chi phí vận hành ăn hết lợi nhuận hợp đồng.

Nội dung dán trong nắp tủ, cỡ chữ ≥ 14 pt để đọc được trong tủ tối:

---

> ### THIẾT BỊ GIÁM SÁT PIN — HƯỚNG DẪN KẾT NỐI LẠI WIFI
>
> **Khi nào cần làm việc này?** Bạn vừa đổi mật khẩu WiFi, đổi router, hoặc đèn báo trên thiết bị
> **nháy tím** / **tím–cam xen kẽ**.
>
> **Xem đèn báo trước:**
>
> | Đèn | Nghĩa là | Bạn cần làm gì |
> |---|---|---|
> | 🟢 Xanh sáng đều | Bình thường | Không cần làm gì |
> | 🟢 Xanh nháy | Đang gửi bù dữ liệu | Chờ vài phút |
> | 🟠 Cam | Đang tìm WiFi | Chờ 5 phút |
> | 🟣 Tím nháy | **Chưa có WiFi** | Làm 4 bước dưới |
> | 🟣🟠 Tím–cam xen kẽ | **Mất mạng đã lâu** | Làm 4 bước dưới |
> | 🔴 Đỏ | Mất mạng | Kiểm tra router trước |
>
> **Bốn bước kết nối lại:**
>
> 1. Mở WiFi trên điện thoại, tìm mạng tên **`SolarGW-....`** (xem nhãn trên thiết bị).
> 2. Nối vào, nhập mật khẩu **ghi trên nhãn**.
> 3. Trang cấu hình tự mở. Nếu không, mở trình duyệt vào **`192.168.4.1`**.
> 4. Chọn tên WiFi nhà bạn → nhập mật khẩu mới → bấm **Save**.
>
> Đèn chuyển sang **xanh** trong khoảng 1 phút là xong.
>
> **Không thấy tên WiFi nhà mình trong danh sách?**
> Thiết bị chỉ bắt được băng **2,4 GHz**. Nhiều router phát cả 2,4 và 5 GHz; hãy kiểm tra băng
> 2,4 GHz có đang bật không. Nếu mạng của bạn dùng **tài khoản riêng cho từng người**
> (WPA2-Enterprise, thường là mạng công ty/trường học) thì thiết bị **không nối được** — hãy gọi
> hỗ trợ.
>
> **Hỗ trợ:** [số điện thoại] — nhớ đọc giúp **mã thiết bị** ghi trên nhãn.

---

## 3. Quy trình lắp đặt — 9 bước cho kỹ thuật viên (IOT3-96)

Làm **đúng thứ tự**. Ba bước đầu ở văn phòng, sáu bước sau tại hiện trường.

### Tại văn phòng

**1. Tạo thiết bị trên trang admin.**
`Admin → IoT Devices → Thêm`. Điền `deviceCode` (in hoa, ví dụ `GW-ESP32-001`), tên hiển thị,
chọn Site. Bấm lưu → dialog hiện API key + QR + thông tin MQTT.

**2. In nhãn.**
Ngay trong dialog đó → **In nhãn 50×30 mm** → dán lên thân thiết bị.
*Không đóng dialog trước khi in.* (Đóng rồi vẫn mở lại được qua **Xem lại thông tin** — nhưng
đừng tạo thói quen đó.)

**3. Nạp firmware + kiểm tra tại bàn.**
```
cd firmware-esp32 && pio run -e esp32-s3-real -t upload
```
Mở Serial, gõ `set devcode GW-ESP32-001` rồi `set apikey iotk_...` (hoặc quét QR ở bước 6).
Gõ `show` — phải thấy `wifi cfg=compile` và `mqtt cfg=compile`. Đó là trạng thái ĐÚNG của thiết
bị chưa provision.

### Tại nhà khách

**4. Lắp phần cứng.** Theo `hardware-assembly-guide.md` và `wiring-diagram.md`. Cấp nguồn cuối cùng.

**5. Xem đèn.** Sau ~10 giây phải là **tím nháy** (chưa có WiFi → đang mở trang cấu hình).
Nếu **đỏ** liên tục: thiết bị chết hoặc firmware chưa nạp — dừng lại, đừng đi tiếp.

**6. Nối WiFi của khách.**
Điện thoại → WiFi `SolarGW-XXXX` → mật khẩu trên nhãn → trang cấu hình tự mở.
Chọn mạng của khách, nhập mật khẩu; điền luôn `deviceCode` và `API key` nếu bước 3 chưa nạp.
Trước khi bấm Save, **xem bảng "Mạng thiết bị dò được"** trên trang đó:

- ⚠ *sóng yếu* → đổi chỗ đặt thiết bị hoặc thêm bộ mở rộng sóng. **Đừng bỏ qua** — sóng yếu
  không làm hỏng buổi lắp, nó làm hỏng ba tháng sau.
- ❌ *không nối được* (WPA2-Enterprise) → dừng, báo về để đổi phương án mạng.

**7. Chờ xanh.** Trong ~60 giây đèn phải chuyển: tím nháy → cam → **xanh**.
Cam quá 2 phút = sai mật khẩu WiFi. Quay lại bước 6.

**8. Kiểm tra trên hệ thống.**
Mở app (Staff → Công cụ → **Thiết bị IoT**) hoặc web (`/staff/iot-devices`), tìm thiết bị vừa lắp:
- Trạng thái **Hoạt động**
- **Thấy lần cuối** dưới 2 phút
- **Lệch đồng hồ** dưới ±10 giây

Ba dòng này đúng thì đường lên backend đã thông. Sai bất kỳ dòng nào → xem mục 4.

**9. Bàn giao.**
Dán tờ A4 (mục 2) vào mặt trong nắp tủ. Chỉ cho khách xem đèn báo và giải thích: *"đèn xanh là
bình thường; nháy tím hoặc tím–cam thì mở tờ hướng dẫn này ra làm theo, hoặc gọi cho chúng tôi."*

---

## 4. Kịch bản hỗ trợ khi có cảnh báo `DeviceOffline` (IOT3-97)

**Đây sẽ là loại ticket phổ biến nhất sau khi triển khai.** Nguyên nhân số một không phải hỏng
thiết bị — mà là **khách đổi WiFi**.

### Trước khi gọi khách — 30 giây tra cứu

Mở `/staff/iot-devices/{id}`, xem biểu đồ heartbeat:

| Dấu hiệu | Nghĩa | Xử lý |
|---|---|---|
| Ngừng hẳn, RSSI mẫu cuối **bình thường** | Mất mạng đột ngột | Nhiều khả năng đổi WiFi / router mất điện → gọi khách |
| RSSI **tụt dần** rồi ngừng | Sóng yếu dần | Cần đi hiện trường đổi vị trí, không phải việc của khách |
| **Hàng đợi dâng** rồi ngừng | Có WiFi nhưng không ra được backend | Kiểm tra backend/mạng khách chặn cổng — **đừng** gọi khách đổi WiFi |
| **Uptime tụt về 0** nhiều lần | Thiết bị boot-loop | Sự cố phần cứng/firmware → đi hiện trường |
| **Lệch đồng hồ** tăng dần rồi ngừng | Không ra được máy chủ NTP | Mạng khách chặn cổng 123 → cần đổi cấu hình mạng |

Phân biệt được bốn trường hợp này trước khi gọi là khác biệt giữa một cuộc gọi 2 phút và một
chuyến đi 2 giờ.

### Mẫu gọi khách (khi nghi đổi WiFi)

> "Chào anh/chị, em gọi từ [công ty]. Hệ thống báo thiết bị giám sát pin ở nhà mình mất kết nối
> từ [thời điểm]. Cho em hỏi khoảng thời gian đó nhà mình **có đổi mật khẩu WiFi, đổi router,
> hay mất điện** không ạ?"
>
> **Nếu CÓ** → hướng dẫn theo tờ A4 trong nắp tủ. Thường xong trong 5 phút, không cần cử người.
>
> **Nếu KHÔNG** → hỏi tiếp: *"Anh/chị mở nắp tủ giúp em, đèn nhỏ trên thiết bị đang màu gì ạ?"*
>
> | Khách trả lời | Kết luận |
> |---|---|
> | Tím nháy / tím–cam | Vẫn là chuyện WiFi → tờ A4 |
> | Cam | Sai mật khẩu WiFi → tờ A4, bước 6 |
> | Đỏ | Router đang tắt/hỏng → kiểm tra router trước |
> | **Không sáng gì** | Mất nguồn hoặc thiết bị hỏng → **cử người đi** |
> | Xanh | Thiết bị lên mạng rồi → sự cố ở phía backend, tạo ticket nội bộ |

### Ghi ticket

Ghi lại **màu đèn khách đọc được** vào ticket. Đó là dữ kiện chẩn đoán duy nhất lấy được từ xa,
và nó phân biệt được "hỏng thiết bị" với "hỏng mạng" — hai hướng xử lý hoàn toàn khác nhau.

---

## 5. Điều khoản hợp đồng về việc dùng mạng của khách (IOT3-98)

Đề xuất đưa vào phụ lục hợp đồng dịch vụ. **Cần luật sư/giảng viên hướng dẫn duyệt trước khi
dùng thật** — dưới đây là nội dung kỹ thuật, không phải văn bản pháp lý đã thẩm định.

> **Điều X. Sử dụng hạ tầng mạng của Khách hàng**
>
> **X.1.** Thiết bị giám sát do Bên A lắp đặt kết nối vào mạng WiFi do Khách hàng cung cấp để gửi
> số liệu vận hành pin về hệ thống của Bên A.
>
> **X.2. Phạm vi truy cập.** Thiết bị **chỉ** thực hiện kết nối ra Internet tới các máy chủ của
> Bên A và máy chủ đồng bộ thời gian (NTP). Thiết bị **không** quét mạng nội bộ, **không** kết nối
> tới bất kỳ thiết bị nào khác trong mạng của Khách hàng, và **không** mở cổng dịch vụ nào ra
> mạng nội bộ trong lúc vận hành bình thường.
>
> *Ngoại lệ duy nhất:* khi mất kết nối, thiết bị tự phát một điểm truy cập WiFi tạm (`SolarGW-XXXX`,
> có mật khẩu) để Khách hàng nhập lại thông tin mạng. Điểm truy cập này chỉ phục vụ trang cấu hình
> của chính thiết bị và tự tắt ngay khi kết nối được khôi phục.
>
> **X.3. Dữ liệu.** Dữ liệu truyền đi gồm: số đo cảm biến pin (điện áp, dòng điện, nhiệt độ, SOC,
> SOH), tình trạng hoạt động của thiết bị, và **tên mạng WiFi (SSID)** dùng cho chẩn đoán sự cố.
> **Không** thu thập lưu lượng mạng, thiết bị khác, hay bất kỳ dữ liệu cá nhân nào của Khách hàng.
>
> **X.4. Bảo mật đường truyền.** Toàn bộ dữ liệu được mã hoá (HTTPS/TLS và MQTT-over-TLS). Mỗi
> thiết bị có thông tin xác thực riêng; lộ thông tin của một thiết bị không ảnh hưởng thiết bị khác.
>
> **X.5. Trách nhiệm của Khách hàng.** Khách hàng duy trì kết nối Internet và **thông báo cho Bên A
> khi thay đổi mật khẩu WiFi hoặc thiết bị mạng**. Trong thời gian thiết bị mất kết nối, Bên A
> không chịu trách nhiệm về việc không phát hiện kịp thời sự cố pin.
>
> **X.6. Băng thông.** Thiết bị dùng dưới **50 MB/tháng** trong điều kiện vận hành bình thường
> (khoảng 1–2 KB mỗi chu kỳ đo, chu kỳ mặc định 5 giây, có nén và gộp lô).

> **Lưu ý cho nhóm:** X.5 là điều khoản quan trọng nhất về mặt vận hành. Không có nó thì mọi lần
> khách đổi WiFi mà không báo, trách nhiệm "vì sao không phát hiện sự cố" sẽ rơi về phía mình.

---

## 6. Nội dung đưa vào báo cáo KLTN (IOT3-99)

Hội đồng **chắc chắn hỏi**: *"lắp ở nhà khách thì mạng ở đâu ra?"* — đây là câu trả lời.

### 6.1 Quyết định

Thiết bị dùng **WiFi sẵn có của khách hàng**, không kèm router/SIM 4G riêng.

### 6.2 Bảng đánh đổi

| Phương án | Chi phí/thiết bị | Độ tin cậy | Phụ thuộc khách | Kết luận |
|---|---|---|---|---|
| **WiFi khách** (đã chọn) | 0 đ | Trung bình — phụ thuộc mạng khách | Cao | ✅ Chọn |
| Router 4G riêng | ~800k thiết bị + ~100k/tháng | Cao | Không | ❌ Chi phí nhân theo số điểm lắp |
| SIM NB-IoT | ~400k + phí data | Cao, băng thông thấp | Không | ❌ Không đủ băng thông cho 3 nguồn × chu kỳ 5 s |
| LoRaWAN + gateway | ~1,5tr gateway/trạm | Cao | Không | ❌ Chỉ đáng khi ≥ 10 thiết bị/trạm |

**Lý do chọn:** trong phạm vi khoá luận, thiết bị lắp ở hộ gia đình/doanh nghiệp nhỏ đã có sẵn
WiFi. Chi phí bằng 0 và không phát sinh phí hằng tháng là yếu tố quyết định. Đổi lại là **phụ
thuộc vào mạng của khách** — và đó chính là rủi ro mà thiết kế dưới đây xử lý.

### 6.3 Captive portal là cơ chế PHỤC HỒI, không phải cơ chế cấu hình ban đầu

Điểm này đáng nhấn mạnh trong báo cáo vì nó là chỗ thể hiện tư duy thiết kế:

Nếu chỉ coi trang cấu hình là "màn hình nhập WiFi lúc lắp đặt" thì gặp sự cố về sau vẫn phải cử
người đi. Ở đây nó được thiết kế thành **máy trạng thái ba chế độ**:

| Chế độ | Điều kiện | Hành vi |
|---|---|---|
| `Unconfigured` | Chưa có SSID | Mở trang cấu hình, **chờ vô hạn** |
| `Connecting` | Mất mạng **< 5 phút** | Chỉ thử lại mỗi 5 giây, **KHÔNG mở AP** |
| `Recovery` | Mất mạng **≥ 5 phút** | **Vừa** phát AP cấu hình **vừa** tiếp tục thử mạng cũ |

Hai chi tiết quan trọng:

1. **Không mở AP khi mất mạng ngắn.** Router khách khởi động lại là chuyện thường ngày; mở AP mỗi
   lần như vậy sẽ khiến điện thoại của khách nhảy vào AP của thiết bị và mất mạng nhà.
2. **Chế độ phục hồi VẪN giữ station** (`WIFI_AP_STA`). Nhờ đó router khách sống lại là thiết bị
   **tự lành**, không cần ai làm gì. Bỏ station đi thì mọi sự cố tự hết cũng thành một chuyến đi.

Luật quyết định này được tách thành hàm thuần `core::decideWifiPhase()` và **kiểm bằng 9 test tự
động** (`test_wifi_phase_policy`), trong đó có test chốt riêng cam kết "mất mạng dưới 5 phút thì
KHÔNG mở AP".

### 6.4 Số liệu dùng được cho báo cáo

- Chu kỳ đo mặc định: **5 giây** (cấu hình được 1–600 s từ trang admin)
- Chu kỳ quét phát hiện bất thường: **10 giây**
- Nén dữ liệu sau **7 ngày**, xoá sau **180 ngày** (TimescaleDB)
- Thiết bị tự xin lại thông tin xác thực sau **5 lần** bị broker từ chối liên tiếp, hạ nhiệt
  **15 phút**
- Firmware: RAM 24,3 % / Flash 18,1 % của ESP32-S3 N16R8
- Kiểm thử tự động: **259** test thuần chạy trên máy tính (không cần phần cứng)

---

## Việc chưa làm

| Mã | Việc | Vì sao chưa |
|---|---|---|
| IOT3-100 | Đo dung lượng thật của hàng đợi LittleFS khi offline dài ngày | Cần chạy thiết bị thật offline nhiều ngày |
| IOT3-101…105 | Tối ưu chu kỳ xuống dưới 1 giây | Đánh dấu **tuỳ chọn**; IOT3-104 phải bỏ SOH/chu kỳ/mã lỗi để đổi lấy tốc độ — không làm khi chưa có yêu cầu thật |
