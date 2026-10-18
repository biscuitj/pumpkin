// Awake hours per weekday, night mode and the next switch between them.
#pragma once

#include "settings.h"

namespace pk {

enum class DayState : uint8_t { Awake, Night, Off };

struct ScheduleNow {
    bool timeValid;
    int weekday;  // 0 = Monday, -1 without time
    int minute;   // since local midnight, -1 without time
    DayState state;
    bool hasNext;
    int nextWeekday;
    int nextMinute;
    bool nextToAwake;
};

// Without a valid time the clock stays awake: it must never go dark by mistake.
bool awakeAt(const ScheduleSettings& s, int weekday, int minute);
ScheduleNow scheduleNow(const ScheduleSettings& s, bool timeValid, int weekday, int minute);

// tm_wday (0 = Sunday) to our weekday (0 = Monday).
inline int weekdayFromTm(int tmWday) { return (tmWday + 6) % 7; }

} // namespace pk
