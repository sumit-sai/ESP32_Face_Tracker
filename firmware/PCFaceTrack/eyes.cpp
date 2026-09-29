#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "motion.h"
#include <FluxGarage_RoboEyes.h>
#include "eyes.h"

// Wire uses I2C controller 0; the installed camera driver uses controller 1.
// Keep RoboEyes in this file so its short direction macros cannot affect tracking.
static Adafruit_SSD1306 display(128, 64, &Wire, -1);
static RoboEyes<Adafruit_SSD1306> eyes(display);
static bool eyesReady = false;
static constexpr uint32_t CENTER_DELAY_MS = 300;
static constexpr uint32_t SAD_DELAY_MS = 1000;
static constexpr float MOVEMENT_THRESHOLD_DEG = 0.05f;
// Physical test: reverse the display's pan mapping; tilt already matches.
// These signs affect only eye direction, not the servo controller.
static constexpr int PAN_TO_SCREEN = -Motion::PAN_DIRECTION;
static constexpr int TILT_TO_SCREEN = Motion::TILT_DIRECTION;
static bool havePosition = false, haveSeenFace = false;
static float previousPan = 90, previousTilt = 90;
static int horizontal = 0, vertical = 0;
static uint32_t lastPanMoveMs = 0, lastTiltMoveMs = 0, lastFaceMs = 0;
static int currentMood = DEFAULT, currentPosition = DEFAULT;

void observeEyesFace(bool found, uint32_t capturedMs) {
  // Called only for accepted observations on the main loop, never the servo task.
  // Use capture time so a stalled tracker cannot keep the eyes happy indefinitely.
  if (found) {
    haveSeenFace = true;
    lastFaceMs = capturedMs;
  }
}

void setupEyes() {
  if (!Wire.begin(1, 2)) {
    Serial.println("OLED I2C initialization failed; robot continues without eyes");
    return;
  }
  Wire.setTimeOut(20);
  Wire.beginTransmission(0x3C);
  if (Wire.endTransmission() != 0) {
    Serial.println("OLED not detected at 0x3C; robot continues without eyes");
    return;
  }
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false)) {
    Serial.println("OLED allocation failed; robot continues without eyes");
    return;
  }
  eyes.begin(128, 64, 30);
  eyes.setMood(DEFAULT);
  eyes.setPosition(DEFAULT);
  eyes.setAutoblinker(ON, 3, 2);
  eyes.setIdleMode(OFF);
  eyesReady = true;
  Serial.println("OLED ready: automatic gaze and face expressions");
}

void updateEyes(float pan, float tilt) {
  if (!eyesReady) return;
  const uint32_t now = millis();
  if (!havePosition) {
    previousPan = pan;
    previousTilt = tilt;
    havePosition = true;
  }
  const float panChange = pan - previousPan;
  const float tiltChange = tilt - previousTilt;
  if (fabsf(panChange) >= MOVEMENT_THRESHOLD_DEG) {
    horizontal = panChange * PAN_TO_SCREEN > 0 ? 1 : -1;
    previousPan = pan;
    lastPanMoveMs = now;
  }
  if (fabsf(tiltChange) >= MOVEMENT_THRESHOLD_DEG) {
    vertical = tiltChange * TILT_TO_SCREEN > 0 ? 1 : -1;
    previousTilt = tilt;
    lastTiltMoveMs = now;
  }
  if (now - lastPanMoveMs >= CENTER_DELAY_MS) horizontal = 0;
  if (now - lastTiltMoveMs >= CENTER_DELAY_MS) vertical = 0;

  int position = DEFAULT;
  if (vertical < 0) position = horizontal < 0 ? NW : (horizontal > 0 ? NE : N);
  else if (vertical > 0) position = horizontal < 0 ? SW : (horizontal > 0 ? SE : S);
  else if (horizontal != 0) position = horizontal < 0 ? W : E;
  if (position != currentPosition) {
    eyes.setPosition(position);
    currentPosition = position;
  }

  // Neutral before the first face; tolerate short gaps, then use TIRED as sad.
  int mood = DEFAULT;
  if (haveSeenFace) {
    mood = now - lastFaceMs < SAD_DELAY_MS ? HAPPY : TIRED;
  }
  if (mood != currentMood) {
    eyes.setMood(mood);
    currentMood = mood;
  }
  eyes.update();
}
