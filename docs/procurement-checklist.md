# Procurement Checklist — Sprint 0

Sprint 0 đặt 3 đợt hàng. Track ở đây để leader theo dõi ETA. Cập nhật cột **Trạng thái** mỗi tuần.

| Đợt | ID task | Hàng | Cần cho | ETA mong đợi | Trạng thái | Ghi chú |
|-----|---------|------|---------|--------------|-----------|---------|
| **Cấp 0** | S0-HW-01 (#8) | 1× ESP32-S3 DevKitC-1 (N16R8) | S0–S4 | Cuối S0 (≤ 7 ngày) | ☐ Đặt ☐ Đã về | Mua thêm 1-2 con dự phòng nếu budget |
| **Cấp 0** | S0-HW-01 (#8) | 3× cáp USB-C data | S0+ | Cuối S0 | ☐ Đặt ☐ Đã về | Đảm bảo là cáp **data**, không phải chỉ sạc |
| **Cấp 0** | S0-HW-01 (#8) | 1× breadboard 830 points | S0+ | Cuối S0 | ☐ Đặt ☐ Đã về | + jumper Dupont M-M, M-F |
| **Cấp 1** | S0-HW-02 (#9) | 2× MAX485 (XY-017 auto-direction) | S5 | Trước S5 (≤ 4 tuần) | ☐ Đặt ☐ Đã về | Auto-direction để khỏi điều khiển DE/RE |
| **Cấp 1** | S0-HW-02 (#9) | Điện trở 120Ω × 4 | S5 | Trước S5 | ☐ Đặt ☐ Đã về | Terminator RS485 |
| **Cấp 1** | S0-HW-02 (#9) | Điện trở 4.7kΩ × 4 | S5 | Trước S5 | ☐ Đặt ☐ Đã về | Pull-up DS18B20 |
| **Cấp 1** | S0-HW-02 (#9) | 20m cáp đôi xoắn shielded (Belden 9841 hoặc tương đương) | S5 | Trước S5 | ☐ Đặt ☐ Đã về | RS485 cần shielded để chống nhiễu |
| **Cấp 1** | S0-HW-02 (#9) | Domino + nẹp dây | S5 | Trước S5 | ☐ Đặt ☐ Đã về | Đấu nối RS485 sạch sẽ |
| **BMS-RS485** | S0-HW-03 (#10) | 3–4 pin LiFePO4 12V có BMS Daly/JBD/JK-BMS | S5 | Trước S5 (≤ 4 tuần) | ☐ Đặt ☐ Đã về | **Critical path — đặt sớm nhất** |

---

## ⚠️ Checklist khi đặt BMS (S0-HW-03)

Trước khi chuyển tiền, **bắt buộc** xác nhận với seller qua tin nhắn/email — lưu screenshot:

- [ ] BMS có cổng RS485 (không phải chỉ Bluetooth, không phải UART 3.3V)
- [ ] Seller gửi **register map** (file PDF/Excel) chỉ rõ:
  - Register voltage (16-bit/32-bit, scale, offset)
  - Register current (signed/unsigned, scale)
  - Register temperature (số sensor, scale)
  - Register SOC, SOH, cycle count
  - Register error code (≤ 64 chars khi convert sang string)
- [ ] BMS cho phép **đổi unitId** (Modbus slave ID) bằng phần mềm hãng — không phải hard-coded
- [ ] Baudrate hỗ trợ: 9600 hoặc 19200 (thường dùng 9600)
- [ ] Có hướng dẫn (manual EN/VN) tải về được

> Nếu seller không trả lời rõ → **không mua**, tìm seller khác. Rủi ro lớn nhất S5 là BMS không có register map → block toàn bộ Modbus integration.

---

## Tracking link đặt hàng

| Đợt | Shop | Link | Ngày đặt | Ngày nhận | Tổng tiền |
|-----|------|------|----------|-----------|-----------|
| Cấp 0 | _Điền_ | _Điền_ | _Điền_ | _Điền_ | _Điền_ |
| Cấp 1 | _Điền_ | _Điền_ | _Điền_ | _Điền_ | _Điền_ |
| BMS | _Điền_ | _Điền_ | _Điền_ | _Điền_ | _Điền_ |

---

## Reference

- BOM chi tiết + giá: `hardware-bom-budget.csv` (root)
- BOM danh sách: `hardware-bom.csv` (root)
- Risk procurement: `tasksprint.md §7` — rủi ro BMS không có register map (mức **Cao**)
