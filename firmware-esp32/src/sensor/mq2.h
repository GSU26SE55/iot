// ==================================================================
// Sprint 6 — S6-FW-01 (#63): MQ-2 smoke/gas sensor.
//
// MQ-2 đo nồng độ khói/gas, xuất analog (AO) tỉ lệ thuận nồng độ. ESP32 đọc
// qua ADC, so với threshold (config). Khi vượt → report EnvironmentalIncident
// (Smoke) lên backend qua environmental_incident reporter (HTTPS).
//
// Hardware (WD §4.3):
//   MQ-2 AO → ESP32-S3 GPIO1 (ADC1_CH0). VCC = 5V, GND chung.
//   ⚠ MQ-2 chạy 5V → chân AO PHẢI qua bộ chia áp / level shifter về ≤ 3.3V
//     trước khi vào GPIO1, nếu không sẽ hỏng chân ADC ESP32 (WD §4.3 note).
//   ⚠ MQ-2 cần warm-up (~20-30s) sau cấp nguồn mới đọc tin cậy (MQ2_WARMUP_MS).
//
// Chống spam: IncidentTrigger fire 1 lần ở cạnh lên + cooldown (MQ2_REARM_COOLDOWN_MS);
// backend cũng dedup theo (site, incidentType) cho incident còn Open/Acknowledged.
//
// Tham chiếu: tasksprint.md S6-FW-01, WD §4.3, OV §A6.
// ==================================================================
#pragma once

#include <cstdint>

namespace sensor {

// Init ADC pin. Bắt đầu đếm warm-up. Trả false nếu MQ2_ENABLED=0.
bool mq2Begin();

// Gọi mỗi loop tick. Tự throttle theo MQ2_POLL_INTERVAL_MS. Trong warm-up thì
// chỉ đọc/log, không report. Khi ADC > threshold (cạnh lên) → report Smoke.
// Caller PHẢI đảm bảo WiFi + NTP synced trước khi report (no-op nếu chưa).
void mq2Tick();

// Đọc raw ADC hiện tại (0-4095). Trả -1 nếu chưa init.
int mq2ReadRaw();

uint32_t mq2ReportCount();   // số lần đã report incident
uint32_t mq2LastRaw();       // raw đọc lần gần nhất (debug)

}  // namespace sensor
