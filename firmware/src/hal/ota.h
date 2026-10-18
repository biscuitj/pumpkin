// Firmware updates uploaded from the dashboard, and the first-boot check that
// keeps a new firmware only once it runs well (otherwise the bootloader goes
// back to the previous one). NVS namespace "sugarota", kept from SugarClock.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/platform.h"

namespace hal {

void otaBegin();
// Main loop: first-boot validation. `healthy` = settings loaded and the
// dashboard is embedded; the loop counter and heap are checked here.
void otaLoop(bool healthy);

// Upload, from the web server task. begin() refuses while another upload runs.
bool otaUploadBegin(size_t size);
bool otaUploadWrite(const uint8_t* data, size_t len);
bool otaUploadEnd();  // true: checked and selected for the next boot
void otaUploadAbort();

void otaView(pk::UpdateView& out);
int otaProgress();  // %, -1 when not receiving
bool otaRolledBack(char* fromVersion, size_t size);

} // namespace hal
