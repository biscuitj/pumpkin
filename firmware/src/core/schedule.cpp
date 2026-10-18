#include "schedule.h"

namespace pk {

bool awakeAt(const ScheduleSettings& s, int weekday, int minute) {
    if (!s.enabled) return true;
    if (weekday < 0 || weekday > 6 || minute < 0) return true;
    const AwakeHours& a = s.awake[weekday];
    return a.on && minute >= a.from && minute < a.to;
}

ScheduleNow scheduleNow(const ScheduleSettings& s, bool timeValid, int weekday, int minute) {
    ScheduleNow n{};
    n.timeValid = timeValid;
    n.weekday = timeValid ? weekday : -1;
    n.minute = timeValid ? minute : -1;
    if (!s.enabled) {
        n.state = DayState::Off;
        return n;
    }
    if (!timeValid) {
        n.state = DayState::Awake;
        return n;
    }
    const bool awake = awakeAt(s, weekday, minute);
    n.state = awake ? DayState::Awake : DayState::Night;

    // The state can only flip at a day's from/to or at midnight: check those
    // points over the coming week, in order.
    for (int dayOffset = 0; dayOffset <= 7 && !n.hasNext; ++dayOffset) {
        const int day = (weekday + dayOffset) % 7;
        const AwakeHours& a = s.awake[day];
        int points[3] = {0, a.from, a.to};
        for (int p : points) {
            if (p >= 1440) continue;
            if (dayOffset == 0 && p <= minute) continue;
            if (awakeAt(s, day, p) != awake) {
                n.hasNext = true;
                n.nextWeekday = day;
                n.nextMinute = p;
                n.nextToAwake = !awake;
                break;
            }
        }
    }
    return n;
}

} // namespace pk
