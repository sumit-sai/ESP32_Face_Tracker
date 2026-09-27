#pragma once
#include <Arduino.h>
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "motion.h"

constexpr int PAN_PIN = 14, TILT_PIN = 21;
QueueHandle_t observationQueue;
portMUX_TYPE motionMux = portMUX_INITIALIZER_UNLOCKED;
bool requestedTracking = false, requestedSearch = true;
struct MotionSnapshot {float pan, tilt; Motion::State state;};
MotionSnapshot motionSnapshot = {90, 90, Motion::State::Waiting};

MotionSnapshot getMotion() {
  portENTER_CRITICAL(&motionMux);
  MotionSnapshot result = motionSnapshot;
  portEXIT_CRITICAL(&motionMux);
  return result;
}
void setTrackingEnabled(bool enabled) {
  portENTER_CRITICAL(&motionMux); requestedTracking = enabled; portEXIT_CRITICAL(&motionMux);
}
void setSearchEnabled(bool enabled) {
  portENTER_CRITICAL(&motionMux); requestedSearch = enabled; portEXIT_CRITICAL(&motionMux);
}
uint32_t servoDuty(float angle) {
  return (uint32_t)((1000.0f + angle * (1000.0f/180.0f))*16384.0f/20000.0f + 0.5f);
}
void writeServo(ledc_channel_t channel, float angle) {
  ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, servoDuty(angle)));
  ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, channel));
}
void motionTask(void *) {
  Motion::Controller controller;
  controller.begin(millis());
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    bool enabled, search;
    portENTER_CRITICAL(&motionMux);
    enabled = requestedTracking; search = requestedSearch;
    portEXIT_CRITICAL(&motionMux);
    const uint32_t now = millis();
    controller.setEnabled(enabled, now); controller.setSearch(search);
    Motion::Observation input;
    if (xQueueReceive(observationQueue, &input, 0) == pdTRUE) controller.observe(input, now);
    controller.tick(now);
    writeServo(LEDC_CHANNEL_2, controller.pan);
    writeServo(LEDC_CHANNEL_3, controller.tilt);
    portENTER_CRITICAL(&motionMux);
    motionSnapshot = {controller.pan, controller.tilt, controller.state};
    portEXIT_CRITICAL(&motionMux);
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(20));
  }
}
void submitObservation(bool valid, bool found, float dx, float dy, uint32_t capturedMs) {
  Motion::Observation input;
  input.valid=valid; input.found=found; input.dx=dx; input.dy=dy; input.capturedMs=capturedMs;
  xQueueOverwrite(observationQueue, &input);
}
void setupTracking() {
  // Camera: timer 0/channel 0. Servos: timer 1/channels 2,3.
  ledc_timer_config_t timer = {};
  timer.speed_mode=LEDC_LOW_SPEED_MODE; timer.duty_resolution=LEDC_TIMER_14_BIT;
  timer.timer_num=LEDC_TIMER_1; timer.freq_hz=50; timer.clk_cfg=LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&timer));
  const int pins[] = {PAN_PIN,TILT_PIN};
  const ledc_channel_t channels[] = {LEDC_CHANNEL_2,LEDC_CHANNEL_3};
  for(int i=0;i<2;++i) {
    ledc_channel_config_t config = {};
    config.gpio_num=pins[i]; config.speed_mode=LEDC_LOW_SPEED_MODE;
    config.channel=channels[i]; config.timer_sel=LEDC_TIMER_1; config.duty=servoDuty(90);
    ESP_ERROR_CHECK(ledc_channel_config(&config));
  }
  observationQueue = xQueueCreate(1, sizeof(Motion::Observation));
  if (!observationQueue) {Serial.println("Motion queue allocation failed"); while(true) delay(1000);}
  delay(500);
  if (xTaskCreatePinnedToCore(motionTask,"servo-motion",4096,nullptr,2,nullptr,0) != pdPASS) {
    Serial.println("Motion task creation failed"); while(true) delay(1000);
  }
}

