#pragma once

#include <cstdint>

namespace net {

// AlwaysAvailable is the normal product mode. The older values remain so
// upgrades and the Wi-Fi phase state machine keep a stable interface.
enum class PortalMode : uint8_t {
  Off = 0,
  FirstTimeSetup = 1,
  Recovery = 2,
  AlwaysAvailable = 3,
};

// SolarGW- plus the final four hexadecimal MAC characters.
const char* portalApSsid();

// Starts the WPA2 AP. AlwaysAvailable keeps it alongside the home Wi-Fi STA.
bool portalStart(PortalMode mode);

// Keeps the AP available if a Wi-Fi driver transition removed its radio bit.
void portalTick();

// Explicit shutdown hook. Normal product flow does not call this.
void portalStop();

bool portalIsActive();
PortalMode portalMode();
uint32_t portalStartedAtMs();

}  // namespace net
