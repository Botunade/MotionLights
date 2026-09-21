#include <Arduino.h>
#include "config.h"

// =================================================================
// RUNTIME STATE
// =================================================================
bool lightState[NUM_LIGHTS]     = {false, false, false, false};
bool manualOverride[NUM_LIGHTS] = {false, false, false, false};

// Edge-detection: only print when state actually changes
bool prevOverride[NUM_LIGHTS]   = {false, false, false, false};
bool prevMotionDetected         = false;

uint32_t lastMotionTime = 0;

// =================================================================
// HARDWARE HELPERS
// =================================================================

inline void writeRelay(uint8_t index, bool turnOn) {
  digitalWrite(RELAY_PINS[index], turnOn ? RELAY_ON : RELAY_OFF);
}

inline bool isMotionDetected() {
  return (digitalRead(PIR_PIN_1) == HIGH) || (digitalRead(PIR_PIN_2) == HIGH);
}

// =================================================================
// SETUP
// =================================================================

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  delay(200);
  Serial.println("\n==============================================");
  Serial.println("   ESP32 MotionLights Controller             ");
  Serial.println("==============================================");
  Serial.println("[INFO] Switch LOW  = relay FORCED OFF, PIR disabled for that channel.");
  Serial.println("[INFO] Switch HIGH = PIR automatic control for that channel.");

  // PIR sensor pins (driven actively by sensor, no internal pull needed)
  pinMode(PIR_PIN_1, INPUT);
  pinMode(PIR_PIN_2, INPUT);

  // Relay outputs: write OFF state before setting as OUTPUT to prevent boot click
  for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
    digitalWrite(RELAY_PINS[i], RELAY_OFF);
    pinMode(RELAY_PINS[i], OUTPUT);

    // Switch pins: internal pull-up — HIGH when released, LOW when shorted to GND
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);
  }

  Serial.println("[SYSTEM] Ready.");
}

// =================================================================
// MAIN LOOP
// =================================================================

void loop() {
  const uint32_t currentTime = millis();

  // ---------------------------------------------------------------
  // 1. READ SWITCH STATES (level-triggered, read every 50ms)
  //    LOW  = switch held to GND → FORCE relay OFF, PIR blocked
  //    HIGH = switch released    → PIR controls this channel
  // ---------------------------------------------------------------
  for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
    manualOverride[i] = (digitalRead(SWITCH_PINS[i]) == LOW);

    if (manualOverride[i] && !prevOverride[i]) {
      // Switch just pulled LOW: kill relay immediately, block PIR
      lightState[i] = false;
      writeRelay(i, false);
      Serial.printf("[SWITCH %u] Pulled LOW → Relay FORCED OFF, PIR disabled for ch%u.\n", i + 1, i + 1);
    }

    if (!manualOverride[i] && prevOverride[i]) {
      // Switch just released: hand back to PIR automation
      // Reset motion clock so timeout doesn't immediately fire
      lastMotionTime = currentTime;
      Serial.printf("[SWITCH %u] Released → PIR automation resumed for ch%u.\n", i + 1, i + 1);
    }

    prevOverride[i] = manualOverride[i];
  }

  // ---------------------------------------------------------------
  // 2. ENFORCE OVERRIDES CONTINUOUSLY
  //    Any channel with switch held LOW stays OFF regardless of PIR.
  // ---------------------------------------------------------------
  for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
    if (manualOverride[i] && lightState[i]) {
      // Catch edge case where PIR fired before we processed the switch
      lightState[i] = false;
      writeRelay(i, false);
    }
  }

  // ---------------------------------------------------------------
  // 3. PIR AUTOMATION (only for channels whose switch is released)
  // ---------------------------------------------------------------
  const bool motionNow = isMotionDetected();

  if (motionNow && !prevMotionDetected) {
    Serial.println("[PIR] Motion detected — activating auto channels.");
  }
  if (!motionNow && prevMotionDetected) {
    Serial.println("[PIR] Motion cleared — inactivity timer started.");
  }
  prevMotionDetected = motionNow;

  if (motionNow) {
    lastMotionTime = currentTime;

    for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
      // Only act on channels NOT held by a switch
      if (!manualOverride[i] && !lightState[i]) {
        lightState[i] = true;
        writeRelay(i, true);
        Serial.printf("[AUTO] Channel %u ON by motion.\n", i + 1);
      }
    }
  } else {
    // No motion: after timeout, turn off non-overridden channels
    if ((currentTime - lastMotionTime) > MOTION_TIMEOUT_MS) {
      for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
        if (!manualOverride[i] && lightState[i]) {
          lightState[i] = false;
          writeRelay(i, false);
          Serial.printf("[AUTO] Channel %u OFF — inactivity timeout.\n", i + 1);
        }
      }
    }
  }

  delay(LOOP_POLL_INTERVAL_MS);
}