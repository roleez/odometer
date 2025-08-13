#include <stdio.h>
#include <string.h>
#include <math.h>
#include <atomic>
#include <WiFi.h>           // hozzáadva: WiFi Station
#include <ArduinoOTA.h>     // hozzáadva: OTA

#include "displaytft.h" // SensorData_t innen jön
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <Arduino.h>
#include <driver/rtc_io.h>
#include "config.h" // A konfigurációs beállítások innen jönnek
//static LGFX lcd;

// --- Globális Változók (szálbiztos) ---
std::atomic<uint64_t> pulseCount(0);        // Teljes impulzusszám (induláskor NVS-ből töltődik)
std::atomic<int64_t> lastPulseTimeUs(0);    // Utolsó REED impulzus ideje mikroszekundumban

// --- RTC Memória Változók ---
// Ezek megőrzik értéküket mélyalvás alatt, de teljes tápmegszakításkor elvesznek/meghatározatlanok
RTC_DATA_ATTR uint64_t dailyTripStartPulseCount = 0; // Az a pulseCount érték, ahonnan a napi út számítása indul
RTC_DATA_ATTR uint16_t bootCount;

// --- Globális változók (mutex-szel védett) ---
SensorData_t sharedSensorData = {0.0, 0.0, 0.0, 0.0, 0}; // Kezdeti értékek, hozzáadva movingTimeSeconds
SemaphoreHandle_t xDataMutex = NULL;       // Mutex a sharedSensorData védelmére

// Új: Megosztott kijelző állapot
DisplayState_t sharedDisplayState = DISPLAY_SPEED;
SemaphoreHandle_t xDisplayStateMutex = NULL;

// Új: Maximális sebesség és átlagsebesség változók (most globálisan elérhetők a reset task és GUI task számára)
double maxSpeedKmh = 0.0;  // Eltávolítottuk a static kulcsszót
double startTotalDistanceKm = 0.0;  // Eltávolítottuk a static kulcsszót
int64_t totalMovingTimeUs = 0;  // Eltávolítottuk a static kulcsszót
double averageSpeedKmh = 0.0;  // Eltávolítottuk a static kulcsszót
extern bool data_changed; // Ez a változó jelzi, hogy az adatok frissültek-e

// --- NVS Globálisok ---
#define NVS_NAMESPACE "storage"
#define NVS_KEY_TOTAL_PULSES "total_pulses"
#define NVS_KEY_DAILY_PULSES "daily_pulses"
#define NVS_KEY_MOVING_TIME "moving_time"  // Új: mozgási idő NVS kulcs
nvs_handle_t g_nvs_handle = 0; // NVS handle (globális, hogy ne kelljen mindenhol nyitni/zárni)

const char *TAG = "WheelSensorV3"; // Logoláshoz TAG

// --- Prototípusok ---
void go_to_deep_sleep(void);
esp_err_t init_nvs(void);
esp_err_t load_total_pulses_from_nvs(uint64_t *pulses);
esp_err_t save_total_pulses_to_nvs(uint64_t pulses);
esp_err_t load_moving_time_from_nvs(uint32_t *movingTimeSeconds); // Új
esp_err_t save_moving_time_to_nvs(uint32_t movingTimeSeconds);    // Új
void reset_button_monitor_task(void *pvParameters);
void reed_simulation_task(void *pvParameters);
void serial_output_task(void *pvParameters); // Hiányzó prototípus hozzáadása

volatile int64_t lastDebounceTimeUs = 0;
const int64_t debounceDelayUs = 10000; // 10 ms

// Új: impulzusesemény jelzésére counting semaphore
static SemaphoreHandle_t xPulseSemaphore = NULL;

// Új: WiFi-off timer
static TimerHandle_t wifiOffTimer = NULL;

// visszahívás 10 perc múlva
void wifiOffTimerCallback(TimerHandle_t xTimer) {
    ESP_LOGI(TAG, "Disabling WiFi to save power");
    //if (WiFi.isConnected()) {
        //WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    //}
}

// --- ISR (Interrupt Service Routine - REED) ---
void IRAM_ATTR gpio_isr_handler(void* arg) {
    int64_t now = esp_timer_get_time();
    if ((now - lastDebounceTimeUs) > debounceDelayUs) {
        lastDebounceTimeUs = now;
        lastPulseTimeUs.store(now, std::memory_order_relaxed);
        pulseCount.fetch_add(1, std::memory_order_relaxed);
        BaseType_t hptw = pdFALSE;
        xSemaphoreGiveFromISR(xPulseSemaphore, &hptw);
        if (hptw) portYIELD_FROM_ISR();
    }
}

void inactivity_monitor_task(void *pvParameters) {
  ESP_LOGI(TAG, "Inactivity monitor task started.");
  while (1) {
    // Várjunk egy ésszerű ideig, pl. 30 másodpercig
    vTaskDelay(pdMS_TO_TICKS(30000));

    int64_t currentTimeUs = esp_timer_get_time();
    int64_t last_pulse_time = lastPulseTimeUs.load(std::memory_order_relaxed);
    int64_t inactivity_duration_us = currentTimeUs - last_pulse_time;

    // Ha az utolsó impulzus óta eltelt idő nagyobb, mint a timeout
    if (inactivity_duration_us > INACTIVITY_TIMEOUT_US) {
      ESP_LOGI(TAG,
               "Inactivity detected for over %d minutes. Entering deep sleep.",
               INACTIVITY_TIMEOUT_S / 60);

      go_to_deep_sleep();
    }
  }
}

// --- NVS Funkciók ---
esp_err_t init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated or new version found, erasing and reinitializing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS initialized successfully.");
        // Globális NVS handle megnyitása itt, ha még nincs megnyitva
        if (g_nvs_handle == 0) { // Csak akkor nyissuk meg, ha még nincs
            esp_err_t open_err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &g_nvs_handle);
            if (open_err != ESP_OK) {
                ESP_LOGE(TAG, "Error (%s) opening NVS handle in init_nvs!", esp_err_to_name(open_err));
                // Itt lehetne jelezni a hibát, pl. egy globális flaggel, hogy az NVS nem használható
            } else {
                ESP_LOGI(TAG, "NVS handle opened successfully in init_nvs.");
            }
        }
    }
    return ret;
}

esp_err_t load_total_pulses_from_nvs(uint64_t *pulses) {
    if (g_nvs_handle == 0) {
        ESP_LOGE(TAG, "NVS handle not open in load_total_pulses_from_nvs. Attempting to open...");
        // Próbáljuk meg itt is megnyitni, ha valamiért nem sikerült az init_nvs-ben, vagy az init_nvs nem nyitotta meg
        esp_err_t open_err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &g_nvs_handle);
        if (open_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open NVS handle in load: %s. Returning error.", esp_err_to_name(open_err));
            *pulses = 0; // Hiba esetén 0-val térünk vissza, mintha nem lenne mentett érték
            return open_err; // Visszatérünk a hibakóddal
        }
    }

    esp_err_t err = nvs_get_u64(g_nvs_handle, NVS_KEY_TOTAL_PULSES, pulses);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "NVS key '%s' not found. Initializing with 0.", NVS_KEY_TOTAL_PULSES);
        *pulses = 0;
        // És el is mentjük a nullát, hogy legközelebb meglegyen
        esp_err_t save_err = nvs_set_u64(g_nvs_handle, NVS_KEY_TOTAL_PULSES, *pulses);
         if (save_err != ESP_OK) {
             ESP_LOGE(TAG, "Failed to save initial NVS value: %s", esp_err_to_name(save_err));
         } else {
            save_err = nvs_commit(g_nvs_handle);
            if (save_err != ESP_OK) ESP_LOGE(TAG, "Failed to commit initial NVS value: %s", esp_err_to_name(save_err));
         }
        err = ESP_OK; // Visszaadjuk, hogy a betöltés sikeres volt (0-val)
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) reading NVS key '%s'! Defaulting to 0.", esp_err_to_name(err), NVS_KEY_TOTAL_PULSES);
        *pulses = 0; // Hiba esetén is 0-val térjünk vissza
    } else {
        ESP_LOGI(TAG, "Loaded total pulses from NVS: %llu", *pulses);
    }
    // A handle-t nyitva hagyjuk
    return err;
}

esp_err_t save_total_pulses_to_nvs(uint64_t pulses) {
    if (g_nvs_handle == 0) {
        ESP_LOGE(TAG, "NVS handle not open in save_total_pulses_to_nvs. Cannot save.");
        // Ideális esetben az init_nvs már megnyitotta. Ha nem, itt hiba van.
        // Lehetne próbálkozni a nyitással, de ha az initben nem sikerült, itt sem fog valószínűleg.
        return ESP_FAIL; // Vagy egy specifikusabb hibakód
    }

    esp_err_t err = nvs_set_u64(g_nvs_handle, NVS_KEY_TOTAL_PULSES, pulses);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) writing NVS key '%s'!", esp_err_to_name(err), NVS_KEY_TOTAL_PULSES);
        return err;
    }
    err = nvs_commit(g_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) committing NVS changes!", esp_err_to_name(err));
    }
    return err;
}

// --- Mozgási idő NVS Funkciók ---
esp_err_t load_moving_time_from_nvs(uint32_t *movingTimeSeconds) {
    if (g_nvs_handle == 0) {
        ESP_LOGE(TAG, "NVS handle not open in load_moving_time_from_nvs.");
        *movingTimeSeconds = 0;
        return ESP_FAIL;
    }

    esp_err_t err = nvs_get_u32(g_nvs_handle, NVS_KEY_MOVING_TIME, movingTimeSeconds);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "NVS key '%s' not found. Initializing with 0.", NVS_KEY_MOVING_TIME);
        *movingTimeSeconds = 0;
        // És el is mentjük a nullát
        esp_err_t save_err = nvs_set_u32(g_nvs_handle, NVS_KEY_MOVING_TIME, *movingTimeSeconds);
        if (save_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save initial moving time NVS value: %s", esp_err_to_name(save_err));
        } else {
            save_err = nvs_commit(g_nvs_handle);
            if (save_err != ESP_OK) ESP_LOGE(TAG, "Failed to commit initial moving time NVS value: %s", esp_err_to_name(save_err));
        }
        err = ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) reading NVS key '%s'! Defaulting to 0.", esp_err_to_name(err), NVS_KEY_MOVING_TIME);
        *movingTimeSeconds = 0;
    } else {
        ESP_LOGI(TAG, "Loaded moving time from NVS: %lu seconds (%.1f minutes)", 
                 (unsigned long)*movingTimeSeconds, *movingTimeSeconds/60.0);
    }
    return err;
}

esp_err_t save_moving_time_to_nvs(uint32_t movingTimeSeconds) {
    if (g_nvs_handle == 0) {
        ESP_LOGE(TAG, "NVS handle not open in save_moving_time_to_nvs. Cannot save.");
        return ESP_FAIL;
    }

    esp_err_t err = nvs_set_u32(g_nvs_handle, NVS_KEY_MOVING_TIME, movingTimeSeconds);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) writing NVS key '%s'!", esp_err_to_name(err), NVS_KEY_MOVING_TIME);
        return err;
    }
    err = nvs_commit(g_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) committing moving time NVS changes!", esp_err_to_name(err));
    }
    return err;
}

// --- NVS Mentő Task ---
void nvs_save_task(void *pvParameters) {
    ESP_LOGI(TAG, "NVS save task started. Saving every %d minutes.", NVS_SAVE_INTERVAL_MS / (60 * 1000));
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(NVS_SAVE_INTERVAL_MS));

        uint64_t currentTotalPulses = pulseCount.load(std::memory_order_relaxed);
        esp_err_t err = save_total_pulses_to_nvs(currentTotalPulses);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Total pulses (%llu) saved to NVS.", currentTotalPulses);
        } else {
            ESP_LOGE(TAG, "Failed to save total pulses to NVS!");
        }

        // Mozgási idő mentése is
        uint32_t currentMovingTime = 0;
        if (xDataMutex != NULL && xSemaphoreTake(xDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            currentMovingTime = sharedSensorData.movingTimeSeconds;
            xSemaphoreGive(xDataMutex);
            
            esp_err_t moving_err = save_moving_time_to_nvs(currentMovingTime);
            if (moving_err == ESP_OK) {
                ESP_LOGI(TAG, "Moving time (%lu sec = %.1f min) saved to NVS.", 
                         (unsigned long)currentMovingTime, currentMovingTime/60.0);
            } else {
                ESP_LOGE(TAG, "Failed to save moving time to NVS!");
            }
        } else {
            ESP_LOGW(TAG, "NVS save task couldn't get mutex for moving time!");
        }
    }
}

// --- Sebesség/Távolság Számoló Task (pulse driven) ---
#define SPEED_TIMEOUT_MS 5000  // 5 mp inaktivitás után 0 km/h

void calculation_and_control_task(void *pvParameters) {
    ESP_LOGI(TAG, "Calc task (pulse-driven) started.");
    const double wheelCircM = M_PI * WHEEL_DIAMETER_M;
    int64_t prevPulseUs = 0;
    double curSpeed = 0.0;
    static double moveAccum = 0.0; // <-- Új: mozgásidő felhalmozó

    uint64_t last_saved_total_pulses =
        pulseCount.load(std::memory_order_relaxed);
    uint32_t last_saved_moving_time = sharedSensorData.movingTimeSeconds;
    int64_t last_nvs_save_time = esp_timer_get_time();

    // --- KEZDETI SZÁMÍTÁS ÉS FELTÖLTÉS ---
    if (xDataMutex != NULL &&
        xSemaphoreTake(xDataMutex, portMAX_DELAY) == pdTRUE) {
      uint64_t initial_pulses = pulseCount.load(std::memory_order_relaxed);
      sharedSensorData.totalDistanceKm =
          (double)initial_pulses * (WHEEL_DIAMETER_M * M_PI) / 1000.0;

      // napi távolság init: különbség a start-impulzusoktól
      {
        uint64_t dp = (initial_pulses >= dailyTripStartPulseCount)
                          ? (initial_pulses - dailyTripStartPulseCount)
                          : 0;
        sharedSensorData.dailyDistanceKm =
          (double)dp * (WHEEL_DIAMETER_M * M_PI) / 1000.0;
      }

      ESP_LOGI(TAG,
               "Initial calculation complete. Total: %.2f km, Daily: %.2f km",
               sharedSensorData.totalDistanceKm,
               sharedSensorData.dailyDistanceKm);
      xSemaphoreGive(xDataMutex);
    }
    // --- VÉGE ---

    while (1) {
        // Várunk új impulzusra, max 5 mp ig
        if (xSemaphoreTake(xPulseSemaphore, pdMS_TO_TICKS(SPEED_TIMEOUT_MS)) == pdTRUE) {
            int64_t now = lastPulseTimeUs.load(std::memory_order_relaxed);
            if (prevPulseUs != 0) {
                double dt = (now - prevPulseUs) / 1000000.0;        // s
                curSpeed = (wheelCircM / dt) * 3.6;                // km/h
                double incKm = wheelCircM / 1000.0;                // km per pulse

                if (xSemaphoreTake(xDataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    sharedSensorData.instantaneousSpeedKmh = curSpeed;
                    sharedSensorData.speedKmh = curSpeed;
                    sharedSensorData.totalDistanceKm += incKm;

                    // napi távolság minden pulzusnál újraszámolva
                    {
                      uint64_t total_p = pulseCount.load(std::memory_order_relaxed);
                      uint64_t dp = (total_p >= dailyTripStartPulseCount)
                                        ? (total_p - dailyTripStartPulseCount)
                                        : 0;
                      sharedSensorData.dailyDistanceKm =
                        (double)dp * wheelCircM / 1000.0;
                    }

                    moveAccum += dt;                        // felhalmozzuk a tört másodperceket
                    /*uint32_t addSec = (uint32_t)moveAccum;  // csak egész másodpercek
                    if (addSec > 0) {
                        sharedSensorData.movingTimeSeconds += addSec;
                        moveAccum -= addSec;                // maradék vissza
                    }*/

                    if (moveAccum >= 1.0) {
                      uint32_t addSec = floor(
                          moveAccum); // Kerekítés lefelé a biztonság kedvéért
                      sharedSensorData.movingTimeSeconds += addSec;
                      moveAccum -= addSec; // maradék vissza
                    }

                    xSemaphoreGive(xDataMutex);
                }
               // ESP_LOGD(TAG, "Pulse dt=%.3f s, speed=%.1f km/h", dt, curSpeed);
            }
            prevPulseUs = now;
        } else {
            // Timeout: 5 mp alatt nem jött új impulzus → 0 km/h
            /*if (curSpeed != 0.0) {
                curSpeed = 0.0;
                if (xSemaphoreTake(xDataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    sharedSensorData.instantaneousSpeedKmh = 0.0;
                    sharedSensorData.speedKmh = 0.0;
                    xSemaphoreGive(xDataMutex);
                }
                ESP_LOGI(TAG, "No pulse for 5s, speed set to 0");*/

            // Timeout: nem jött impulzus, megálltunk
            if (curSpeed != 0.0) {
              if (xSemaphoreTake(xDataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                // ÚJ: Az utolsó impulzus óta eltelt idő hozzáadása a megállás
                // előtt
                int64_t now = esp_timer_get_time();
                int64_t prevPulseUs =
                    lastPulseTimeUs.load(std::memory_order_relaxed);
                if (prevPulseUs != 0) {
                  double dt_since_last =
                      (double)(now - prevPulseUs) / 1000000.0;
                  // Csak a timeout-ig eltelt időt adjuk hozzá, ne a teljeset,
                  // ha az több
                  if (dt_since_last > (SPEED_TIMEOUT_MS / 1000.0)) {
                    dt_since_last = (SPEED_TIMEOUT_MS / 1000.0);
                  }
                  moveAccum += dt_since_last;
                  if (moveAccum >= 1.0) {
                    uint32_t addSec = floor(moveAccum);
                    sharedSensorData.movingTimeSeconds += addSec;
                    moveAccum -= addSec;
                  }
                }

                curSpeed = 0.0;
                sharedSensorData.instantaneousSpeedKmh = 0.0;
                sharedSensorData.speedKmh = 0.0;
                //data_changed = true;
                xSemaphoreGive(xDataMutex);
                ESP_LOGI(TAG, "Speed timeout. Set speed to 0.");
              }
            }
       }
    }
}

// --- Mélyalvás Indítása ---
void go_to_deep_sleep(void) {
    ESP_LOGI(TAG, "Preparing for deep sleep...");

    // Mielőtt aludni megyünk, mentsük el az aktuális értékeket
    uint64_t currentTotalPulses = pulseCount.load(std::memory_order_relaxed);
    esp_err_t err = save_total_pulses_to_nvs(currentTotalPulses);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Final total pulses (%llu) saved to NVS before sleep.", currentTotalPulses);
    } else {
        ESP_LOGE(TAG, "Failed to save final total pulses to NVS before sleep!");
    }

    // Mozgási idő mentése alvás előtt
    /*if (xDataMutex != NULL && xSemaphoreTake(xDataMutex, pdMS_TO_TICKS(100)) == pdTRUE)*/ {
        uint32_t currentMovingTime = sharedSensorData.movingTimeSeconds;
      //  xSemaphoreGive(xDataMutex);
        
        esp_err_t moving_err = save_moving_time_to_nvs(currentMovingTime);
        if (moving_err == ESP_OK) {
            ESP_LOGI(TAG, "Final moving time (%lu sec = %.1f min) saved to NVS before sleep.", 
                     (unsigned long)currentMovingTime, currentMovingTime/60.0);
        } else {
            ESP_LOGE(TAG, "Failed to save final moving time to NVS before sleep!");
        }
    }

    // NVS handle bezárása alvás előtt, ha nyitva van
    if (g_nvs_handle) {
        nvs_close(g_nvs_handle);
        g_nvs_handle = 0;
        ESP_LOGI(TAG, "NVS handle closed before sleep.");
    } else {
        ESP_LOGW(TAG, "NVS handle was not open (or already closed) before sleep.");
    }

    ESP_LOGI(TAG, "Configuring wake up sources...");
    esp_sleep_enable_ext0_wakeup(BUTTON_PIN, 0); // Gomb (GPIO0)
    const uint64_t ext1_wakeup_pin_mask = 1ULL << REED_SWITCH_PIN; // REED (GPIO32)
    esp_sleep_enable_ext1_wakeup(ext1_wakeup_pin_mask, ESP_EXT1_WAKEUP_ALL_LOW);

    ESP_LOGI(TAG, "Entering deep sleep now.");
    fflush(stdout); // Biztosítjuk, hogy minden log kimenjen
    lcd.setTextColor(TFT_WHITE, TFT_BLUE);
    vTaskDelay(pdMS_TO_TICKS(10)); // Várunk egy kicsit
    esp_deep_sleep_start(); 
    
    // A program innen már nem folytatódik, csak ébredés után újraindul az app_main-től
}

// --- REED Szimulációs Task ---
void reed_simulation_task(void *pvParameters) {
  ESP_LOGI(TAG,
           "REED Simulation Task started. Simulating %.1f km/h for %d minutes.",
           SIMULATED_SPEED_KMH, SIMULATION_DURATION_MINUTES);

  if (SIMULATED_SPEED_KMH <= 0 || SIMULATION_DURATION_MINUTES <= 0) {
    ESP_LOGW(TAG, "Simulated speed or duration is 0 or negative. Simulation "
                  "task stopping.");
    vTaskDelete(NULL);
    return;
  }

  // Számítások a szimulációhoz
  const double speed_mps = SIMULATED_SPEED_KMH / 3.6;
  const double wheel_circumference_m = M_PI * WHEEL_DIAMETER_M;
  const double revolutions_per_sec = speed_mps / wheel_circumference_m;
  const double pulses_per_sec = revolutions_per_sec * PULSES_PER_REVOLUTION;

  if (pulses_per_sec <= 0) {
    ESP_LOGW(TAG, "Calculated pulses_per_sec is 0 or negative. Simulation task "
                  "stopping.");
    vTaskDelete(NULL);
    return;
  }

  const uint32_t delay_between_pulses_ms =
      (uint32_t)((1.0 / pulses_per_sec) * 1000.0);

  if (delay_between_pulses_ms == 0) {
    ESP_LOGW(TAG, "Delay between pulses is 0 (speed too high or config error). "
                  "Task stopping.");
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG, "Simulation details: Speed: %.2f m/s, Circumference: %.3f m, "
                "RPS: %.3f, PPS: %.3f, Delay: %lu ms",
           speed_mps, wheel_circumference_m, revolutions_per_sec,
           pulses_per_sec, delay_between_pulses_ms);

  const int64_t simulation_start_time_us = esp_timer_get_time();
  const int64_t simulation_duration_us = (int64_t)SIMULATION_DURATION_MINUTES *
                                         60 * 1000 * 1000; // Mikroszekundumban

  while (1) {
    // Ellenőrizzük, hogy lejárt-e a szimulációs idő
    if ((esp_timer_get_time() - simulation_start_time_us) >=
        simulation_duration_us) {
      ESP_LOGI(TAG, "Simulation duration of %d minutes reached. Stopping "
                    "simulation task.",
               SIMULATION_DURATION_MINUTES);
      // Az utolsó impulzus ideje már be van állítva, az inaktivitás figyelő
      // innen számolhat.
      break; // Kilépés a while ciklusból
    }

    vTaskDelay(pdMS_TO_TICKS(delay_between_pulses_ms));

    // Ellenőrizzük újra az időt a vTaskDelay után, hogy ne generáljunk
    // impulzust, ha már lejárt az idő
    if ((esp_timer_get_time() - simulation_start_time_us) >=
        simulation_duration_us) {
      ESP_LOGI(TAG, "Simulation duration reached during delay. Stopping "
                    "simulation task.");
      break;
    }

    // Szimuláljuk az ISR működését
    pulseCount.fetch_add(1, std::memory_order_relaxed);
    lastPulseTimeUs.store(esp_timer_get_time(), std::memory_order_relaxed);

    // ÚJ: impulzus jelzése a számoló (calculation_and_control) tasknak
    {
      BaseType_t xHPTW = pdFALSE;
      xSemaphoreGiveFromISR(xPulseSemaphore, &xHPTW);
      if (xHPTW) portYIELD_FROM_ISR();
    }

    // ESP_LOGD(TAG, "Simulated pulse. Count: %llu",
    // pulseCount.load(std::memory_order_relaxed));
  }

  ESP_LOGI(TAG, "REED Simulation task finished and self-deleted.");
  vTaskDelete(NULL); // Task törlése
}

// --- Soros Portra Küldő Task ---
void serial_output_task(void *pvParameters) {
    ESP_LOGI(TAG, "Serial output task started.");
    uint64_t local_daily_trip_start_pulses;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(REPORTING_INTERVAL_MS));
        SensorData_t dataToPrint;

        local_daily_trip_start_pulses = dailyTripStartPulseCount; // Olvassuk ki az RTC változót
        uint64_t current_total_p = pulseCount.load(std::memory_order_relaxed);
        uint64_t current_daily_p = 0;
        if (current_total_p >= local_daily_trip_start_pulses) {
             current_daily_p = current_total_p - local_daily_trip_start_pulses;
        }

        if (xSemaphoreTake(xDataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            dataToPrint = sharedSensorData;
            xSemaphoreGive(xDataMutex);
            /*ESP_LOGI(TAG, "Speed: %.2f km/h, Daily: %.2f km, Total: %.2f km, Moving: %lu sec | TP: %llu, DSP: %llu, CDP: %llu",
                   dataToPrint.speedKmh,
                   dataToPrint.dailyDistanceKm,
                   dataToPrint.totalDistanceKm,
                   (unsigned long)dataToPrint.movingTimeSeconds,
                   current_total_p,
                   local_daily_trip_start_pulses,
                   current_daily_p);*/
        } else {
            ESP_LOGW(TAG, "Serial task couldn't get mutex for printing!");
        }
    }
}

// --- Napi Út Nullázó és Kontextusfüggő Reset Gomb Figyelő Task ---
void reset_button_monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "Reset button monitor task started (GPIO%d).", RESET_DAILY_BTN_PIN);

    TickType_t button_press_start_time = 0;
    bool button_currently_pressed = false;
    bool reset_action_taken_this_press = false;

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(RESET_BUTTON_POLL_INTERVAL_MS));

        int button_state = gpio_get_level(RESET_DAILY_BTN_PIN);

        if (button_state == 0) { // Gomb lenyomva (LOW)
            if (!button_currently_pressed) { // Éppen most nyomták le
                button_currently_pressed = true;
                button_press_start_time = xTaskGetTickCount();
                reset_action_taken_this_press = false;
                ESP_LOGD(TAG, "Reset button (GPIO%d) pressed down.", RESET_DAILY_BTN_PIN);
            }

            if (button_currently_pressed && !reset_action_taken_this_press) {
                if ((xTaskGetTickCount() - button_press_start_time) * portTICK_PERIOD_MS >= RESET_BUTTON_HOLD_TIME_MS) {
                    // Hosszú nyomás észlelve - kontextusfüggő reset
                    DisplayState_t currentDisplayState = DISPLAY_SPEED; // Default érték
                    
                    // Aktuális kijelző állapot lekérése
                    if (xDisplayStateMutex != NULL && xSemaphoreTake(xDisplayStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        currentDisplayState = sharedDisplayState;
                        xSemaphoreGive(xDisplayStateMutex);
                    } else {
                        ESP_LOGW(TAG, "Reset task couldn't get display state mutex!");
                    }

                    // Kontextusfüggő reset végrehajtása
                    switch (currentDisplayState) {
                        case DISPLAY_DAILY_DISTANCE:
                            ESP_LOGI(TAG, "Reset button held - resetting DAILY distance.");
                            {
                                uint64_t current_total_pulses_for_reset = pulseCount.load(std::memory_order_relaxed);
                                dailyTripStartPulseCount = current_total_pulses_for_reset;
                                ESP_LOGI(TAG, "Daily trip start pulse count set to: %llu", dailyTripStartPulseCount);

                                if (xSemaphoreTake(xDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                    sharedSensorData.dailyDistanceKm = 0.0;
                                    xSemaphoreGive(xDataMutex);
                                    ESP_LOGI(TAG, "Shared daily distance updated to 0 km.");
                                }
                            }
                            break;

                        case DISPLAY_MAX_SPEED:
                            ESP_LOGI(TAG, "Reset button held - resetting MAX speed.");
                            maxSpeedKmh = 0.0;
                            break;

                        case DISPLAY_AVERAGE_SPEED:
                            ESP_LOGI(TAG, "Reset button held - resetting AVERAGE speed.");
                            if (xSemaphoreTake(xDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                startTotalDistanceKm = sharedSensorData.totalDistanceKm;
                                xSemaphoreGive(xDataMutex);
                            }
                            totalMovingTimeUs = 0;
                            averageSpeedKmh = 0.0;
                            ESP_LOGI(TAG, "Average speed calculation restarted from %.3f km", startTotalDistanceKm);
                            break;

                        case DISPLAY_MOVEMENT_TIME:
                            ESP_LOGI(TAG, "Reset button held - resetting MOVEMENT time.");
                            if (xSemaphoreTake(xDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                sharedSensorData.movingTimeSeconds = 0;
                                xSemaphoreGive(xDataMutex);
                                ESP_LOGI(TAG, "Movement time reset to 0 in sharedSensorData.");
                            }
                            // Mentés NVS-be is
                            save_moving_time_to_nvs(0);
                            break;

                        case DISPLAY_SPEED:
                            ESP_LOGI(TAG, "Reset button held on SPEED display - no reset action defined.");
                            break;

                        case DISPLAY_TOTAL_DISTANCE:
                            ESP_LOGI(TAG, "Reset button held on TOTAL distance - reset NOT ALLOWED for safety.");
                            break;

                        default:
                            ESP_LOGW(TAG, "Reset button held - unknown display state: %d", currentDisplayState);
                            break;
                    }
                    
                    reset_action_taken_this_press = true;
                }
            }
        } else { // Gomb felengedve (HIGH)
            if (button_currently_pressed) {
                ESP_LOGD(TAG, "Reset button (GPIO%d) released.", RESET_DAILY_BTN_PIN);
                button_currently_pressed = false;
                reset_action_taken_this_press = false;
            }
        }
    }
}

// --- GPS UART Initialization ---
void init_gps_uart(void) {
  uart_config_t uart_config = {
      .baud_rate = 9600, // Common baud rate for GPS modules
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 0, // Hiányzó mező hozzáadása
      .source_clk = UART_SCLK_APB,
  };
  int intr_alloc_flags = 0;

  ESP_ERROR_CHECK(
      uart_driver_install(GPS_UART_NUM, 2048, 0, 0, NULL, intr_alloc_flags));
  ESP_ERROR_CHECK(uart_param_config(GPS_UART_NUM, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM, GPS_TX_PIN, GPS_RX_PIN,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_LOGI(TAG, "GPS UART%d initialized on TX:GPIO%d, RX:GPIO%d.", GPS_UART_NUM,
           GPS_TX_PIN, GPS_RX_PIN);
}

// --- Main (app_main) ---
void setup()
{
    ESP_LOGI(TAG, "Starting Wheel Sensor Application V3 (Corrected Sleep Logic)");

    // NVS inicializálása és handle megnyitása
    esp_err_t nvs_err = init_nvs();
    if (nvs_err != ESP_OK || g_nvs_handle == 0) {
        ESP_LOGE(TAG, "NVS initialization or handle opening failed! NVS features might not work. Halting.");
        return;
    }

    // MUTEX LÉTREHOZÁSA KORÁN (mielőtt bármilyen sharedSensorData műveletet végeznénk)
    xDataMutex = xSemaphoreCreateMutex();
    if (xDataMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create data mutex! Halting.");
        if (g_nvs_handle) nvs_close(g_nvs_handle);
        return;
    }

    // Új: Display state mutex létrehozása
    xDisplayStateMutex = xSemaphoreCreateMutex();
    if (xDisplayStateMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create display state mutex! Halting.");
        if (g_nvs_handle) nvs_close(g_nvs_handle);
        vSemaphoreDelete(xDataMutex);
        return;
    }

    // JAVÍTÁS: Kilométeróra beállítása 75 km-re (csak egyszer!)
    //const double targetDistanceKm = 220.0;
#if SET_INITIAL_ODOMETER == 1
    const double wheelCircumferenceM = M_PI * WHEEL_DIAMETER_M;
    const double targetDistanceM = INITIAL_TOTAL_KM * 1000.0;
    const double revolutionsNeeded = targetDistanceM / wheelCircumferenceM;
    const uint64_t pulsesNeeded = (uint64_t)(revolutionsNeeded * PULSES_PER_REVOLUTION);
    
    ESP_LOGI(TAG, "Target: %.1f km = %.0f m = %.2f rev = %llu pulses", 
             targetDistanceM / 1000.0, targetDistanceM, revolutionsNeeded, pulsesNeeded);
    save_total_pulses_to_nvs(pulsesNeeded);
    pulseCount.store(pulsesNeeded, std::memory_order_relaxed);
    double currentKm =
        ((double)pulsesNeeded / PULSES_PER_REVOLUTION) *
        wheelCircumferenceM / 1000.0;
#endif

    uint64_t loaded_pulses_from_nvs = 0;
    load_total_pulses_from_nvs(&loaded_pulses_from_nvs);
    // ensure we restore the stored odometer value on every boot
    pulseCount.store(loaded_pulses_from_nvs, std::memory_order_relaxed);
    ESP_LOGI(TAG, "Boot start pulseCount set to: %llu (from NVS)", loaded_pulses_from_nvs);

    uint32_t savedMovingTime = 0;
    esp_sleep_source_t wakeup_cause = esp_sleep_get_wakeup_cause();
    ESP_LOGW(TAG, "Wakeup cause: %d", wakeup_cause);

    switch (wakeup_cause) {
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:
        case ESP_SLEEP_WAKEUP_TIMER:
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
        case ESP_SLEEP_WAKEUP_ULP:
          /*ESP_LOGI(TAG, "Waking from configured deep sleep source. Daily trip "
                    "start count (%llu) preserved.",
               dailyTripStartPulseCount);*/
          bootCount++;
          ESP_LOGI(TAG, "Boot count incremented to %d.", bootCount);    
          
          // ÉBREDÉSKOR: Mozgási idő betöltése NVS-ből (MOST MÁR VAN MUTEX!)
          if (load_moving_time_from_nvs(&savedMovingTime) == ESP_OK) {
              if (xSemaphoreTake(xDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                  sharedSensorData.movingTimeSeconds = savedMovingTime;
                  xSemaphoreGive(xDataMutex);
                  ESP_LOGI(TAG, "Moving time restored from NVS: %lu seconds (%.1f minutes)", 
                           (unsigned long)savedMovingTime, savedMovingTime/60.0);
              } else {
                  ESP_LOGW(TAG, "Failed to get mutex for moving time restore!");
              }
          }
          break;

        case ESP_SLEEP_WAKEUP_UNDEFINED:
        case ESP_SLEEP_WAKEUP_ALL:
        case ESP_SLEEP_WAKEUP_GPIO:
        case ESP_SLEEP_WAKEUP_UART:
        case ESP_SLEEP_WAKEUP_WIFI:
        case ESP_SLEEP_WAKEUP_COCPU:
        case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
        case ESP_SLEEP_WAKEUP_BT:
        default:
            bootCount = 0;
            ESP_LOGI(TAG, "Cold boot: dailyTripStartPulseCount set to %llu pulses", pulseCount.load(std::memory_order_relaxed));
            dailyTripStartPulseCount = pulseCount.load(std::memory_order_relaxed);
            lastPulseTimeUs.store(0, std::memory_order_relaxed);
            
            // POWER-ON: Mozgási idő nullázása (MOST MÁR VAN MUTEX!)
            if (xSemaphoreTake(xDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                sharedSensorData.movingTimeSeconds = 0;
                xSemaphoreGive(xDataMutex);
                ESP_LOGI(TAG, "Moving time reset to 0 on power-on.");
                save_moving_time_to_nvs(0);
            } else {
                ESP_LOGW(TAG, "Failed to get mutex for moving time reset!");
            }

          // WiFi és OTA csak cold-bootkor
          WiFi.mode(WIFI_AP);
          if (WiFi.softAP(WIFI_SSID, WIFI_PASS, 6)) {
            ESP_LOGI(TAG,
                     "WiFi AP started successfully on channel 6 with SSID: %s",
                     WIFI_SSID);
          } else {
            ESP_LOGE(TAG, "WiFi AP Failed to start!");
          }
          ESP_LOGI(TAG, "WiFi AP starting with SSID: %s", WIFI_SSID);
          uint16_t wifiRetryCount = 0;
/*          while (WiFi.status() != WL_CONNECTED && wifiRetryCount < WIFI_CONNECT_MAX_RETRIES) {
              ESP_LOGI(TAG, "WiFi connecting... Attempt %d/%d", wifiRetryCount + 1, WIFI_CONNECT_MAX_RETRIES);
              vTaskDelay(pdMS_TO_TICKS(1000));
              wifiRetryCount++;
          }*/

          //if (WiFi.status() == WL_CONNECTED) ESP_LOGI(TAG, "WiFi connected, IP: %s", WiFi.localIP().toString().c_str()); else ESP_LOGI(TAG, "WiFi connection failed after %d attempts.", wifiRetryCount);
          ESP_LOGI(TAG, "WiFi AP IP: %s", WiFi.softAPIP().toString().c_str());
          //if (WiFi.status() == WL_CONNECTED) { 
            ArduinoOTA.begin();
            ESP_LOGI(TAG, "OTA Ready");
          //}

          // egyszer futó timer 10 perc múlva WiFi lekapcsoláshoz
          wifiOffTimer = xTimerCreate(
              "WiFiOffTimer",
              pdMS_TO_TICKS(10 * 60 * 1000),
              pdFALSE,
              NULL,
              wifiOffTimerCallback
          );
          if (wifiOffTimer) {
              xTimerStart(wifiOffTimer, 0);
          }
          break;
    }

    // GPIO konfiguráció UTÁN a mutex létrehozása után
    gpio_config_t io_conf_reed = {};
    io_conf_reed.intr_type = GPIO_INTR_NEGEDGE;
    io_conf_reed.pin_bit_mask = (1ULL << REED_SWITCH_PIN);
    io_conf_reed.mode = GPIO_MODE_INPUT;
    io_conf_reed.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf_reed);
    ESP_LOGI(TAG, "REED GPIO %d configured.", REED_SWITCH_PIN);

    gpio_config_t io_conf_button = {};
    io_conf_button.pin_bit_mask = (1ULL << BUTTON_PIN);
    io_conf_button.mode = GPIO_MODE_INPUT;
    io_conf_button.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf_button);
    ESP_LOGI(TAG, "Wakeup Button GPIO %d configured.", BUTTON_PIN);

    gpio_config_t io_conf_reset_btn = {};
    io_conf_reset_btn.pin_bit_mask = (1ULL << RESET_DAILY_BTN_PIN);
    io_conf_reset_btn.mode = GPIO_MODE_INPUT;
    io_conf_reset_btn.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf_reset_btn.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf_reset_btn);
    ESP_LOGI(TAG, "Reset Daily Button GPIO %d configured (EXTERNAL PULL-UP NEEDED!).", RESET_DAILY_BTN_PIN);

#if SIMULATE_REED_INPUT == 0
    esp_err_t isr_err = gpio_install_isr_service(0);
     if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s. Halting.", esp_err_to_name(isr_err));
        if (g_nvs_handle) nvs_close(g_nvs_handle);
        vSemaphoreDelete(xDataMutex);
        return;
    } else if (isr_err == ESP_ERR_INVALID_STATE) {
         ESP_LOGW(TAG, "GPIO ISR service already installed.");
    } else {
         ESP_LOGI(TAG, "GPIO ISR service installed successfully.");
    }

    isr_err = gpio_isr_handler_add(REED_SWITCH_PIN, gpio_isr_handler, (void*) REED_SWITCH_PIN);
     if (isr_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler for REED GPIO %d: %s. Halting.", REED_SWITCH_PIN, esp_err_to_name(isr_err));
        if (g_nvs_handle) nvs_close(g_nvs_handle);
        vSemaphoreDelete(xDataMutex);
        return;
    } else {
         ESP_LOGI(TAG, "ISR handler added for REED GPIO %d", REED_SWITCH_PIN);
    }
#else
  ESP_LOGW(TAG, "REED Simulation is ACTIVE. Real REED ISR is NOT attached.");
#endif
    // Impulzus semafor létrehozása
    xPulseSemaphore = xSemaphoreCreateCounting(UINT32_MAX, 0);
    if (!xPulseSemaphore) {
        ESP_LOGE(TAG, "Failed to create pulse semaphore!");
        return;
    }

    BaseType_t task_created;
    task_created = xTaskCreate(calculation_and_control_task, "calc_ctrl_task", 4096, NULL, 5, NULL);
    if (task_created != pdPASS) { ESP_LOGE(TAG, "Failed to create calculation_and_control_task! Halting."); /* Cleanup... */ return; }

    /*task_created = xTaskCreate(serial_output_task, "serial_task", 4096, NULL, 4, NULL);
    if (task_created != pdPASS) { ESP_LOGE(TAG, "Failed to create serial_output_task! Halting."); /* Cleanup... * return; } */

    task_created = xTaskCreate(nvs_save_task, "nvs_save_task", 4096, NULL, 3, NULL);
    if (task_created != pdPASS) { ESP_LOGE(TAG, "Failed to create nvs_save_task! Halting."); /* Cleanup... */ return; }

    task_created = xTaskCreate(reset_button_monitor_task, "reset_btn_task", 2048, NULL, 6, NULL);
    if (task_created != pdPASS) { ESP_LOGE(TAG, "Failed to create reset_button_monitor_task! Halting."); /* Cleanup... */ return; }

    task_created = xTaskCreate(guiTask, "TFT task", 4096, NULL, 6, NULL);
    if (task_created != pdPASS) { ESP_LOGE(TAG, "Failed to create TFT task! Halting."); /* Cleanup... */ return; }

    xTaskCreate(inactivity_monitor_task, "inactivity_monitor", 2048, NULL, 3, NULL);

#if SIMULATE_REED_INPUT == 1
    task_created = xTaskCreate(reed_simulation_task, "reed_sim_task", 2048,
                               NULL, 4, NULL);
    if (task_created != pdPASS) {
      ESP_LOGE(TAG, "Failed to create REED simulation task! Halting.");
      return;
    } else {
      ESP_LOGI(TAG, "REED Simulation task created.");
    }
#endif

    ESP_LOGI(TAG, "Initialization complete. Tasks are running.");
    vTaskDelay(pdMS_TO_TICKS(100));
}

void loop() {
    // A loop() függvény üres, mert a feladatok már futnak a FreeRTOS-ban.
    // Az alkalmazás működése a létrehozott feladatokon keresztül történik.
    vTaskDelay(pdMS_TO_TICKS(100)); // Csak hogy ne terheljük túl a CPU-t
}