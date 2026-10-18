// Wall-clock time: the DS1307 real-time clock at boot, NTP once online, and the
// bridge's HTTP Date header when the network blocks NTP (schools often do).
#pragma once

#include <stdint.h>

#include "core/platform.h"

namespace hal {

void timeBegin(const char* posix);
void timeApplyZone(const char* posix);

// Call when the clock gets online; starts SNTP once (it re-syncs by itself).
void timeOnline();

// A "Date: Sun, 06 Nov 1994 08:49:37 GMT" header from an HTTPS answer. Used
// only while NTP hasn't delivered: it's good to a second or two.
void timeFromHttpDate(const char* value);

void timeLocal(pk::LocalTime& out, pk::TimeSource& source);
uint32_t timeUnix();  // 0 while unknown

} // namespace hal
