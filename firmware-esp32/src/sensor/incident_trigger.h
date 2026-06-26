// ==================================================================
// Sprint 6 — S6-FW-01/02 (#63/#64): Incident trigger — edge + cooldown.
//
// Pure logic (header-only) — KHÔNG phụ thuộc Arduino. Dùng chung cho MQ-2
// (smoke) và water leak: phát hiện CHUYỂN CẠNH bình thường→bất thường (rising
// edge) và chỉ "fire" 1 lần mỗi cooldown để không spam backend khi điều kiện
// còn kéo dài (vd khói vẫn còn nhiều giây).
//
// Tách ra header pure (giống ui/led_palette.h, cmd/cmd_logic.h, queue/queue_index.h)
// để test native không cần ESP32 (test_incident_trigger).
//
// Ngữ nghĩa:
//   - update(active, nowMs) trả true ĐÚNG MỘT lần tại cạnh lên (inactive→active),
//     với điều kiện đã qua cooldown kể từ lần fire trước (hoặc chưa fire bao giờ).
//   - Khi active giữ HIGH liên tục: chỉ fire ở cạnh lên đầu tiên, không lặp.
//   - active xuống LOW rồi lên lại trong cooldown → suppress (tránh chattering).
//   - Lần đọc đầu đã active (vd nước có sẵn lúc boot) → coi là cạnh lên → fire.
// ==================================================================
#pragma once

#include <cstdint>

namespace sensor {

class IncidentTrigger {
 public:
  // cooldownMs: khoảng tối thiểu giữa 2 lần fire (suppress re-report).
  explicit IncidentTrigger(uint32_t cooldownMs) : cooldownMs_(cooldownMs) {}

  // Cập nhật trạng thái với 1 mẫu đo. Trả true nếu nên report incident NGAY.
  bool update(bool active, uint32_t nowMs) {
    bool fire = false;
    // Rising edge: mẫu trước inactive (hoặc lần đọc đầu), mẫu này active.
    if (active && !prevActive_) {
      if (!hasFired_ || (nowMs - lastFireMs_) >= cooldownMs_) {
        fire = true;
        hasFired_ = true;
        lastFireMs_ = nowMs;
      }
    }
    prevActive_ = active;
    return fire;
  }

  bool isActive()  const { return prevActive_; }
  bool hasFired()  const { return hasFired_; }
  uint32_t lastFireMs() const { return lastFireMs_; }

 private:
  uint32_t cooldownMs_;
  bool     prevActive_ = false;
  bool     hasFired_   = false;
  uint32_t lastFireMs_ = 0;
};

}  // namespace sensor
