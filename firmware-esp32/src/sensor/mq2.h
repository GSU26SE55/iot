// ==================================================================
// Sprint 6 — S6-FW-01 (#63): MQ-2 smoke/gas sensor.
//
// MQ-2 đo nồng độ khói/gas, xuất analog (AO) tỉ lệ thuận nồng độ. ESP32 đọc qua
// ADC và công bố mức % cho `ambient_report`, nơi gửi lên
// `/api/ambient/readings/batch`.
//
// Thiết bị KHÔNG đánh giá ngưỡng. Ngưỡng cảnh báo nằm duy nhất ở
// `AmbientThresholdConfig.HighGasWarning/Critical` (DB, admin sửa trên dashboard).
// Xem đầu mq2.cpp cho lý do bỏ ngưỡng cục bộ.
//
// Hardware (WD §4.3):
//   MQ-2 AO → ESP32-S3 GPIO1 (ADC1_CH0). VCC = 5V, GND chung.
//   ⚠ MQ-2 chạy 5V → chân AO PHẢI qua bộ chia áp / level shifter về ≤ 3.3V
//     trước khi vào GPIO1, nếu không sẽ hỏng chân ADC ESP32 (WD §4.3 note).
//   ⚠ MQ-2 cần warm-up (~20-30s) sau cấp nguồn mới đọc tin cậy (MQ2_WARMUP_MS).
//
// Tham chiếu: tasksprint.md S6-FW-01, WD §4.3, OV §A6.
// ==================================================================
#pragma once

#include <cstdint>

namespace sensor {

// Init ADC pin. Bắt đầu đếm warm-up. Trả false nếu MQ2_ENABLED=0.
bool mq2Begin();

// Gọi mỗi loop tick. Tự throttle theo MQ2_POLL_INTERVAL_MS. Chỉ đọc ADC và cập
// nhật số đo mới nhất — không gửi gì, không quyết định gì.
void mq2Tick();

// % gas đã dùng được cho báo cáo ambient chưa: false khi chưa init hoặc còn warm-up
// (sợi đốt chưa ổn định, % lúc đó chỉ là nhiễu và sẽ làm sai ngưỡng cảnh báo).
bool mq2AmbientValueReady();

// Đọc raw ADC hiện tại (0-4095). Trả -1 nếu chưa init.
int mq2ReadRaw();

uint32_t mq2LastRaw();       // raw đọc lần gần nhất (debug)
int      mq2LastPercent();   // mức gas % hiện tại (0-100%)

}  // namespace sensor
