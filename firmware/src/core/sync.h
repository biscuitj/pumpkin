// One mutex for the shared App state. The web server (AsyncTCP task), the
// network task and the main loop all take it; nothing slow happens while it's held.
#pragma once

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace pk {
class Mutex {
public:
    Mutex() : h_(xSemaphoreCreateRecursiveMutex()) {}
    void lock() { xSemaphoreTakeRecursive(h_, portMAX_DELAY); }
    void unlock() { xSemaphoreGiveRecursive(h_); }

private:
    SemaphoreHandle_t h_;
};
} // namespace pk
#else
#include <mutex>

namespace pk {
class Mutex {
public:
    void lock() { m_.lock(); }
    void unlock() { m_.unlock(); }

private:
    std::recursive_mutex m_;
};
} // namespace pk
#endif

namespace pk {
class Locked {
public:
    explicit Locked(Mutex& m) : m_(m) { m_.lock(); }
    ~Locked() { m_.unlock(); }
    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;

private:
    Mutex& m_;
};
} // namespace pk
