# BMS Modbus Register Map — Sprint IoT-1 (#251)

Mapping địa chỉ Holding Register Modbus của 2 BMS phổ biến → payload `SensorReading` của backend.

> **Lưu ý:** Mỗi BMS có map khác nhau. Khi pilot với BMS mới, sao chép template dưới và verify bằng Modbus scanner trước khi flash firmware production.

## 1. JBD JBD-SP04S100A (Sprint pilot)

| Register | Địa chỉ | Loại | Scale | Đơn vị engineering | Field SensorReading |
|----------|---------|------|-------|--------------------|---------------------|
| Pack voltage | `0x0000` | uint16 | x 0.01 | V | `Voltage` |
| Pack current | `0x0001` | int16 | x 0.01 (± sạc/xả) | A | `Current` |
| SOC | `0x0002` | uint16 | x 0.1 | % | `SocPercent` |
| Cell #1..#4 voltage | `0x0003 ..0x0006` | uint16 | x 0.001 | V | dùng để tính `CellVoltageDeltaMv` |
| Temperature sensor 1 | `0x0007` | int16 (offset 2731 = 0°C) | / 10 | °C | `Temperature` |
| Cycle count | `0x0009` | uint16 | — | — | `CycleCount` |

Polling sequence (1 lần đọc 10 register liên tục bằng `readHoldingRegisters(0x0000, 10)`):

```cpp
voltage    = buf[0] * 0.01f;
current    = (int16_t)buf[1] * 0.01f;
soc        = buf[2] * 0.1f;
cellMin    = min(buf[3], buf[4], buf[5], buf[6]) * 0.001f;
cellMax    = max(buf[3], buf[4], buf[5], buf[6]) * 0.001f;
cellDelta  = (cellMax - cellMin) * 1000.0f;   // mV
tempC      = ((int16_t)buf[7] - 2731) / 10.0f;
cycleCnt   = buf[9];
```

## 2. Daly Smart BMS R32S (alternate hardware)

| Register | Địa chỉ | Loại | Scale | Đơn vị | Field |
|----------|---------|------|-------|--------|-------|
| Pack voltage | `0x9000` | uint32 (2 reg) | x 0.001 | V | `Voltage` |
| Pack current | `0x9002` | int32 (2 reg) | x 0.001 | A | `Current` |
| SOC | `0x9004` | uint16 | x 0.1 | % | `SocPercent` |
| Temperature | `0x9020` | int16 (offset 40) | x 1 | °C | `Temperature` |

> Tham khảo `iot/hardware/datasheets/daly-r32s-modbus.pdf` (vendor docs, không commit vào repo).

## 3. Mock BMS (simulator only)

Dùng cho dev khi chưa có hardware (xem `iot/simulator/esp32_simulator.py`):

```python
voltage = 3.7 + 0.05 * sin(t/30) + noise(±0.01)
current = uniform(-2.0, 2.0)
temperature = 25 + 2*sin(t/120) + noise(±0.5)
soc = 70 + 10*sin(t/600)
```

## Quy trình thêm BMS model mới

1. Tải datasheet vendor → tìm "Modbus register map".
2. Verify bằng Modbus scanner tool (`mbpoll`, `QModMaster`) trên PC.
3. Tạo bảng register map giống mẫu trên → commit vào repo.
4. Cập nhật `main.ino` (function `readBms()`) để parse đúng layout.
5. Cập nhật `IotDeviceCalibration` ở backend nếu BMS có offset khác chuẩn.
