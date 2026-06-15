// ==================================================================
// Sprint 3 — S3-FW-01: Local queue implementation (LittleFS-backed)
//
// File layout dưới /queue/:
//   <epoch10>.json  — body JSON batch
//   <epoch10>.idem  — idempotency key (single line)
//
// Lý do filename = epoch zero-padded 10 chars: lexicographic sort = chronological.
// dir.openNextFile() trên LittleFS không guarantee order — manual sort needed
// để pick oldest cho FIFO.
// ==================================================================
#include "queue/local_queue.h"
#include "queue/queue_index.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <stdio.h>
#include <string.h>

namespace queue {

namespace {
constexpr const char* kDir         = "/queue";
constexpr const char* kBodyExt     = ".json";
constexpr const char* kIdemExt     = ".idem";
constexpr size_t      kFilenameLen = 32;     // /queue/<10 digit>.json + null

bool s_mounted = false;

void formatFilename(char* buf, size_t bufLen, uint32_t epochSec, const char* ext) {
  snprintf(buf, bufLen, "%s/%010lu%s", kDir, static_cast<unsigned long>(epochSec), ext);
}

// Parse epoch từ filename "<10digit>.json|.idem".
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

// Scan /queue/ → trả về epoch oldest (min) và largest (max). 0 nếu rỗng.
bool scanQueue(uint32_t* outOldest, uint32_t* outNewest, size_t* outCount) {
  if (outOldest) *outOldest = 0;
  if (outNewest) *outNewest = 0;
  if (outCount)  *outCount = 0;

  File dir = LittleFS.open(kDir);
  if (!dir || !dir.isDirectory()) return false;

  uint32_t oldest = UINT32_MAX;
  uint32_t newest = 0;
  size_t   count  = 0;

  File f = dir.openNextFile();
  while (f) {
    const char* name = f.name();
    // Bỏ leading "/" hoặc dir prefix nếu LittleFS trả full path
    const char* base = strrchr(name, '/');
    if (base) base++; else base = name;

    // Chỉ count .json (1 batch = 1 .json + 1 .idem)
    size_t nameLen = strlen(base);
    if (nameLen > 5 && strcmp(base + nameLen - 5, kBodyExt) == 0) {
      uint32_t epoch = 0;
      if (parseEpochFromName(base, &epoch)) {
        count++;
        if (epoch < oldest) oldest = epoch;
        if (epoch > newest) newest = epoch;
      }
    }
    f.close();
    f = dir.openNextFile();
  }
  dir.close();

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

bool deleteBatchFiles(uint32_t epochSec) {
  char body[kFilenameLen], idem[kFilenameLen];
  formatFilename(body, sizeof(body), epochSec, kBodyExt);
  formatFilename(idem, sizeof(idem), epochSec, kIdemExt);
  bool ok = LittleFS.remove(body);
  LittleFS.remove(idem);   // best-effort cho .idem (có thể không tồn tại)
  return ok;
}
}  // namespace

bool queueBegin() {
  if (s_mounted) return true;
  if (!LittleFS.begin(true /*formatOnFail*/)) {
    Serial.println("[queue] LittleFS mount FAILED");
    return false;
  }
  s_mounted = true;

  if (!LittleFS.exists(kDir)) {
    if (!LittleFS.mkdir(kDir)) {
      Serial.println("[queue] mkdir /queue FAILED");
      return false;
    }
  }

  size_t initial = queueSize();
  Serial.printf("[queue] mount OK; depth=%u, max=%u\n",
                static_cast<unsigned>(initial),
                static_cast<unsigned>(kMaxQueuedBatches));
  return true;
}

bool queueEnqueue(uint32_t epochSec,
                  const char* body, size_t bodyLen,
                  const char* idempotencyKey) {
  if (!s_mounted) {
    if (!queueBegin()) return false;
  }
  if (!body || bodyLen == 0) return false;

  // Nếu đầy → drop oldest trước
  size_t count = 0;
  uint32_t oldest = 0;
  scanQueue(&oldest, nullptr, &count);
  if (count >= kMaxQueuedBatches && oldest != 0) {
    deleteBatchFiles(oldest);
    Serial.printf("[queue] full → dropped oldest epoch=%lu\n",
                  static_cast<unsigned long>(oldest));
  }

  // Tránh ghi đè cùng epoch (caller cần epoch unique; nếu duplicate, +1)
  char nameBody[kFilenameLen];
  formatFilename(nameBody, sizeof(nameBody), epochSec, kBodyExt);
  while (LittleFS.exists(nameBody)) {
    epochSec++;
    formatFilename(nameBody, sizeof(nameBody), epochSec, kBodyExt);
  }

  // Ghi body
  File f = LittleFS.open(nameBody, FILE_WRITE);
  if (!f) {
    Serial.printf("[queue] open(%s) FAILED\n", nameBody);
    return false;
  }
  size_t wrote = f.write(reinterpret_cast<const uint8_t*>(body), bodyLen);
  f.close();
  if (wrote != bodyLen) {
    Serial.printf("[queue] write %u/%u bytes — partial — delete\n",
                  static_cast<unsigned>(wrote), static_cast<unsigned>(bodyLen));
    LittleFS.remove(nameBody);
    return false;
  }

  // Ghi idempotency key (optional — Sprint 3 luôn có)
  if (idempotencyKey && idempotencyKey[0]) {
    char nameIdem[kFilenameLen];
    formatFilename(nameIdem, sizeof(nameIdem), epochSec, kIdemExt);
    File fi = LittleFS.open(nameIdem, FILE_WRITE);
    if (fi) {
      fi.print(idempotencyKey);
      fi.close();
    }
  }
  return true;
}

bool queuePeekOldest(char* outBody, size_t outBodyLen, size_t* outBodyBytes,
                     char* outIdem, size_t outIdemLen,
                     uint32_t* outEpochSec) {
  if (!s_mounted) return false;
  uint32_t oldest = 0;
  size_t count = 0;
  scanQueue(&oldest, nullptr, &count);
  if (count == 0 || oldest == 0) return false;

  char nameBody[kFilenameLen];
  formatFilename(nameBody, sizeof(nameBody), oldest, kBodyExt);
  File f = LittleFS.open(nameBody, FILE_READ);
  if (!f) return false;
  size_t fileSize = f.size();
  if (fileSize >= outBodyLen) {
    f.close();
    if (outBodyBytes) *outBodyBytes = fileSize;
    return false;        // buffer too small
  }
  size_t read = f.readBytes(outBody, fileSize);
  outBody[read] = '\0';
  f.close();
  if (outBodyBytes) *outBodyBytes = read;

  // Idem (optional)
  if (outIdem && outIdemLen > 0) {
    outIdem[0] = '\0';
    char nameIdem[kFilenameLen];
    formatFilename(nameIdem, sizeof(nameIdem), oldest, kIdemExt);
    File fi = LittleFS.open(nameIdem, FILE_READ);
    if (fi) {
      size_t n = fi.readBytes(outIdem, outIdemLen - 1);
      outIdem[n] = '\0';
      // Strip newline cuối nếu có
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
  return deleteBatchFiles(epochSec);
}

size_t queueSize() {
  if (!s_mounted) return 0;
  size_t count = 0;
  scanQueue(nullptr, nullptr, &count);
  return count;
}

bool queueClear() {
  if (!s_mounted) return false;
  File dir = LittleFS.open(kDir);
  if (!dir || !dir.isDirectory()) return false;

  // Collect filenames trước (xóa trong loop có thể vô hiệu hóa iterator)
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
    LittleFS.remove(names[i]);
  }
  return true;
}

}  // namespace queue
