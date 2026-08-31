// ==================================================================
// Local queue implementation — SD card primary, LittleFS fallback
//
// Both SD and LittleFS implement fs::FS. We store a pointer to
// whichever mounted successfully and use it uniformly.
// ==================================================================
#include "queue/local_queue.h"
#include "queue/queue_index.h"
#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif

#include <Arduino.h>
#include <LittleFS.h>
#include <stdio.h>
#include <string.h>

#if SD_CARD_ENABLED
#include <SD.h>
#include <SPI.h>
#endif

namespace queue {

namespace {
constexpr const char* kDir         = "/queue";
constexpr const char* kBodyExt     = ".json";
constexpr const char* kIdemExt     = ".idem";
constexpr size_t      kFilenameLen = 32;     // /queue/<10 digit>.json + null

bool s_mounted = false;
fs::FS* s_fs   = nullptr;           // points to SD or LittleFS after mount
size_t s_depth = 0;
uint32_t s_oldest = 0;
uint32_t s_newest = 0;
uint32_t s_epochs[kMaxQueuedBatches] = {};

enum class StorageKind : uint8_t { None, Sd, LittleFs };
StorageKind s_storageKind = StorageKind::None;

#if SD_CARD_ENABLED
constexpr const char* kSdProbePath = "/.sd_probe";
constexpr char kSdProbeData[] = "solar-bms-sd-ok";
SPIClass s_sdSpi(FSPI);

const char* sdCardTypeName(uint8_t type) {
  switch (type) {
    case CARD_MMC:  return "MMC";
    case CARD_SD:   return "SDSC";
    case CARD_SDHC: return "SDHC/SDXC";
    default:        return "UNKNOWN";
  }
}

bool verifySdReadWrite() {
  SD.remove(kSdProbePath);

  File out = SD.open(kSdProbePath, FILE_WRITE);
  if (!out) {
    Serial.println("[queue] SD probe: cannot create test file");
    return false;
  }
  const size_t expected = strlen(kSdProbeData);
  const size_t written = out.write(
      reinterpret_cast<const uint8_t*>(kSdProbeData), expected);
  out.flush();
  out.close();
  if (written != expected) {
    Serial.printf("[queue] SD probe: short write %u/%u bytes\n",
                  static_cast<unsigned>(written),
                  static_cast<unsigned>(expected));
    SD.remove(kSdProbePath);
    return false;
  }

  File in = SD.open(kSdProbePath, FILE_READ);
  if (!in) {
    Serial.println("[queue] SD probe: cannot reopen test file");
    SD.remove(kSdProbePath);
    return false;
  }
  char actual[sizeof(kSdProbeData)] = {};
  const size_t read = in.readBytes(actual, expected);
  in.close();
  SD.remove(kSdProbePath);

  if (read != expected || memcmp(actual, kSdProbeData, expected) != 0) {
    Serial.printf("[queue] SD probe: read-back mismatch (%u/%u bytes)\n",
                  static_cast<unsigned>(read),
                  static_cast<unsigned>(expected));
    return false;
  }
  return true;
}

bool mountSdCard(uint32_t frequencyHz) {
  // SD.begin() calls SPI.begin() internally, but SPIClass keeps the custom pins
  // only when the bus is already started. Start it explicitly before every try.
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  // A floating MISO line commonly produces sdSelectCard()/CMD0 failures on
  // inexpensive HS0724 modules. Keep it high while the card is deselected.
  pinMode(SD_MISO_PIN, INPUT_PULLUP);
  s_sdSpi.end();
  delay(100);
  s_sdSpi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  delay(100);

  Serial.printf("[queue] SD init CS=%d MOSI=%d SCK=%d MISO=%d @%lukHz\n",
                SD_CS_PIN, SD_MOSI_PIN, SD_SCK_PIN, SD_MISO_PIN,
                static_cast<unsigned long>(frequencyHz / 1000UL));

  // Never format automatically: a missing/unsupported filesystem must not erase
  // a user's card. Format it as FAT32 on a computer instead.
  if (!SD.begin(SD_CS_PIN, s_sdSpi, frequencyHz, "/sd", 5, false)) {
    Serial.println("[queue] SD mount failed");
    return false;
  }

  const uint8_t type = SD.cardType();
  if (type == CARD_NONE || SD.cardSize() == 0) {
    Serial.println("[queue] SD mounted but no usable card was detected");
    SD.end();
    return false;
  }

  if (!verifySdReadWrite()) {
    Serial.println("[queue] SD read/write verification failed");
    SD.end();
    return false;
  }

  Serial.printf("[queue] SD ready type=%s size=%lluMB used=%lluMB (write/read OK)\n",
                sdCardTypeName(type),
                SD.cardSize() / (1024ULL * 1024ULL),
                SD.usedBytes() / (1024ULL * 1024ULL));
  return true;
}
#endif

void formatFilename(char* buf, size_t bufLen, uint32_t epochSec, const char* ext) {
  snprintf(buf, bufLen, "%s/%010lu%s", kDir, static_cast<unsigned long>(epochSec), ext);
}

bool parseEpochFromName(const char* name, uint32_t* outEpoch) {
  if (!name || !outEpoch) return false;
  uint32_t epoch = 0;
  int parsedDigits = 0;
  for (const char* p = name; *p && *p >= '0' && *p <= '9' && parsedDigits < 10; ++p) {
    epoch = epoch * 10 + static_cast<uint32_t>(*p - '0');
    parsedDigits++;
  }
  if (parsedDigits != 10) return false;
  *outEpoch = epoch;
  return true;
}

bool scanQueue(uint32_t* outOldest, uint32_t* outNewest, size_t* outCount) {
  if (outOldest) *outOldest = 0;
  if (outNewest) *outNewest = 0;
  if (outCount)  *outCount = 0;

  File dir = s_fs->open(kDir);
  if (!dir || !dir.isDirectory()) return false;

  uint32_t oldest = UINT32_MAX;
  uint32_t newest = 0;
  size_t   count  = 0;
  bool     overflow = false;

  File f = dir.openNextFile();
  while (f) {
    const char* name = f.name();
    const char* base = strrchr(name, '/');
    if (base) base++; else base = name;

    size_t nameLen = strlen(base);
    if (nameLen > 5 && strcmp(base + nameLen - 5, kBodyExt) == 0) {
      uint32_t epoch = 0;
      if (parseEpochFromName(base, &epoch)) {
        if (count < kMaxQueuedBatches) {
          s_epochs[count++] = epoch;
        } else {
          overflow = true;
        }
        if (epoch < oldest) oldest = epoch;
        if (epoch > newest) newest = epoch;
      }
    }
    f.close();
    f = dir.openNextFile();
  }
  dir.close();

  if (overflow) {
    Serial.printf("[queue] index overflow: more than %u batches on storage\n",
                  static_cast<unsigned>(kMaxQueuedBatches));
    return false;
  }

  if (count == 0) {
    if (outOldest) *outOldest = 0;
    if (outNewest) *outNewest = 0;
  } else {
    if (outOldest) *outOldest = oldest;
    if (outNewest) *outNewest = newest;
  }
  if (outCount) *outCount = count;
  return true;
}

bool refreshQueueIndex() {
  return scanQueue(&s_oldest, &s_newest, &s_depth);
}

void refreshQueueBoundsFromRam() {
  s_oldest = findOldestEpoch(s_epochs, s_depth);
  s_newest = findNewestEpoch(s_epochs, s_depth);
}

bool addEpochToIndex(uint32_t epochSec) {
  if (s_depth >= kMaxQueuedBatches) return false;
  s_epochs[s_depth++] = epochSec;
  if (s_oldest == 0 || epochSec < s_oldest) s_oldest = epochSec;
  if (epochSec > s_newest) s_newest = epochSec;
  return true;
}

bool removeEpochFromIndex(uint32_t epochSec) {
  if (!removeEpoch(s_epochs, &s_depth, epochSec)) return false;
  refreshQueueBoundsFromRam();
  return true;
}

bool deleteBatchFiles(uint32_t epochSec) {
  char body[kFilenameLen], idem[kFilenameLen];
  formatFilename(body, sizeof(body), epochSec, kBodyExt);
  formatFilename(idem, sizeof(idem), epochSec, kIdemExt);
  bool ok = s_fs->remove(body);
  s_fs->remove(idem);
  return ok;
}
}  // namespace

bool queueBegin() {
  if (s_mounted) return true;

#if SD_CARD_ENABLED
  const uint32_t frequencies[] = {
      SD_SPI_FREQUENCY_HZ,
      1000000UL,
      400000UL,
  };
  for (size_t i = 0; i < sizeof(frequencies) / sizeof(frequencies[0]); ++i) {
    if (i > 0 && frequencies[i] == frequencies[i - 1]) continue;
    if (mountSdCard(frequencies[i])) {
      s_fs = &SD;
      s_mounted = true;
      s_storageKind = StorageKind::Sd;
      break;
    }
    SD.end();
    s_sdSpi.end();
    delay(250);
  }

  if (!s_mounted) {
    Serial.println("[queue] SD unavailable; falling back to LittleFS");
    Serial.println("[queue]   SPI card did not answer at 4MHz/1MHz/400kHz; check 3V3, common GND,");
    Serial.println("[queue]   CS=14 MOSI=5 SCK=21 MISO=7, card insertion, and FAT32 formatting.");
    SD.end();
    s_sdSpi.end();
  }
#endif

  if (!s_mounted) {
    if (!LittleFS.begin(true /* formatOnFail */)) {
      Serial.println("[queue] LittleFS mount/format FAILED");
      Serial.println("[queue] refusing to auto-format: unsent telemetry may still be present");
      return false;
    }
    s_fs = &LittleFS;
    s_mounted = true;
    s_storageKind = StorageKind::LittleFs;
    Serial.println("[queue] LittleFS mount OK (fallback)");
  }

  if (!s_fs->exists(kDir)) {
    if (!s_fs->mkdir(kDir)) {
      Serial.println("[queue] mkdir /queue FAILED");
      return false;
    }
  }

  if (!refreshQueueIndex()) {
    Serial.println("[queue] initial index scan FAILED");
    return false;
  }
  Serial.printf("[queue] backend=%s depth=%u/%u used=%lluKB total=%lluKB\n",
                queueStorageName(), static_cast<unsigned>(s_depth),
                static_cast<unsigned>(kMaxQueuedBatches),
                queueStorageUsedBytes() / 1024ULL,
                queueStorageTotalBytes() / 1024ULL);
  return true;
}

bool queueEnqueue(uint32_t epochSec,
                  const char* body, size_t bodyLen,
                  const char* idempotencyKey) {
  if (!s_mounted) {
    if (!queueBegin()) return false;
  }
  if (!body || bodyLen == 0) return false;

  const bool atCapacity = s_depth >= kMaxQueuedBatches && s_oldest != 0;
  const uint32_t oldestToDrop = s_oldest;

  char nameBody[kFilenameLen];
  formatFilename(nameBody, sizeof(nameBody), epochSec, kBodyExt);
  while (s_fs->exists(nameBody)) {
    epochSec++;
    formatFilename(nameBody, sizeof(nameBody), epochSec, kBodyExt);
  }

  File f = s_fs->open(nameBody, FILE_WRITE);
  if (!f) {
    Serial.printf("[queue] open(%s) FAILED\n", nameBody);
    return false;
  }
  size_t wrote = f.write(reinterpret_cast<const uint8_t*>(body), bodyLen);
  f.close();
  if (wrote != bodyLen) {
    Serial.printf("[queue] write %u/%u bytes — partial — delete\n",
                  static_cast<unsigned>(wrote), static_cast<unsigned>(bodyLen));
    s_fs->remove(nameBody);
    return false;
  }

  if (idempotencyKey && idempotencyKey[0]) {
    char nameIdem[kFilenameLen];
    formatFilename(nameIdem, sizeof(nameIdem), epochSec, kIdemExt);
    File fi = s_fs->open(nameIdem, FILE_WRITE);
    if (!fi) {
      Serial.printf("[queue] open(%s) FAILED — rollback body\n", nameIdem);
      s_fs->remove(nameBody);
      return false;
    }
    const size_t idemLen = strlen(idempotencyKey);
    const size_t idemWrote = fi.write(
        reinterpret_cast<const uint8_t*>(idempotencyKey), idemLen);
    fi.close();
    if (idemWrote != idemLen) {
      Serial.printf("[queue] idem write %u/%u bytes — rollback batch\n",
                    static_cast<unsigned>(idemWrote),
                    static_cast<unsigned>(idemLen));
      s_fs->remove(nameBody);
      s_fs->remove(nameIdem);
      return false;
    }
  }

  // Preserve the old item until the new body+idempotency pair is durable. If
  // deleting the old item fails, roll back the new pair instead of silently
  // exceeding capacity or losing both generations.
  if (atCapacity) {
    if (!deleteBatchFiles(oldestToDrop)) {
      char nameIdem[kFilenameLen];
      formatFilename(nameIdem, sizeof(nameIdem), epochSec, kIdemExt);
      s_fs->remove(nameBody);
      s_fs->remove(nameIdem);
      Serial.println("[queue] full but oldest delete FAILED — rolled back new batch");
      return false;
    }
    Serial.printf("[queue] full → dropped oldest epoch=%lu\n",
                  static_cast<unsigned long>(oldestToDrop));
    // Bình thường chỉ cập nhật index RAM. Full scan chỉ là recovery nếu index
    // lệch storage (ví dụ mất điện giữa hai thao tác).
    if (removeEpochFromIndex(oldestToDrop)) {
      if (!addEpochToIndex(epochSec)) refreshQueueIndex();
    } else {
      refreshQueueIndex();  // scan thấy file mới và không còn file cũ
    }
  } else {
    if (!addEpochToIndex(epochSec)) refreshQueueIndex();
  }
  return true;
}

bool queuePeekOldest(char* outBody, size_t outBodyLen, size_t* outBodyBytes,
                     char* outIdem, size_t outIdemLen,
                     uint32_t* outEpochSec) {
  if (!s_mounted) return false;
  const uint32_t oldest = s_oldest;
  if (s_depth == 0 || oldest == 0) return false;

  char nameBody[kFilenameLen];
  formatFilename(nameBody, sizeof(nameBody), oldest, kBodyExt);
  File f = s_fs->open(nameBody, FILE_READ);
  if (!f) {
    // The cached oldest may have disappeared after a power loss or a failed
    // delete/index refresh. Rebuild once so the next 10ms tick can continue.
    refreshQueueIndex();
    return false;
  }
  size_t fileSize = f.size();
  if (fileSize >= outBodyLen) {
    f.close();
    if (outBodyBytes) *outBodyBytes = fileSize;
    return false;
  }
  size_t read = f.readBytes(outBody, fileSize);
  outBody[read] = '\0';
  f.close();
  if (outBodyBytes) *outBodyBytes = read;

  if (outIdem && outIdemLen > 0) {
    outIdem[0] = '\0';
    char nameIdem[kFilenameLen];
    formatFilename(nameIdem, sizeof(nameIdem), oldest, kIdemExt);
    File fi = s_fs->open(nameIdem, FILE_READ);
    if (fi) {
      size_t n = fi.readBytes(outIdem, outIdemLen - 1);
      outIdem[n] = '\0';
      while (n > 0 && (outIdem[n - 1] == '\n' || outIdem[n - 1] == '\r')) {
        outIdem[--n] = '\0';
      }
      fi.close();
    }
  }

  if (outEpochSec) *outEpochSec = oldest;
  return true;
}

bool queueDelete(uint32_t epochSec) {
  if (!s_mounted) return false;
  if (!deleteBatchFiles(epochSec)) return false;
  if (!removeEpochFromIndex(epochSec) && !refreshQueueIndex()) {
    Serial.println("[queue] delete succeeded but index recovery failed; will rescan on next peek");
  }
  return true;
}

size_t queueSize() {
  return s_mounted ? s_depth : 0;
}

const char* queueStorageName() {
  switch (s_storageKind) {
    case StorageKind::Sd: return "sd";
    case StorageKind::LittleFs: return "littlefs";
    default: return "none";
  }
}

uint64_t queueStorageTotalBytes() {
#if SD_CARD_ENABLED
  if (s_storageKind == StorageKind::Sd) return SD.totalBytes();
#endif
  if (s_storageKind == StorageKind::LittleFs) return LittleFS.totalBytes();
  return 0;
}

uint64_t queueStorageUsedBytes() {
#if SD_CARD_ENABLED
  if (s_storageKind == StorageKind::Sd) return SD.usedBytes();
#endif
  if (s_storageKind == StorageKind::LittleFs) return LittleFS.usedBytes();
  return 0;
}

bool queueClear() {
  if (!s_mounted) return false;
  File dir = s_fs->open(kDir);
  if (!dir || !dir.isDirectory()) return false;

  static constexpr size_t kMaxScan = kMaxQueuedBatches * 2 + 16;
  char names[kMaxScan][kFilenameLen];
  size_t cnt = 0;

  File f = dir.openNextFile();
  while (f && cnt < kMaxScan) {
    const char* name = f.name();
    const char* base = strrchr(name, '/');
    if (base) base++; else base = name;
    snprintf(names[cnt], kFilenameLen, "%s/%s", kDir, base);
    cnt++;
    f.close();
    f = dir.openNextFile();
  }
  dir.close();

  for (size_t i = 0; i < cnt; ++i) {
    s_fs->remove(names[i]);
  }
  s_depth = 0;
  s_oldest = 0;
  s_newest = 0;
  return true;
}

}  // namespace queue
