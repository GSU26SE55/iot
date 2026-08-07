#pragma once
//
// GH-748 — đọc kết quả NHẬN MỘT PHẦN nằm trong response HTTP 2xx.
//
// Lỗi gốc: firmware coi mọi 2xx là "cả batch đã vào", không hề đọc thân response. Backend
// lại trả `{ totalReceived, inserted, skipped }` — nên khi backend chỉ nhận một phần, firmware
// vẫn báo thành công và xoá phần còn lại khỏi hàng đợi. Việc bỏ dữ liệu diễn ra trong im lặng.
//
// Vì sao KHÔNG thử gửi lại phần bị bỏ: `skipped` của backend gồm `mapping_invalid` (thiết bị
// gửi serial không được map cho nó) và `rejectedOutliers` (giá trị ngoài dải vật lý). Cả hai
// đều VĨNH VIỄN — gửi lại đúng dữ liệu đó chỉ ra đúng kết quả đó. Cái thực sự mất là **tín
// hiệu**: người vận hành không biết thiết bị đang bị bỏ số đo vì sai mapping.
//
// ⇒ Việc đúng là ĐỌC và LA LÊN, không phải retry.
//
// Muốn retry CHỌN LỌC thì backend phải trả kết quả theo TỪNG item (có định danh) — hợp đồng
// hiện tại chỉ có số đếm nên không thể biết item nào hỏng. Ghi nhận là việc riêng phía backend.
//

#include <cstddef>

namespace core {

struct IngestResult {
  /// Parse được thân response hay không. False ⇒ các số bên dưới vô nghĩa.
  bool parsed = false;
  int  totalReceived = 0;
  int  inserted = 0;
  int  skipped = 0;

  /// True khi backend nhận thiếu so với số gửi đi.
  bool isPartial() const { return parsed && totalReceived > 0 && inserted < totalReceived; }
};

/// <summary>
/// Đọc `data.{totalReceived,inserted,skipped}` từ thân `CommonResponse`.
/// </summary>
/// <remarks>
/// Chịu được thân bị CẮT NGẮN: firmware chỉ giữ một đoạn đầu response
/// (<c>responseSnippet</c>) nên JSON có thể không đóng ngoặc. Khi đó trả
/// <c>parsed = false</c> thay vì đoán bừa — báo nhầm "nhận thiếu" sẽ khiến người vận hành
/// đi tìm một sự cố không có thật.
/// </remarks>
IngestResult parseIngestResult(const char* json, size_t len);

}  // namespace core
