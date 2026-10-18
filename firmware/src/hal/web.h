// The web server: the dashboard pages, the API (core/api) and firmware
// uploads, behind the optional password (HTTP Basic, user "pumpkin").
#pragma once

#include "core/app.h"

namespace hal {

void webBegin(pk::App& app);

} // namespace hal
