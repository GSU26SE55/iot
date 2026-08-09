#pragma once

namespace portal {

// Start the setup web application. The app is always available through the
// station IP and starts a fallback access point when the station is offline.
void setupPortalBegin();

// Handle HTTP/DNS requests and Wi-Fi fallback state. Call frequently.
void setupPortalTick();

bool setupPortalApActive();

}  // namespace portal
