#include "runner.h"

namespace pk {

const EngineOutput& Runner::frame(const HardwareInputs& hw) {
    const uint32_t now = app_.platform.millis();
    EngineInputs in;
    bool next = false, prev = false, preview = false;
    Preview p{};
    {
        Locked l(app_.mutex);
        settings_.display = app_.settings.display;
        settings_.schedule = app_.settings.schedule;
        data_ = app_.source.data;
        in.kind = app_.settings.source.kind;
        in.sourceConfigured = app_.sourceConfigured();
        in.sourceState = app_.sourceState();
        in.haveData = app_.source.haveData;
        in.readingAgeSec = app_.readingAgeSec(now);
        in.failures = app_.source.failures;
        in.hasWifiCredentials = app_.settings.wifi.ssid[0] != '\0';
        in.dnsBlocked = app_.net.dns == CheckResult::Fail;
        in.dataBlocked = app_.net.data == CheckResult::Fail;
        copyStr(dataHost_, app_.net.dataHost);

        next = app_.commands.next;
        prev = app_.commands.prev;
        preview = app_.commands.preview;
        p = app_.commands.p;
        app_.commands = EngineCommands{};
    }

    app_.platform.wifiView(wifi_);
    TimeSource src;
    app_.platform.localTime(in.time, src);

    in.now = now;
    in.display = &settings_.display;
    in.schedule = &settings_.schedule;
    in.data = &data_;
    in.wifi = wifi_.mode;
    in.ip = wifi_.ip;
    in.localName = wifi_.localName;
    in.apSsid = wifi_.apSsid;
    in.apIp = wifi_.apIp;
    in.trial = wifi_.trial;
    in.trialSsid = wifi_.trialSsid;
    in.dataHost = dataHost_;
    in.light = hw.light;
    in.updating = hw.updating;
    in.updateProgress = hw.updateProgress;
    in.restarting = hw.restarting;

    if (next) engine_.next(now);
    if (prev) engine_.prev(now);
    if (preview) engine_.preview(p, now);

    engine_.tick(in, out_);

    {
        Locked l(app_.mutex);
        Mirror& m = app_.mirror;
        m.lowLocked = engine_.lowLocked();
        m.urgent = engine_.urgent();
        if (m.watched(now)) {
            if (!m.ready || !(m.frame == out_.frame)) {
                m.frame = out_.frame;
                ++m.sequence;
            }
            m.screen = out_.screen;
            m.ready = true;
        } else {
            m.ready = false;  // nobody looking: don't copy every frame
        }
    }
    return out_;
}

void Runner::button(Button b) {
    const uint32_t now = app_.platform.millis();
    switch (b) {
        case Button::Left: engine_.prev(now); break;
        case Button::Right: engine_.next(now); break;
        case Button::Middle: engine_.wave(now); break;
        case Button::MiddleLong: engine_.showAddress(now); break;
    }
}

} // namespace pk
