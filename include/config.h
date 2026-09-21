#pragma once

#include <Arduino.h>

// =================================================================
// SYSTEM & CHANNEL CONFIGURATION
// =================================================================
constexpr uint8_t NUM_LIGHTS = 4;

// =================================================================
// PIN ASSIGNMENTS
// =================================================================
// Manual Override Switches: INPUT_PULLUP.
// Each switch is wired between its pin and GND.
// LOW  = switch held down → relay ON, PIR ignored for this channel.
// HIGH = switch released  → channel under PIR automatic control.
constexpr uint8_t SWITCH_PINS[NUM_LIGHTS] = {13, 14, 26, 27};

// Relay outputs (Active-Low pre-built relay boards).
constexpr uint8_t RELAY_PINS[NUM_LIGHTS] = {16, 17, 18, 19};

// PIR Sensors: Input-Only pins (no internal pull-up/down).
// PIR output is HIGH when motion is detected, LOW otherwise.
constexpr uint8_t PIR_PIN_1 = 34;
constexpr uint8_t PIR_PIN_2 = 35;

// =================================================================
// ELECTRICAL POLARITY
// =================================================================
constexpr uint8_t RELAY_ON  = LOW;   // Active-Low relay ON
constexpr uint8_t RELAY_OFF = HIGH;  // Active-Low relay OFF

// =================================================================
// TIMING (milliseconds)
// =================================================================
constexpr uint32_t MOTION_TIMEOUT_MS    = 10000;  // 10s no-motion before auto-off
constexpr uint32_t LOOP_POLL_INTERVAL_MS = 50;    // Main loop tick

// Serial
constexpr uint32_t SERIAL_BAUD_RATE = 115200;