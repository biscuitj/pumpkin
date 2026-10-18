#include "app.h"

namespace pk {

void App::markDirty(uint8_t groups) {
    dirty |= groups;
    dirtyAt = platform.millis();
}

bool App::sourceConfigured() const {
    if (settings.source.kind == SourceKind::Demo) return true;
    return settings.source.bridgeUrl[0] && settings.source.token[0];
}

uint32_t App::readingAgeSec(uint32_t now) const {
    if (!source.haveData) return UINT32_MAX;
    return (now - source.readingAtMs) / 1000u;
}

SourceState App::sourceState() const {
    if (settings.source.kind == SourceKind::Demo) return SourceState::Demo;
    if (!sourceConfigured()) return SourceState::NotConfigured;
    if (!source.fetched) return SourceState::Waiting;
    return source.state;
}

void App::resetSource() {
    source = SourceRuntime{};
}

} // namespace pk
