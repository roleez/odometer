// displaytft.h
#ifndef DISPLAYTFT_H
#define DISPLAYTFT_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <atomic>
#define TOUCH_CS 33
#include "TFT_eSPI.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <Arduino.h>
#include <driver/rtc_io.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

typedef struct {
  double speedKmh;
  double dailyDistanceKm;
  double totalDistanceKm;
  double instantaneousSpeedKmh;   // <-- új mező
  uint32_t movingTimeSeconds;     // <-- új mező: mozgásban eltöltött idő másodpercekben
} SensorData_t;

// Enumeráció a kijelzett adatok típusához
typedef enum {
  DISPLAY_SPEED,
  DISPLAY_DAILY_DISTANCE,
  DISPLAY_TOTAL_DISTANCE,
  DISPLAY_MAX_SPEED,    // Maximális sebesség
  DISPLAY_AVERAGE_SPEED, // Átlagsebesség
  DISPLAY_MOVEMENT_TIME, // Új: tényleges mozgási idő
  DISPLAY_STATE_COUNT   // Az állapotok száma a ciklikus váltáshoz
} DisplayState_t;

extern TFT_eSPI lcd;
extern TFT_eSprite sprite;
extern uint16_t bootCount;
extern const char *TAG; // Ha a displaytft.cpp-ben is szükséged van rá
// Globális adatok és mutex extern deklarációja
extern SensorData_t sharedSensorData;
extern SemaphoreHandle_t xDataMutex;

// Új: Megosztott kijelző állapot a reset task számára
extern DisplayState_t sharedDisplayState;
extern SemaphoreHandle_t xDisplayStateMutex;

// Új: Globális sebesség változók extern deklarációi
extern double maxSpeedKmh;
extern double averageSpeedKmh;
extern double startTotalDistanceKm;
extern int64_t totalMovingTimeUs;

void guiTask(void *pvParameters); // Csak a deklaráció

#endif
