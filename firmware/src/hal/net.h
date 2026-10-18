// The network task: fetches from the bridge (core/net_jobs), runs the
// reachability checks a school network needs, and keeps the clock's
// "name.local" address up. Blocking network calls live here and nowhere else.
#pragma once

#include "core/app.h"

namespace hal {

void netStart(pk::App& app, const char* chipId);

void netRecheck();      // re-run the reachability checks
void netWake();         // fetch now
void netNameChanged();  // re-announce under the new name

// "keuken.local" once mDNS is up, "" otherwise.
void netLocalName(char* out, size_t size);

} // namespace hal
