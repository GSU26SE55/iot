#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>

namespace {
constexpr int kCsPin = 14;
constexpr int kMosiPin = 5;
constexpr int kSckPin = 21;
constexpr int kMisoPin = 7;
constexpr char kProbePath[] = "sd_probe.txt";
constexpr char kProbeData[] = "solar-bms-sd-ok";

SPIClass s_spi(FSPI);
SdFs s_sd;

bool runAtMHz(uint8_t mhz) {
  s_sd.end();
  s_spi.end();
  delay(250);

  pinMode(kCsPin, OUTPUT);
  digitalWrite(kCsPin, HIGH);
  pinMode(kMisoPin, INPUT_PULLUP);
  s_spi.begin(kSckPin, kMisoPin, kMosiPin, kCsPin);

  Serial.printf("[sd-test] begin CS=%d MOSI=%d SCK=%d MISO=%d @%uMHz\n",
                kCsPin, kMosiPin, kSckPin, kMisoPin, mhz);
  SdSpiConfig config(kCsPin, DEDICATED_SPI, SD_SCK_MHZ(mhz), &s_spi);
  if (!s_sd.begin(config)) {
    Serial.printf("[sd-test] init FAILED code=0x%02X data=0x%02X\n",
                  s_sd.sdErrorCode(), s_sd.sdErrorData());
    return false;
  }

  FsFile out = s_sd.open(kProbePath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!out) {
    Serial.println("[sd-test] open for write FAILED");
    return false;
  }
  const size_t expected = sizeof(kProbeData) - 1;
  const size_t written = out.write(kProbeData, expected);
  out.sync();
  out.close();

  FsFile in = s_sd.open(kProbePath, O_RDONLY);
  if (!in) {
    Serial.println("[sd-test] reopen for read FAILED");
    return false;
  }
  char actual[sizeof(kProbeData)] = {};
  const int read = in.read(actual, expected);
  in.close();
  s_sd.remove(kProbePath);

  if (written != expected || read != static_cast<int>(expected) ||
      memcmp(actual, kProbeData, expected) != 0) {
    Serial.printf("[sd-test] verify FAILED write=%u read=%d\n",
                  static_cast<unsigned>(written), read);
    return false;
  }

  Serial.printf("[sd-test] PASS cardSize=%lluMB write/read/delete OK\n",
                s_sd.card()->sectorCount() / 2048ULL);
  return true;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[sd-test] isolated HS0724 diagnostic");
  if (runAtMHz(4) || runAtMHz(1)) return;
  Serial.println("[sd-test] ALL ATTEMPTS FAILED");
}

void loop() {
  delay(1000);
}
