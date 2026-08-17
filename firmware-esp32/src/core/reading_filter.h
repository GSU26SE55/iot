#pragma once
//
// GH-740 — loại các reading ĐÃ publish qua MQTT ra khỏi payload fallback HTTPS.
//
// Lỗi gốc: `ingestViaMqtt()` publish theo TỪNG NHÓM serial. Nhóm đầu gửi xong, nhóm sau
// fail thì hàm `return false`, và caller rơi xuống fallback HTTPS gửi lại **TOÀN BỘ** batch
// — nên nhóm đã gửi qua MQTT bị ghi lần thứ hai. Khoá idempotency của đường HTTPS không cứu
// được: nó chỉ khử trùng giữa các lần gửi HTTPS với nhau, còn bản ghi kia đã vào backend
// bằng đường MQTT với hình dạng payload khác hẳn.
//
// Hàm THUẦN (không Arduino, không I/O) ⇒ test được ở env:native.
//

#include <cstddef>

#include "core/reading.h"

namespace core {

/// <summary>
/// Chép sang <paramref name="out"/> những reading có serial KHÔNG nằm trong
/// <paramref name="publishedSerials"/>.
/// </summary>
/// <returns>Số reading đã chép.</returns>
/// <remarks>
/// So sánh theo <c>serial</c> vì MQTT publish theo nhóm serial — cả nhóm cùng thành công
/// hoặc cùng thất bại, không có trạng thái nửa vời trong một nhóm.
/// Serial rỗng được coi là CHƯA gửi (giữ lại): thà gửi thừa một bản ghi không định danh
/// được còn hơn làm mất nó.
/// </remarks>
size_t filterOutPublished(const SensorReading* in, size_t inCount,
                          const char* const* publishedSerials, size_t publishedCount,
                          SensorReading* out, size_t outCap);

}  // namespace core
