#include "inputs.h"

#include <Arduino.h>

#include "pins.h"

namespace hal {

namespace {

constexpr uint32_t kDebounceMs = 40;
constexpr uint32_t kLongMs = 1000;
constexpr uint32_t kLightEveryMs = 250;
constexpr int kLightSamples = 16;

struct Key {
    int pin;
    bool raw = false;       // true = down
    bool stable = false;
    uint32_t changedAt = 0;
    uint32_t downAt = 0;
    bool longFired = false;
};

Key keys[3] = {{pins::kLeft}, {pins::kMiddle}, {pins::kRight}};

pk::Button queue[4];
volatile int qHead = 0, qTail = 0;

uint16_t light[kLightSamples];
int lightIndex = 0;
uint32_t lightAt = 0;
volatile uint16_t lightSmooth = 2048;

void push(pk::Button b) {
    const int next = (qTail + 1) % 4;
    if (next == qHead) return;  // full: drop
    queue[qTail] = b;
    qTail = next;
}

} // namespace

void inputsBegin() {
    for (Key& k : keys) pinMode(k.pin, INPUT_PULLUP);
    analogReadResolution(12);
    analogSetPinAttenuation(pins::kLight, ADC_11db);
    const uint16_t first = analogRead(pins::kLight);
    for (uint16_t& v : light) v = first;
    lightSmooth = first;
}

void inputsLoop() {
    const uint32_t now = millis();
    for (int i = 0; i < 3; ++i) {
        Key& k = keys[i];
        const bool down = digitalRead(k.pin) == LOW;
        if (down != k.raw) {
            k.raw = down;
            k.changedAt = now;
        }
        if (now - k.changedAt < kDebounceMs || down == k.stable) {
            // Held: the middle button's long press fires while still down.
            if (i == 1 && k.stable && !k.longFired && now - k.downAt >= kLongMs) {
                k.longFired = true;
                push(pk::Button::MiddleLong);
            }
            continue;
        }
        k.stable = down;
        if (down) {
            k.downAt = now;
            k.longFired = false;
            // Left and right act on press, so stepping through screens feels quick.
            if (i == 0) push(pk::Button::Left);
            if (i == 2) push(pk::Button::Right);
        } else if (i == 1 && !k.longFired) {
            push(pk::Button::Middle);
        }
    }

    if (now - lightAt >= kLightEveryMs) {
        lightAt = now;
        light[lightIndex] = analogRead(pins::kLight);
        lightIndex = (lightIndex + 1) % kLightSamples;
        uint32_t sum = 0;
        for (uint16_t v : light) sum += v;
        lightSmooth = (uint16_t)(sum / kLightSamples);
    }
}

bool nextButton(pk::Button& out) {
    if (qHead == qTail) return false;
    out = queue[qHead];
    qHead = (qHead + 1) % 4;
    return true;
}

void buttonStates(bool& left, bool& middle, bool& right) {
    left = keys[0].stable;
    middle = keys[1].stable;
    right = keys[2].stable;
}

uint16_t lightLevel() { return lightSmooth; }

} // namespace hal
