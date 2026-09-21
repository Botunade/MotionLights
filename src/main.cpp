#include <Arduino.h>
#include "config.h"

// =================================================================
// RUNTIME STATE
// =================================================================
bool lightState[NUM_LIGHTS]     = {false, false, false, false};
bool manualOverride[NUM_LIGHTS] = {false, false, false, false};

// Edge-detection: only print when state actually changes
bool prevOverride[NUM_LIGHTS]   = {false, false, false, false};
bool prevMotionConfirmed        = false;

uint32_t lastMotionTime = 0;

// PIR false-trigger filter:
// Motion is only accepted after PIR_CONFIRM_TICKS consecutive HIGH reads.
// Motion is only cleared after PIR_CONFIRM_TICKS consecutive LOW reads.
// This rejects brief noise spikes on either edge.
uint8_t pirHighCount = 0;  // Consecutive HIGH ticks
uint8_t pirLowCount  = 0;  // Consecutive LOW ticks
bool    motionConfirmed = false;

// =================================================================
// HARDWARE HELPERS
// =================================================================

inline void writeRelay(uint8_t index, bool turnOn) {
  digitalWrite(RELAY_PINS[index], turnOn ? RELAY_ON : RELAY_OFF);
}

/**
 * @brief Reads both PIR sensors with a confirmation filter.
 *
 * Returns true only after PIR_CONFIRM_TICKS consecutive HIGH readings.
 * Returns false only after PIR_CONFIRM_TICKS consecutive LOW readings.
 * Intermediate noisy samples are ignored — confirmed state is held.
 *
 * GPIO 32 & 33 use INPUT_PULLDOWN so disconnected sensors read firmly LOW,
 * preventing floating-pin false triggers.
 */
bool readMotionFiltered() {
  bool rawHigh = (digitalRead(PIR_PIN_1) == HIGH) || (digitalRead(PIR_PIN_2) == HIGH);

  if (rawHigh) {
    if (pirHighCount < PIR_CONFIRM_TICKS) pirHighCount++;
    pirLowCount = 0;
  } else {
    if (pirLowCount < PIR_CONFIRM_TICKS) pirLowCount++;
    pirHighCount = 0;
  }

  // Latch ON only after enough consecutive HIGHs
  if (pirHighCount >= PIR_CONFIRM_TICKS) {
    motionConfirmed = true;
  }
  // Latch OFF only after enough consecutive LOWs
  if (pirLowCount >= PIR_CONFIRM_TICKS) {
    motionConfirmed = false;
  }

  return motionConfirmed;
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
  Serial.printf("[INFO] PIR filter: %u ticks × %lums = %lums confirmation window.\n",
                PIR_CONFIRM_TICKS, LOOP_POLL_INTERVAL_MS,
                (uint32_t)PIR_CONFIRM_TICKS * LOOP_POLL_INTERVAL_MS);

  // PIR pins: INPUT_PULLDOWN holds pin at 0V when sensor is disconnected.
  // This prevents floating-pin noise from causing false motion triggers.
  pinMode(PIR_PIN_1, INPUT_PULLDOWN);
  pinMode(PIR_PIN_2, INPUT_PULLDOWN);

  // Relay outputs: write OFF before setting as OUTPUT to prevent boot click
  for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
    digitalWrite(RELAY_PINS[i], RELAY_OFF);
    pinMode(RELAY_PINS[i], OUTPUT);
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
  //    LOW  = switch held to GND → FORCE relay OFF, PIR blocked
  //    HIGH = switch released    → PIR controls this channel
  // ---------------------------------------------------------------
  for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
    manualOverride[i] = (digitalRead(SWITCH_PINS[i]) == LOW);

    if (manualOverride[i] && !prevOverride[i]) {
      // Switch just pulled LOW: kill relay immediately
      lightState[i] = false;
      writeRelay(i, false);
      Serial.printf("[SWITCH %u] Pulled LOW → Relay FORCED OFF, PIR disabled for ch%u.\n", i + 1, i + 1);
    }
    if (!manualOverride[i] && prevOverride[i]) {
      // Switch just released: hand back to PIR
      lastMotionTime = currentTime; // Reset clock so timeout doesn't fire instantly
      Serial.printf("[SWITCH %u] Released → PIR automation resumed for ch%u.\n", i + 1, i + 1);
    }
    prevOverride[i] = manualOverride[i];
  }

  // ---------------------------------------------------------------
  // 2. ENFORCE OVERRIDES CONTINUOUSLY
  //    Catch edge case: PIR fired in same tick as switch was pulled LOW.
  // ---------------------------------------------------------------
  for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
    if (manualOverride[i] && lightState[i]) {
      lightState[i] = false;
      writeRelay(i, false);
    }
  }

  // ---------------------------------------------------------------
  // 3. READ PIR WITH CONFIRMATION FILTER
  //    Requires PIR_CONFIRM_TICKS consecutive HIGH reads before accepting.
  //    Requires PIR_CONFIRM_TICKS consecutive LOW reads before clearing.
  //    Disconnected sensor held LOW by INPUT_PULLDOWN — no false triggers.
  // ---------------------------------------------------------------
  const bool motionNow = readMotionFiltered();

  if (motionNow && !prevMotionConfirmed) {
    Serial.println("[PIR] Motion confirmed — activating auto channels.");
  }
  if (!motionNow && prevMotionConfirmed) {
    Serial.println("[PIR] Motion cleared — inactivity timer started.");
  }
  prevMotionConfirmed = motionNow;

  // ---------------------------------------------------------------
  // 4. PIR AUTOMATION (only for channels whose switch is released)
  // ---------------------------------------------------------------
  if (motionNow) {
    lastMotionTime = currentTime;

    for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
      if (!manualOverride[i] && !lightState[i]) {
        lightState[i] = true;
        writeRelay(i, true);
        Serial.printf("[AUTO] Channel %u ON by motion.\n", i + 1);
      }
    }
  } else {
    // No confirmed motion: turn off non-overridden channels after timeout
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