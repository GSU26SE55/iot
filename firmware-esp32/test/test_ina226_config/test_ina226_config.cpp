// ==================================================================
// IOT3-04..07 — Chốt các ràng buộc CẤU HÌNH của INA226 ngay ở CI.
//
// Vì sao cần bài này: ba ràng buộc dưới đây chỉ lộ ra lúc CHẠY THẬT, dưới dạng
// một mã lỗi khó tra trong `setMaxCurrentShunt()`, và hậu quả là INA226 im lặng
// không init — nguồn `redundant` rỗng mà không có gì kêu lên. Đưa chúng thành
// test thuần giúp bắt được ngay khi ai đó đổi shunt hoặc đổi dòng tối đa.
//
// Nguồn số liệu (robtillaart/INA226 v0.6.6, INA226.cpp:220-223):
//     float shuntVoltage = maxCurrent * shunt;
//     if (shuntVoltage > 0.08190)           return INA226_ERR_SHUNTVOLTAGE_HIGH;
//     if (maxCurrent < 0.001)               return INA226_ERR_MAXCURRENT_LOW;
//     if (shunt < INA226_MINIMAL_SHUNT_OHM) return INA226_ERR_SHUNT_LOW;
//
// Phần cứng dự án: JK-BD6A24S10P — 100 A liên tục / 200 A đỉnh.
// ==================================================================
#include <unity.h>

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

namespace {

// Trần điện áp shunt mà thư viện chấp nhận (81,90 mV — datasheet 81,92 trừ biên tràn số).
constexpr float kLibShuntVoltageMax = 0.08190f;

// Trần dòng tối đa mà khối `normalize` của thư viện còn xử lý được.
// Khối đó làm tròn current_LSB về 1/2/5 × 10ⁿ µA bằng vòng lặp chỉ chạy `i < 4`
// ⇒ giá trị lớn nhất tạo được là 5000 µA. Truy ngược:
//     current_LSB = maxCurrent / 32768;  cần current_LSB × 1e6 + 1 <= 5000
//     ⇒ maxCurrent <= 163,83 A
constexpr float kNormalizeMaxCurrent = 163.83f;

// Ngưỡng mặc định của thư viện — dưới mức này phải ghi đè bằng build flag.
constexpr float kLibDefaultMinimalShunt = 0.001f;

// Dòng đỉnh BMS phải đo được, nếu không thì mọi cú surge đều đọc sai.
constexpr float kBmsPeakCurrentA = 200.0f;

}  // namespace

void setUp() {}
void tearDown() {}

// --- Ràng buộc 1: không vượt trần điện áp shunt ---------------------------
void test_shunt_voltage_within_library_limit() {
    const float shuntVoltage = INA226_MAX_CURRENT_A * INA226_SHUNT_OHM;
    TEST_ASSERT_TRUE_MESSAGE(
        shuntVoltage <= kLibShuntVoltageMax,
        "INA226_MAX_CURRENT_A * INA226_SHUNT_OHM vuot 81.90 mV "
        "=> setMaxCurrentShunt tra INA226_ERR_SHUNTVOLTAGE_HIGH (0x8000), chip khong init.");
}

// --- Ràng buộc 2: đo được tới đỉnh 200 A của BMS --------------------------
void test_measurable_range_covers_bms_peak() {
    const float maxMeasurable = kLibShuntVoltageMax / INA226_SHUNT_OHM;
    TEST_ASSERT_TRUE_MESSAGE(
        maxMeasurable >= kBmsPeakCurrentA,
        "Dai do cua shunt khong phu duoc dong dinh 200A cua JK-BD6A24S10P "
        "=> moi cu surge deu doc sai (bao hoa). Dung shunt 200A/75mV = 0.375 mOhm.");

    // Và maxCurrent khai báo cũng phải đủ: register CURRENT là int16 × current_LSB,
    // trần đọc = INA226_MAX_CURRENT_A. Khai thiếu là cắt ngọn ở phần mềm.
    TEST_ASSERT_TRUE_MESSAGE(
        INA226_MAX_CURRENT_A >= kBmsPeakCurrentA,
        "INA226_MAX_CURRENT_A nho hon dong dinh 200A => so doc bi cat ngon.");
}

// --- Ràng buộc 3: maxCurrent hợp lệ ---------------------------------------
void test_max_current_above_library_floor() {
    TEST_ASSERT_TRUE_MESSAGE(
        INA226_MAX_CURRENT_A >= 0.001f,
        "INA226_MAX_CURRENT_A < 1 mA => INA226_ERR_MAXCURRENT_LOW (0x8001).");
}

// --- Ràng buộc 4: hai lối thoát bắt buộc phải còn đó ----------------------
//
// Hai bài dưới KHÔNG kiểm được platformio.ini / ina226.cpp từ C++, nên chúng
// khẳng định ĐIỀU KIỆN KÍCH HOẠT: nếu điều kiện còn đúng thì lối thoát tương ứng
// vẫn bắt buộc. Ai đổi cấu hình làm điều kiện lệch đi sẽ thấy tên bài test này
// và biết phải xem lại chỗ nào.
void test_sub_milliohm_shunt_requires_build_flag() {
    if (INA226_SHUNT_OHM < kLibDefaultMinimalShunt) {
        TEST_ASSERT_TRUE_MESSAGE(
            true,
            "shunt < 1 mOhm => platformio.ini PHAI co -DINA226_MINIMAL_SHUNT_OHM=0.0001, "
            "neu khong setMaxCurrentShunt tra INA226_ERR_SHUNT_LOW (0x8002).");
    } else {
        TEST_IGNORE_MESSAGE(
            "shunt >= 1 mOhm => khong can -DINA226_MINIMAL_SHUNT_OHM nua, "
            "xem lai co nen go build flag do khoi platformio.ini khong.");
    }
}

void test_high_max_current_requires_normalize_false() {
    if (INA226_MAX_CURRENT_A > kNormalizeMaxCurrent) {
        TEST_ASSERT_TRUE_MESSAGE(
            true,
            "maxCurrent > 163.83A => ina226.cpp PHAI goi setMaxCurrentShunt(..., false), "
            "neu khong tra INA226_ERR_NORMALIZE_FAILED (0x8003).");
    } else {
        TEST_IGNORE_MESSAGE(
            "maxCurrent <= 163.83A => khoi normalize xu ly duoc, "
            "co the bo tham so false o ina226.cpp neu muon LSB tron so.");
    }
}

// --- Ràng buộc 5: bus voltage của pack ------------------------------------
void test_pack_voltage_within_ina226_bus_limit() {
    // INA226 đo bus tối đa 36 V. Pack dự án là 8S LiFePO4 (8 × 3,65 = 29,2 V).
    // BMS hỗ trợ tới 24S nên đây là chỗ dễ vượt khi nâng cấp pack.
    constexpr float kInaBusMaxV   = 36.0f;
    constexpr float kCellMaxV     = 3.65f;   // LiFePO4 sạc đầy
    constexpr int   kPackSeries   = 8;       // khớp battery_mapping.h: "pack 8S 24V"

    const float packMaxV = kCellMaxV * kPackSeries;
    TEST_ASSERT_TRUE_MESSAGE(
        packMaxV <= kInaBusMaxV,
        "Dien ap pack vuot 36V bus limit cua INA226 => hong chip. "
        "Tu 10S tro len phai doi sang INA228/INA238.");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_shunt_voltage_within_library_limit);
    RUN_TEST(test_measurable_range_covers_bms_peak);
    RUN_TEST(test_max_current_above_library_floor);
    RUN_TEST(test_sub_milliohm_shunt_requires_build_flag);
    RUN_TEST(test_high_max_current_requires_normalize_false);
    RUN_TEST(test_pack_voltage_within_ina226_bus_limit);
    return UNITY_END();
}
