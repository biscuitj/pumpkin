#include "engine.h"

#include <stdio.h>

#include "art.h"
#include "glucose.h"
#include "pets.h"
#include "schedule.h"

namespace pk {

namespace {

constexpr uint32_t kBootPumpkinMs = 1200;
constexpr uint32_t kBootScrollMs = 3000;
constexpr uint32_t kBootMs = kBootPumpkinMs + kBootScrollMs;
constexpr uint32_t kFadeMs = 600;
constexpr uint32_t kPreviewMs = 8000;
constexpr uint32_t kAlertsPreviewMs = 7 * 5000 + 1000;  // every alert once
constexpr uint32_t kAddressMaxMs = 30000;
constexpr uint32_t kDeltaFlashMs = 3000;
constexpr uint32_t kWaveMs = 1800;
constexpr uint32_t kAlertMs = 5000;
constexpr uint8_t kStaleFailures = 5;
constexpr uint8_t kNoDataFailures = 10;
constexpr uint8_t kStaleMinBrightness = 20;  // a grey or dimmed reading must stay visible

bool isScroller(Screen s) { return s == Screen::Message || s == Screen::BridgeAlert; }

} // namespace

const char* screenName(Screen s) {
    switch (s) {
        case Screen::Glucose: return "glucose";
        case Screen::Tir: return "tir";
        case Screen::Iob: return "iob";
        case Screen::Alerts: return "alerts";
        case Screen::Time: return "time";
        case Screen::Pet: return "pet";
        case Screen::Message:
        case Screen::BridgeAlert:
        case Screen::Status:
        case Screen::Address: return "status";
        case Screen::Boot: return "boot";
        case Screen::Update: return "update";
    }
    return "status";
}

void Engine::begin(uint32_t now) {
    bootAt_ = now;
    screenStart_ = now;
    shownSince_ = now;
    current_ = Screen::Glucose;
    shown_ = target_ = Screen::Boot;
}

// ---------------------------------------------------------------- inputs

Engine::Fresh Engine::freshness(const EngineInputs& in) const {
    if (!in.haveData || !in.data || !in.data->valid) return Fresh::None;
    if (in.kind == SourceKind::Demo) return Fresh::Fresh;
    const uint32_t stale = (uint32_t)in.display->glucose.staleAfterMin * 60u;
    if (in.failures >= kStaleFailures || in.readingAgeSec >= stale) return Fresh::Stale;
    if (in.readingAgeSec >= stale / 2) return Fresh::Warning;
    return Fresh::Fresh;
}

void Engine::buildRotation(const EngineInputs& in) {
    const DisplaySettings& d = *in.display;
    const bool usable = fresh_ == Fresh::Fresh || fresh_ == Fresh::Warning;
    const bool alerts = d.screens.alerts && usable && in.data && in.data->warningCount > 0;
    int n = 0;
    rotation_[n++] = Screen::Glucose;
    if (in.haveData && in.data && in.data->bridgeAlert[0]) rotation_[n++] = Screen::BridgeAlert;
    if (in.haveData && in.data && in.data->message[0]) rotation_[n++] = Screen::Message;
    if (d.screens.tir) rotation_[n++] = Screen::Tir;
    if (d.screens.iob) rotation_[n++] = Screen::Iob;
    if (d.screens.time) rotation_[n++] = Screen::Time;
    if (d.screens.pet && !(alerts && d.alerts.replacePet)) rotation_[n++] = Screen::Pet;
    if (alerts) rotation_[n++] = Screen::Alerts;  // after the pet, or in its place
    rotationCount_ = n;
}

int Engine::indexOf(Screen s) const {
    for (int i = 0; i < rotationCount_; ++i)
        if (rotation_[i] == s) return i;
    return -1;
}

// ---------------------------------------------------------------- commands

void Engine::step(int dir, uint32_t now) {
    if (lowLocked_) return;  // the low reading stays put
    previewActive_ = false;
    addressActive_ = false;
    if (rotationCount_ == 0) return;
    int i = indexOf(current_);
    if (i < 0) i = 0;
    i = (i + dir + rotationCount_) % rotationCount_;
    current_ = rotation_[i];
    screenStart_ = now;
    needAdvance_ = false;
}

void Engine::wave(uint32_t now) {
    if (shown_ == Screen::Pet) happyUntil_ = now + kWaveMs;
}

void Engine::showAddress(uint32_t now) {
    if (lowLocked_) return;
    addressActive_ = true;
    addressStart_ = now;
}

bool Engine::preview(const Preview& p, uint32_t now) {
    if (lowLocked_ || urgent_) return false;
    preview_ = p;
    previewActive_ = true;
    previewUntil_ = now + (p.kind == Preview::Alerts ? kAlertsPreviewMs : kPreviewMs);
    if (p.kind == Preview::Alerts) {
        alertCurrent_ = Warning::None;
        alertsPlayed_ = 0;
        alertsDone_ = false;
        alertsLastRender_ = 0;
    }
    return true;
}

// ---------------------------------------------------------------- tick

void Engine::tick(const EngineInputs& in, EngineOutput& out) {
    const uint32_t now = in.now;
    const DisplaySettings& d = *in.display;
    fresh_ = freshness(in);
    buildRotation(in);

    // Glucose state
    const bool usable = fresh_ == Fresh::Fresh || fresh_ == Fresh::Warning;
    const int mgdl = (in.data && in.data->valid) ? in.data->mgdl : 0;
    lowLocked_ = d.glucose.lowLock && usable && mgdl < d.glucose.low && in.wifi != WifiMode::SetupAp;
    urgent_ = usable && (mgdl < d.glucose.urgentLow || mgdl > d.glucose.urgentHigh);

    // A new reading: remember the change for the flash.
    if (in.data && in.data->valid && in.data->timestamp != lastTimestamp_) {
        if (lastTimestamp_ != 0 && lastMgdl_ > 0) {
            delta_ = mgdl - lastMgdl_;
            if (d.glucose.deltaFlash && fresh_ == Fresh::Fresh && !lowLocked_) flashUntil_ = now + kDeltaFlashMs;
        }
        lastTimestamp_ = in.data->timestamp;
        lastMgdl_ = mgdl;
    }
    if (lowLocked_) flashUntil_ = 0;  // never hide a low value behind a delta

    // Keep the rotation screen valid as screens come and go.
    if (indexOf(current_) < 0) {
        current_ = Screen::Glucose;
        screenStart_ = now;
        needAdvance_ = false;
    }

    // Auto-cycle
    if (lowLocked_) {
        screenStart_ = now;  // a full turn for the current screen once the low is over
    } else if (d.rotation.autoCycle && rotationCount_ > 1 && !previewActive_ && !addressActive_) {
        bool advance;
        if (current_ == Screen::Alerts) advance = needAdvance_;
        else if (isScroller(current_)) advance = needAdvance_ && now - screenStart_ >= 2000;
        else advance = now - screenStart_ >= (uint32_t)d.rotation.seconds * 1000u;
        if (advance) step(1, now);
    }

    // Overlays expire
    if (previewActive_ && (int32_t)(now - previewUntil_) >= 0) previewActive_ = false;
    if (addressActive_ && now - addressStart_ >= kAddressMaxMs) addressActive_ = false;

    // What should be on the matrix, highest priority first.
    char status[sizeof(status_)];
    Rgb statusColor{0, 0, 0};
    bool alternate = false;
    Screen want;
    if (in.updating || in.restarting) {
        want = Screen::Update;
    } else if (now - bootAt_ < kBootMs) {
        want = Screen::Boot;
    } else if (lowLocked_) {
        want = Screen::Glucose;
    } else if (previewActive_ && !urgent_) {
        want = preview_.kind == Preview::Pet ? Screen::Pet : preview_.kind == Preview::Alerts ? Screen::Alerts : Screen::Glucose;
    } else if (addressActive_ && !urgent_) {
        want = Screen::Address;
    } else if (chooseStatus(in, status, sizeof(status), statusColor, alternate)) {
        want = Screen::Status;
        if (strcmp(status, status_) != 0) {
            copyStr(status_, status);
            statusStart_ = now;
        }
        statusColor_ = statusColor;
        statusAlternate_ = alternate;
    } else {
        want = current_;
    }
    // Boot, previews and status messages pause the rotation: the screen they
    // cover gets its full time afterwards.
    if (want != current_) screenStart_ = now;

    // Fade out, switch, fade in. A low reading switches at once.
    if (lowLocked_ && shown_ != Screen::Glucose) {
        shown_ = target_ = Screen::Glucose;
        shownSince_ = now;
        phase_ = Phase::None;
        fade_ = 255;
    }
    if (phase_ == Phase::None && want != shown_) {
        phase_ = Phase::Out;
        target_ = want;
        fadeStart_ = now;
        fadeFrom_ = fade_;
    }
    if (phase_ == Phase::Out) {
        if (want == shown_) {
            phase_ = Phase::In;  // the reason went away mid-fade: come back smoothly
            fadeStart_ = now;
            fadeFrom_ = fade_;
        } else {
            target_ = want;
            const uint32_t t = now - fadeStart_;
            if (t >= kFadeMs || shown_ == Screen::Boot) {
                shown_ = target_;
                shownSince_ = now;
                phase_ = Phase::In;
                fadeStart_ = now;
                fadeFrom_ = 0;
                fade_ = 0;
                if (shown_ == Screen::Alerts) alertsLastRender_ = 0;  // start with the first alert
            } else {
                fade_ = (uint8_t)((uint32_t)fadeFrom_ * (kFadeMs - t) / kFadeMs);
            }
        }
    }
    if (phase_ == Phase::In) {
        if (want != shown_) {
            phase_ = Phase::Out;
            target_ = want;
            fadeStart_ = now;
            fadeFrom_ = fade_;
        } else {
            const uint32_t t = now - fadeStart_;
            if (t >= kFadeMs) {
                fade_ = 255;
                phase_ = Phase::None;
            } else {
                fade_ = (uint8_t)(fadeFrom_ + (uint32_t)(255 - fadeFrom_) * t / kFadeMs);
            }
        }
    }

    out.frame.clear();
    render(shown_, in, out.frame);
    out.screen = shown_;
    out.fade = fade_;
    out.brightness = brightness(in, shown_);
}

// ---------------------------------------------------------------- status

bool Engine::chooseStatus(const EngineInputs& in, char* text, size_t size, Rgb& color, bool& alternate) {
    alternate = false;
    if (in.kind == SourceKind::Demo) return false;  // demo data needs nothing from the network

    if (in.wifi == WifiMode::SetupAp) {
        color = kTeal;
        switch (in.trial) {
            case TrialState::Associating: snprintf(text, size, "Joining %s", in.trialSsid); return true;
            case TrialState::Authenticating: snprintf(text, size, "Signing in to %s", in.trialSsid); return true;
            case TrialState::Connected:
                if (in.localName[0]) snprintf(text, size, "Connected! Visit %s or %s", in.localName, in.ip);
                else snprintf(text, size, "Connected! Visit %s", in.ip);
                return true;
            case TrialState::FailedAuth: snprintf(text, size, "%s turned down the password", in.trialSsid); return true;
            case TrialState::FailedNoAp: snprintf(text, size, "Can't find %s", in.trialSsid); return true;
            case TrialState::FailedTimeout: snprintf(text, size, "%s didn't answer. Try again", in.trialSsid); return true;
            default: break;
        }
        snprintf(text, size, "Wi-Fi setup: join %s, then open http://%s", in.apSsid, in.apIp);
        return true;
    }
    if (in.wifi != WifiMode::Connected) {
        if (!in.hasWifiCredentials) return false;
        snprintf(text, size, "NO|WIFI");
        color = kRed;
        alternate = true;
        return true;
    }
    if (in.dnsBlocked || in.dataBlocked) {
        color = kAmber;
        if (in.dnsBlocked) snprintf(text, size, "The network blocks name lookups (DNS)");
        else snprintf(text, size, "The network blocks %s", in.dataHost[0] ? in.dataHost : "the bridge");
        return true;
    }
    if (!in.sourceConfigured) {
        color = kTeal;
        if (in.localName[0]) snprintf(text, size, "Visit %s or %s to set up", in.localName, in.ip);
        else snprintf(text, size, "Visit %s to set up", in.ip);
        return true;
    }
    if (!in.haveData && in.sourceState != SourceState::Waiting && in.failures >= 1) {
        snprintf(text, size, "NO|DATA");
        color = kRed;
        alternate = true;
        return true;
    }
    if (in.haveData && in.failures >= kNoDataFailures) {
        snprintf(text, size, "NO|DATA");
        color = kRed;
        alternate = true;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- brightness

uint8_t Engine::brightness(const EngineInputs& in, Screen shown) const {
    const DisplaySettings& d = *in.display;
    int level;
    if (d.brightness.autoLevel) {
        level = 5 + (int)((uint32_t)in.light * (200 - 5) / 4095u);  // dark room 5, bright 200
    } else {
        level = d.brightness.level;
    }

    const LocalTime& t = in.time;
    const bool night = !awakeAt(*in.schedule, t.valid ? t.weekday : -1, t.valid ? t.hour * 60 + t.minute : -1);
    if (night) {
        if (in.schedule->night == NightMode::Mute) return 0;  // nothing wakes it, not even a low
        level = in.schedule->nightBrightness;
    }
    if (in.updating || in.restarting) return (uint8_t)(level < 40 ? 40 : level);

    const bool glucoseLike = shown == Screen::Glucose || shown == Screen::Pet;
    if (glucoseLike && fresh_ == Fresh::Warning) {
        level /= 3;
        if (level < kStaleMinBrightness) level = kStaleMinBrightness;
    } else if (glucoseLike && fresh_ == Fresh::Stale) {
        if (level < kStaleMinBrightness) level = kStaleMinBrightness;
    }
    if (level < 1) level = 1;
    if (level > 255) level = 255;
    return (uint8_t)level;
}

// ---------------------------------------------------------------- rendering

void Engine::render(Screen s, const EngineInputs& in, Frame& f) {
    const uint32_t now = in.now;
    switch (s) {
        case Screen::Boot: {
            const uint32_t t = now - bootAt_;
            if (t < kBootPumpkinMs) {
                drawBootPumpkin(f);
            } else {
                // "Pumpkin" enters from the right and leaves on the left.
                const int start = kWidth, end = -textWidth("Pumpkin");
                const int x = start - (int)((int64_t)(start - end) * (int64_t)(t - kBootPumpkinMs) / kBootScrollMs);
                drawText(f, "Pumpkin", x < end ? end : x, 0, kOrange);
            }
            break;
        }
        case Screen::Update: {
            char text[12];
            if (in.restarting) snprintf(text, sizeof(text), "REBOOT");
            else if (in.updateProgress >= 0) snprintf(text, sizeof(text), "%d%%", in.updateProgress);
            else snprintf(text, sizeof(text), "UPDATE");
            drawCentered(f, text, kTeal);
            break;
        }
        case Screen::Glucose:
            renderGlucose(in, f, previewActive_ && preview_.kind == Preview::Glucose ? preview_.mgdl : -1);
            break;
        case Screen::Tir: renderTir(in, f); break;
        case Screen::Iob: renderIob(in, f); break;
        case Screen::Time: renderTime(in, f); break;
        case Screen::Pet: renderPet(in, f, previewActive_ && preview_.kind == Preview::Pet); break;
        case Screen::Alerts: renderAlerts(in, f); break;
        case Screen::Message:
        case Screen::BridgeAlert: {
            const bool alert = s == Screen::BridgeAlert;
            const char* text = in.data ? (alert ? in.data->bridgeAlert : in.data->message) : "";
            if (drawScroll(f, text, alert ? kAmber : kWhite, now - shownSince_)) {
                if (current_ == s) needAdvance_ = true;
            }
            break;
        }
        case Screen::Address: {
            char text[96];
            if (in.localName[0]) snprintf(text, sizeof(text), "Visit %s or %s", in.localName, in.ip);
            else if (in.ip[0]) snprintf(text, sizeof(text), "Visit %s", in.ip);
            else snprintf(text, sizeof(text), "Not on Wi-Fi");
            if (drawScroll(f, text, kTeal, now - addressStart_)) addressActive_ = false;
            break;
        }
        case Screen::Status: renderStatus(in, f); break;
    }
}

void Engine::renderStatus(const EngineInputs& in, Frame& f) {
    if (statusAlternate_) {
        // "NO|WIFI": two short words, two seconds each.
        char first[16], second[16];
        const char* bar = strchr(status_, '|');
        if (!bar) return;
        snprintf(first, sizeof(first), "%.*s", (int)(bar - status_), status_);
        copyStr(second, bar + 1);
        drawCentered(f, ((in.now - statusStart_) / 2000) % 2 == 0 ? first : second, statusColor_);
        return;
    }
    drawScroll(f, status_, statusColor_, in.now - statusStart_, 70);
}

void Engine::renderGlucose(const EngineInputs& in, Frame& f, int mgdlOverride) {
    const DisplaySettings& d = *in.display;
    const ColorPreset preset = d.glucose.colors;
    const bool previewing = mgdlOverride > 0;
    if (!previewing && (!in.haveData || !in.data || !in.data->valid)) {
        drawText(f, "---", 7, 0, kGrey);
        return;
    }
    const int mgdl = previewing ? mgdlOverride : in.data->mgdl;
    const Trend trend = previewing ? Trend::Flat : in.data->trend;
    const Fresh fresh = previewing ? Fresh::Fresh : fresh_;
    const Rgb c = fresh == Fresh::Stale ? presetColors(preset).stale : bandColor(bandOf(mgdl, d), preset);

    if (!previewing && (int32_t)(flashUntil_ - in.now) > 0 && fresh == Fresh::Fresh) {
        char text[8];
        formatDelta(text, sizeof(text), delta_, d.glucose.unit);
        drawCentered(f, text, c);
        return;
    }

    char text[8];
    formatGlucose(text, sizeof(text), mgdl, d.glucose.unit);
    // The value and its arrow together are centred: 6 px per glyph, 6 for the arrow.
    const int w = textWidth(text);
    const int x = (kWidth - w - 6) / 2;
    drawText(f, text, x, 0, c);
    drawTrend(f, trend, x + w + 1, c);

    // Sport mode (temp target): 2 px bars at both edges. Not on old data:
    // the "!" uses the right edge then.
    if (fresh == Fresh::Fresh && !previewing && in.data->tempTarget) {
        for (int y = 0; y < kHeight; ++y) {
            f.set(0, y, c);
            f.set(1, y, c);
            f.set(kWidth - 2, y, c);
            f.set(kWidth - 1, y, c);
        }
    }
    if (fresh == Fresh::Warning) drawText(f, "!", kWidth - 4, 0, kYellow);
}

void Engine::renderTir(const EngineInputs& in, Frame& f) {
    const PresetColors& c = presetColors(in.display->glucose.colors);
    const bool usable = fresh_ == Fresh::Fresh || fresh_ == Fresh::Warning;
    if (usable && in.data && in.data->tir >= 0) {
        char text[12];
        snprintf(text, sizeof(text), "%d%%", in.data->tir);
        // >= 70 % is the usual consensus goal.
        const Rgb col = in.data->tir >= 70 ? c.inRange : in.data->tir >= 50 ? c.high : c.urgentLow;
        drawRight(f, text, col);
    } else {
        drawRight(f, "--%", c.stale);
    }
}

void Engine::renderIob(const EngineInputs& in, Frame& f) {
    const bool usable = fresh_ == Fresh::Fresh || fresh_ == Fresh::Warning;
    char text[10];
    if (usable && in.data && in.data->iobValid) {
        // Two decimals below 10 U, one above: always 5 glyphs at most.
        snprintf(text, sizeof(text), in.data->iob < 9.995f ? "%.2fU" : "%.1fU", (double)in.data->iob);
        drawCentered(f, text, kCyan);
    } else {
        drawCentered(f, "--U", presetColors(in.display->glucose.colors).stale);
    }
}

void Engine::renderTime(const EngineInputs& in, Frame& f) {
    const LocalTime& t = in.time;
    if (!t.valid) {
        drawText(f, "--:--", 4, 0, kGrey);
        return;
    }
    const DisplaySettings& d = *in.display;
    char text[8];
    // With a date: time and date take turns, five seconds each.
    if (d.time.date != DateFormat::Off && ((in.now - shownSince_) / 5000) % 2 == 1) {
        if (d.time.date == DateFormat::DayMonth) snprintf(text, sizeof(text), "%d/%d", t.day, t.month);
        else snprintf(text, sizeof(text), "%d/%d", t.month, t.day);
        drawCentered(f, text, kWhite);
        return;
    }
    int h = t.hour;
    if (!d.time.hour24) {
        h %= 12;
        if (h == 0) h = 12;
    }
    snprintf(text, sizeof(text), t.second % 2 == 0 ? "%d:%02d" : "%d %02d", h, t.minute);
    drawText(f, text, (kWidth - textWidth(text)) / 2, 0, kWhite);
}

void Engine::renderPet(const EngineInputs& in, Frame& f, bool previewing) {
    const DisplaySettings& d = *in.display;
    const PresetColors& c = presetColors(d.glucose.colors);
    int range = 0;
    PetId id = d.pet.id;
    if (previewing) {
        id = preview_.pet;
        range = preview_.range;
    } else {
        if (fresh_ == Fresh::None || fresh_ == Fresh::Stale) {
            // No pose without a reading to base it on.
            drawText(f, "---", 7, 0, c.stale);
            return;
        }
        const Band b = bandOf(in.data->mgdl, d);
        range = (b == Band::Low || b == Band::UrgentLow) ? 1 : (b == Band::High || b == Band::UrgentHigh) ? 2 : 0;
    }

    const LocalTime& t = in.time;
    const bool night = !awakeAt(*in.schedule, t.valid ? t.weekday : -1, t.valid ? t.hour * 60 + t.minute : -1);
    const uint32_t frame = in.now / 100;
    const bool waving = (int32_t)(happyUntil_ - in.now) > 0;
    // Now and then an in-range pet waves on its own.
    const bool happy = waving || (!night && range == 0 && frame % 80 < 16);
    const uint32_t ms = waving ? in.now - (happyUntil_ - kWaveMs) : in.now;

    char px[8][32];
    petFrame(id, ms, night, happy, d.pet.style, range, px);

    const bool showTir = d.pet.showTir && d.pet.style == PetStyle::Text && in.data && in.data->tir >= 0 && !previewing;
    if (showTir)
        for (int y = 0; y < 8; ++y)
            for (int x = 14; x < 32; ++x) px[y][x] = '.';

    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 32; ++x)
            if (px[y][x] != '.') f.set(x, y, rgb(petPixelColor(px[y][x], c.low, c.inRange, c.high)));

    if (showTir) {
        // 3x5 digits and '%' where LOW / OK / HIGH would be.
        static const char* const kDigits[11][5] = {
            {"111", "101", "101", "101", "111"}, {"010", "110", "010", "010", "111"}, {"111", "001", "111", "100", "111"},
            {"111", "001", "111", "001", "111"}, {"101", "101", "111", "001", "001"}, {"111", "100", "111", "001", "111"},
            {"111", "100", "111", "101", "111"}, {"111", "001", "010", "010", "010"}, {"111", "101", "111", "101", "111"},
            {"111", "101", "111", "001", "111"}, {"101", "001", "010", "100", "101"}};
        const int tir = in.data->tir;
        const Rgb col = tir >= 70 ? c.inRange : tir >= 50 ? c.high : c.urgentLow;
        char text[6];
        snprintf(text, sizeof(text), "%d%%", tir);
        const int len = (int)strlen(text);
        const int start = 15 + (17 - (len * 4 - 1)) / 2;
        for (int i = 0; i < len; ++i) {
            const int g = text[i] == '%' ? 10 : text[i] - '0';
            for (int y = 0; y < 5; ++y)
                for (int x = 0; x < 3; ++x)
                    if (kDigits[g][y][x] == '1') f.set(start + i * 4 + x, y + 2, col);
        }
    }

    // Seasonal surprises, only around a lone, in-range pet.
    if (d.pet.seasonal && d.pet.style == PetStyle::PetOnly && range == 0 && t.valid) {
        if (t.month == 10 && t.day == 31) {
            const Rgb purple = rgb(0x944BCD), orange = rgb(0xF4762A);
            f.set(29, 0, purple);
            for (int x = 28; x <= 30; ++x) f.set(x, 1, purple);
            f.set(29, 2, purple);
            f.set(2, 6, orange);
        } else if ((t.month == 12 && t.day == 31) || (t.month == 1 && t.day == 1)) {
            static const uint8_t xs[] = {1, 4, 28, 31};
            for (unsigned i = 0; i < sizeof(xs); ++i) {
                const int y = (int)((frame / 3 + i * 2) % 7);
                f.set(xs[i], y, (i & 1) ? rgb(0xE83C36) : rgb(0xFFBE42));
            }
        }
    }
}

void Engine::renderAlerts(const EngineInputs& in, Frame& f) {
    const uint32_t now = in.now;
    const bool previewing = previewActive_ && preview_.kind == Preview::Alerts;
    static const Warning kAll[] = {Warning::Suspended, Warning::Alert,  Warning::Battery, Warning::Reservoir,
                                   Warning::Bg,        Warning::Sensor, Warning::Set};
    const Warning* items = previewing ? kAll : (in.data ? in.data->warnings : nullptr);
    const int count = previewing ? 7 : (in.data ? in.data->warningCount : 0);

    // (Re)entering the screen always starts with the first alert.
    if (alertsLastRender_ == 0 || now - alertsLastRender_ > 1000) {
        alertCurrent_ = Warning::None;
        alertsPlayed_ = 0;
        alertsDone_ = false;
    }
    alertsLastRender_ = now;

    // The alert on screen cleared (new data): drop it.
    bool there = false;
    for (int i = 0; i < count; ++i) there |= items[i] == alertCurrent_;
    if (!there) alertCurrent_ = Warning::None;

    // Next: the first in priority order that hasn't played. Tracked by type,
    // so an alert that clears never makes another one skip.
    if (alertCurrent_ == Warning::None && !alertsDone_) {
        for (int i = 0; i < count; ++i) {
            if (!(alertsPlayed_ & (1u << (int)items[i]))) {
                alertCurrent_ = items[i];
                alertStarted_ = now;
                break;
            }
        }
        if (alertCurrent_ == Warning::None) alertsDone_ = true;
    }

    if (alertCurrent_ != Warning::None) {
        drawWarning(f, alertCurrent_);
        if (now - alertStarted_ >= kAlertMs) {
            alertsPlayed_ |= (uint16_t)(1u << (int)alertCurrent_);
            alertCurrent_ = Warning::None;
            bool more = false;
            for (int i = 0; i < count; ++i) more |= !(alertsPlayed_ & (1u << (int)items[i]));
            if (!more) alertsDone_ = true;
        }
    }

    if (alertsDone_) {
        if (previewing) {
            previewActive_ = false;  // played once: back to what it interrupted
        } else if (in.display->rotation.autoCycle && rotationCount_ > 1) {
            needAdvance_ = true;
        } else {
            alertsLastRender_ = 0;  // no rotation to move on: loop the alerts
        }
    }
}

} // namespace pk
