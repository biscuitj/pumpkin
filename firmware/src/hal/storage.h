// Settings in NVS (one JSON blob per group, namespace "pumpkin") and the
// enterprise Wi-Fi CA certificate in LittleFS. A clock coming from the old
// SugarClock-based firmware has its settings carried over once.
#pragma once

#include <stddef.h>

#include "core/settings.h"

namespace hal {

// Loads into `s` (which holds the defaults). False when nothing was stored.
bool storageLoad(pk::Settings& s);
void storageSave(const pk::Settings& s, uint8_t groups);
void storageErase();  // factory reset: settings and certificate

bool caWrite(const char* pem, size_t len);
bool caDelete();
bool caExists();
size_t caRead(char* out, size_t size);  // 0 when missing or too big

} // namespace hal
