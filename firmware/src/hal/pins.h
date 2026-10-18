// Ulanzi TC001 (ESP32-WROOM-32D) wiring.
#pragma once

namespace pins {
constexpr int kMatrix = 32;   // WS2812B data, 8x32, row-major serpentine from the top left
constexpr int kLeft = 26;     // buttons: active low, internal pull-up
constexpr int kMiddle = 27;
constexpr int kRight = 14;
constexpr int kBuzzer = 15;   // driven low and left alone: Pumpkin is silent
constexpr int kLight = 35;    // light sensor (ADC)
constexpr int kSda = 21;      // DS1307 real-time clock
constexpr int kScl = 22;
} // namespace pins
