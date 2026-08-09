#pragma once

namespace portal {

// Start the authenticated administration app on CONFIG_PORTAL_PORT. Access is
// through the station IP or the AP managed by net/setup_portal when active.
void setupPortalBegin();

// Handle HTTP requests and scheduled restart. Call frequently.
void setupPortalTick();

bool setupPortalApActive();

}  // namespace portal
