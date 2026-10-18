#include "clock_time.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_sntp.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "pins.h"

namespace hal {

namespace {

constexpr uint8_t kRtcAddress = 0x68;  // DS1307
constexpr time_t kSane = 1700000000;   // anything earlier is an unset clock

volatile pk::TimeSource source = pk::TimeSource::None;
volatile bool ntpArrived = false;
bool rtcPresent = false;
bool sntpStarted = false;
uint32_t rtcWrittenAt = 0;
char zone[64] = "UTC0";

uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

// UTC seconds for a civil date (Howard Hinnant's days_from_civil): no TZ juggling,
// which would briefly shift the time other tasks read.
time_t utcFrom(int year, int month, int day, int h, int m, int s) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long days = (long)era * 146097 + (long)doe - 719468;
    return (time_t)(days * 86400L + h * 3600L + m * 60L + s);
}

// The RTC keeps UTC, so a zone change never shifts it.
bool rtcRead(time_t& out) {
    Wire.beginTransmission(kRtcAddress);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom((int)kRtcAddress, 7) < 7) return false;
    const uint8_t secReg = Wire.read();
    if (secReg & 0x80) return false;  // clock halted: never set
    const int sec = bcd2dec(secReg & 0x7F);
    const int min = bcd2dec(Wire.read());
    const int hour = bcd2dec(Wire.read() & 0x3F);
    Wire.read();  // weekday
    const int day = bcd2dec(Wire.read());
    const int month = bcd2dec(Wire.read());
    const int year = bcd2dec(Wire.read()) + 2000;
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || min > 59 || sec > 59) return false;
    out = utcFrom(year, month, day, hour, min, sec);
    return out >= kSane;
}

void rtcWrite(time_t now) {
    if (!rtcPresent) return;
    struct tm t;
    gmtime_r(&now, &t);
    Wire.beginTransmission(kRtcAddress);
    Wire.write(0x00);
    Wire.write(dec2bcd(t.tm_sec));
    Wire.write(dec2bcd(t.tm_min));
    Wire.write(dec2bcd(t.tm_hour));  // bit 6 clear: 24-hour mode
    Wire.write(dec2bcd(t.tm_wday + 1));
    Wire.write(dec2bcd(t.tm_mday));
    Wire.write(dec2bcd(t.tm_mon + 1));
    Wire.write(dec2bcd(t.tm_year - 100));
    Wire.endTransmission();
    rtcWrittenAt = millis();
}

void onSntp(struct timeval* tv) {
    ntpArrived = true;
    source = pk::TimeSource::Ntp;
    // At most hourly: the DS1307 drifts slowly and I2C is shared with nothing.
    if (rtcWrittenAt == 0 || millis() - rtcWrittenAt > 3600000UL) rtcWrite(tv->tv_sec);
}

int monthIndex(const char* m) {
    static const char* const kMonths = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char* p = strstr(kMonths, m);
    return (p && m[0] && (p - kMonths) % 3 == 0) ? (int)(p - kMonths) / 3 : -1;
}

} // namespace

void timeBegin(const char* posix) {
    timeApplyZone(posix);
    Wire.begin(pins::kSda, pins::kScl);
    Wire.beginTransmission(kRtcAddress);
    rtcPresent = Wire.endTransmission() == 0;
    time_t t;
    if (rtcPresent && rtcRead(t)) {
        struct timeval tv = {t, 0};
        settimeofday(&tv, nullptr);
        source = pk::TimeSource::Rtc;
    }
    Serial.printf("[time] RTC %s, %s\n", rtcPresent ? "found" : "missing",
                  source == pk::TimeSource::Rtc ? "time loaded" : "no time yet");
}

void timeApplyZone(const char* posix) {
    strncpy(zone, posix && posix[0] ? posix : "UTC0", sizeof(zone) - 1);
    zone[sizeof(zone) - 1] = '\0';
    setenv("TZ", zone, 1);
    tzset();
}

void timeOnline() {
    if (sntpStarted) return;
    sntpStarted = true;
    sntp_set_time_sync_notification_cb(onSntp);
    configTzTime(zone, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

void timeFromHttpDate(const char* value) {
    if (ntpArrived || !value) return;
    // "Sun, 06 Nov 1994 08:49:37 GMT"
    char wd[4], mon[4];
    int day, year, h, m, s;
    if (sscanf(value, "%3s, %d %3s %d %d:%d:%d", wd, &day, mon, &year, &h, &m, &s) != 7) return;
    const int mi = monthIndex(mon);
    if (mi < 0 || year < 2023) return;
    if (day < 1 || day > 31 || h > 23 || m > 59 || s > 60) return;
    const time_t utc = utcFrom(year, mi + 1, day, h, m, s);
    const time_t now = time(nullptr);
    if (utc < kSane) return;
    if (source != pk::TimeSource::Http || llabs((long long)(utc - now)) > 2) {
        struct timeval tv = {utc, 0};
        settimeofday(&tv, nullptr);
        if (source != pk::TimeSource::Http) rtcWrite(utc);
        source = pk::TimeSource::Http;
    }
}

void timeLocal(pk::LocalTime& out, pk::TimeSource& src) {
    out = pk::LocalTime{};
    src = source;
    const time_t now = time(nullptr);
    if (now < kSane) {
        src = pk::TimeSource::None;
        return;
    }
    struct tm t;
    localtime_r(&now, &t);
    out = pk::LocalTime{true, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, (t.tm_wday + 6) % 7};
}

uint32_t timeUnix() {
    const time_t now = time(nullptr);
    return now >= kSane ? (uint32_t)now : 0;
}

} // namespace hal
