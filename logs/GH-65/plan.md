# Plan — GH-65: [S6-FW-03] Canonical sensorSourceCode cho cross-source pairing

## Metadata
- Status: PLANNING | Role: FW | Ngày: 2026-06-24
- Issue: #65 — https://github.com/GSU26SE55/iot/issues/65
- Sprint: Sprint 6
- Spec: OV §B2 (o), newiot §7.6, tasksprint S6-FW-03

## Mục tiêu
Đảm bảo INA226 + DS18B20 reading có `sourceType=IotGateway(2)` + `sensorSourceCode`
nhất quán để backend `CrossSourceValidationService` ghép cặp với BMS `Bms(1)` →
phát hiện `SensorMismatch(15)`. AC: DB query có cặp primary + redundant/external-temp
cùng battery + cùng phút.

## Phân tích hiện trạng
Behavior đã ĐÚNG nhưng mỗi file hard-code string literal rời rạc:
- `modbus_bms.cpp:169-170` → `Bms` + `"primary"`
- `ina226.cpp:125-126`     → `IotGateway` + `"redundant"`
- `ds18b20.cpp:115-116`    → `IotGateway` + `"external-temp"`
- `mock_bms.cpp:181/211/232`→ 3 literal tương tự
→ Rủi ro: 1 typo (vd "redundent") làm vỡ pairing mà không ai phát hiện. #65 = biến
việc đúng này thành **được đảm bảo** bằng single source of truth + test invariant.

## Files
| File | Action | Ghi chú |
|------|--------|---------|
| `src/core/source_tags.h` | (đã có) | canonical constants — SSOT |
| `src/bms/modbus_bms.cpp` | modify | dùng `core::kSourceType/CodePrimary` |
| `src/sensor/ina226.cpp` | modify | `kSourceType/CodeRedundant` |
| `src/sensor/ds18b20.cpp` | modify | `kSourceType/CodeExternalTemp` |
| `src/bms/mock_bms.cpp` | modify | 3 chỗ dùng constant |
| `test/test_source_tags/test_source_tags.cpp` | **create** | native test invariant |

## Surgical scope
Chỉ thay 2 dòng tag (sourceType + sensorSourceCode) mỗi chỗ + thêm 1 include
`core/source_tags.h`. KHÔNG đổi logic đọc sensor, decode, payload.

## Test invariant (test_source_tags)
- 3 code khác nhau đôi một, ≤ 20 chars (khớp `sensorSourceCode[24]`)
- primary → `SourceType::Bms`
- redundant + external-temp → `SourceType::IotGateway`
- BMS sourceType ≠ IotGateway sourceType (điều kiện ghép cặp cross-source)

## Steps
- [ ] Áp constant: modbus_bms, ina226, ds18b20, mock_bms (+include)
- [ ] test_source_tags.cpp
- [ ] `pio test -e native -f test_source_tags` PASS
- [ ] `pio test -e native` (full) PASS — không regress test cũ
- [ ] compile esp32 + esp32-s3-real PASS
