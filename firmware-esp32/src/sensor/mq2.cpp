// ==================================================================
// Sprint 6 — S6-FW-01 (#63): MQ-2 smoke/gas sensor implementation.
//
// Pipeline: warm-up → đọc ADC GPIO1 → công bố mức gas % cho `ambient_report`,
// nơi đẩy nó lên `/api/ambient/readings/batch`.
//
// Thiết bị KHÔNG còn tự quyết định có sự cố hay không. Trước đây nó so raw ADC với
// `MQ2_THRESHOLD_RAW` (2000 ≈ 48%) rồi tự POST EnvironmentalIncident(GasLeak), trong khi
// backend cũng chấm chính mức % đó bằng `AmbientThresholdConfig.HighGasWarning/Critical`.
// Kết quả: MỘT lần rò khí sinh HAI alert và HAI ticket, với HAI ngưỡng khác nhau — con số
// trong firmware không ai sửa được nếu không nạp lại, còn con số admin đặt trên dashboard
// thì không chi phối được thiết bị.
//
// Nay ngưỡng chỉ nằm một chỗ: `AmbientThresholdConfig` trong DB. Thiết bị chỉ đo và gửi số.
// Bỏ luôn ngưỡng cục bộ cũng không mất khả năng cảnh báo khi mất mạng — đường report cũ
// cũng cần HTTPS mới gửi được, nên offline thì cả hai đều im.
//
// Xem mq2.h cho hardware note.
// ==================================================================
#include "sensor/mq2.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#include <Arduino.h>

namespace sensor {

namespace {

bool     s_inited        = false;
uint32_t s_warmupStartMs = 0;
uint32_t s_lastPollMs    = 0;
uint32_t s_lastRaw       = 0;

bool inWarmup(uint32_t now) {
  return (now - s_warmupStartMs) < MQ2_WARMUP_MS;
}

}  // namespace

bool mq2Begin() {
#if !MQ2_ENABLED
  Serial.println("[mq2] disabled (MQ2_ENABLED=0) — skip");
  return false;
#else
  // ESP32 Arduino mặc định 12-bit ADC (0-4095). Set explicit để khớp thang % bên dưới.
  analogReadResolution(12);
  // Full-scale attenuation → đọc được dải ~0-3.3V trên chân ADC.
  analogSetPinAttenuation(MQ2_ADC_PIN, ADC_11db);

  s_warmupStartMs = millis();
  s_lastPollMs    = 0;
  s_inited        = true;
  Serial.printf("[mq2] init OK pin=GPIO%d warmup=%lums poll=%lums "
                "(ngưỡng cảnh báo do backend giữ — AmbientThresholdConfig)\n",
                static_cast<int>(MQ2_ADC_PIN),
                static_cast<unsigned long>(MQ2_WARMUP_MS),
                static_cast<unsigned long>(MQ2_POLL_INTERVAL_MS));
  return true;
#endif
}

int mq2ReadRaw() {
  if (!s_inited) return -1;
  return analogRead(MQ2_ADC_PIN);
}

int mq2LastPercent() {
  if (!s_inited) return 0;
  return static_cast<int>((s_lastRaw * 100) / 4095);
}

void mq2Tick() {
  if (!s_inited) return;

  const uint32_t now = millis();
  if (now - s_lastPollMs < MQ2_POLL_INTERVAL_MS) return;
  s_lastPollMs = now;

  const int raw = analogRead(MQ2_ADC_PIN);
  s_lastRaw = static_cast<uint32_t>(raw);

  // Warm-up: sợi đốt chưa ổn định → số đo lúc này là nhiễu, không được gửi lên.
  // `mq2AmbientValueReady()` trả false trong giai đoạn này nên ambient_report tự bỏ qua.
  if (inWarmup(now)) {
    Serial.printf("[mq2] warm-up... gas=%d%% (raw=%d) (%lus còn lại)\n",
                  (raw * 100) / 4095, raw,
                  static_cast<unsigned long>((MQ2_WARMUP_MS - (now - s_warmupStartMs)) / 1000));
  }
}

uint32_t mq2LastRaw() { return s_lastRaw; }

bool mq2AmbientValueReady() {
  return s_inited && !inWarmup(millis());
}

}  // namespace sensor
