#include <Arduino.h>
#include "config.h"

// =================================================================
// RUNTIME STATE
// =================================================================
bool lightState[NUM_LIGHTS]      = {false, false, false, false};
bool manualOverride[NUM_LIGHTS]  = {false, false, false, false};

// For change-detection (only print when something actually changes)
bool prevOverride[NUM_LIGHTS]    = {false, false, false, false};
bool prevLightState[NUM_LIGHTS]  = {false, false, false, false};
bool prevMotionDetected          = false;

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
  Serial.println("[INFO] Switch LOW = relay ON, PIR ignored.");
  Serial.println("[INFO] Switch HIGH = PIR automatic control.");

  // PIR pins — input only, no pullup needed (PIR drives them actively)
  pinMode(PIR_PIN_1, INPUT);
  pinMode(PIR_PIN_2, INPUT);

  // Relay outputs: default to OFF before setting as output (prevents boot click)
  for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
    digitalWrite(RELAY_PINS[i], RELAY_OFF);
    pinMode(RELAY_PINS[i], OUTPUT);

    // Switch pins: internal pullup — HIGH when released, LOW when shorted to GND
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
  // 1. READ SWITCH STATES (level-triggered)
  //    LOW  → switch is held to GND → manual override ON for this channel
  //    HIGH → switch is floating    → PIR automatic control for this channel
  // ---------------------------------------------------------------
  for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
    manualOverride[i] = (digitalRead(SWITCH_PINS[i]) == LOW);

    // Log transition: switch just pulled to GND
    if (manualOverride[i] && !prevOverride[i]) {
      Serial.printf("[SWITCH %u] Pulled LOW → Relay ON, PIR disabled for ch%u.\n", i + 1, i + 1);
    }
    // Log transition: switch just released
    if (!manualOverride[i] && prevOverride[i]) {
      Serial.printf("[SWITCH %u] Released → PIR automation resumed for ch%u.\n", i + 1, i + 1);
      // Reset motion clock so the channel doesn't immediately time-out
      lastMotionTime = currentTime;
    }
    prevOverride[i] = manualOverride[i];
  }

  // ---------------------------------------------------------------
  // 2. APPLY MANUAL OVERRIDES
  //    Channels with switch LOW are forced ON and immune to PIR.
  // ---------------------------------------------------------------
  for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
    if (manualOverride[i] && !lightState[i]) {
      lightState[i] = true;
      writeRelay(i, true);
    }
    // When switch is released, PIR logic (below) takes over — do not force OFF here
    // (PIR timeout will handle turning it off naturally)
  }

  // ---------------------------------------------------------------
  // 3. PIR AUTOMATION (applies only to channels NOT held by a switch)
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

    // Turn ON every channel that is NOT manually overridden and is currently OFF
    for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
      if (!manualOverride[i] && !lightState[i]) {
        lightState[i] = true;
        writeRelay(i, true);
        Serial.printf("[AUTO] Channel %u ON by motion.\n", i + 1);
      }
    }
  } else {
    // No motion: turn OFF non-overridden channels after timeout
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

  // ---------------------------------------------------------------
  // 4. SYNC STATE: if switch released while light was ON (from PIR),
  //    update lightState to match actual relay so timeout logic is correct.
  // ---------------------------------------------------------------
  for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
    if (lightState[i] != prevLightState[i]) {
      prevLightState[i] = lightState[i];
    }
  }

  delay(LOOP_POLL_INTERVAL_MS);
}