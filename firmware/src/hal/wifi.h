// Wi-Fi: joins the saved network, keeps retrying, and opens the setup hotspot
// (open, "Pumpkin-Setup-E5F6", with a captive-portal DNS) when there's no
// network saved or nothing has worked for two minutes. Joins from the
// dashboard run as a trial: the credentials are only kept once they work.
//
// wifiLoop() runs on the main loop and does all radio work; the other calls
// are safe from the web server task and only read state or queue requests.
#pragma once

#include "core/platform.h"
#include "core/settings.h"

namespace hal {

void wifiBegin(const pk::WifiSettings& saved, const char* setupSsid, const char* hostname);
void wifiLoop(const pk::WifiSettings& saved);

bool wifiConnected();  // joined with an address
bool wifiApUp();
bool wifiOnAp(uint32_t localIp);  // a request that arrived over the setup hotspot

void wifiView(pk::WifiView& out);  // all but localName and caInstalled
int wifiScan(pk::ScanEntry* out, int max, bool& scanning, int32_t& ageSec);
bool wifiStartScan();
bool wifiJoin(const pk::WifiJoinRequest& r);

// Credentials of a trial that just worked, once: the caller saves them.
bool wifiTakeJoined(pk::WifiSettings& out);

} // namespace hal
