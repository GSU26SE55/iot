#pragma once

#include <IPAddress.h>

namespace net {

/// Resolve một hostname `.local` bằng mDNS multicast, có cache ngắn hạn.
/// Trả false để caller dùng đường DNS/client mặc định làm fallback.
bool resolveMdnsHost(const char* host, IPAddress& outAddress);

}  // namespace net
