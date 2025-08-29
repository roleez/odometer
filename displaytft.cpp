#include "displaytft.h" // Include-old a saját headerödet
#include "esp_log.h"    // Az ESP_LOGI-hoz
#include "icons.h"     // Az ikonokhoz
#include "driver/gpio.h" // GPIO funkciókhoz
#include "config.h"

// Külső változók deklarálása
extern const char *TAG;
extern uint16_t bootCount;
extern SemaphoreHandle_t xDataMutex;
extern SensorData_t sharedSensorData;
extern DisplayState_t sharedDisplayState;
extern SemaphoreHandle_t xDisplayStateMutex;


static TimerHandle_t xDisplayTimer = NULL;
static bool manualDisplayChange = false;
TFT_eSPI lcd = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&lcd);

// Timer callback függvény - automatikus kijelző váltáshoz
void displayTimerCallback(TimerHandle_t xTimer) {

  if (keptoggle) {
    return;
  }
  
  // Csak akkor váltunk automatikusan, ha nem volt manuális váltás
  if (!manualDisplayChange) {
    // Kijelző állapot váltása
    DisplayState_t newState =
        (DisplayState_t)((sharedDisplayState + 1) % DISPLAY_STATE_COUNT);

    // Frissítsük a megosztott display state-et
    if (xDisplayStateMutex != NULL &&
        xSemaphoreTake(xDisplayStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      sharedDisplayState = newState;
      xSemaphoreGive(xDisplayStateMutex);
      ESP_LOGI(TAG, "Auto display switch: %d -> %d", sharedDisplayState,
               newState);
    }
  }

  // Reset a manuális flag-et a következő ciklusra
  manualDisplayChange = false;
}

void draw1bitBitmap(int x, int y, const uint8_t *bitmap, int w,
                    int h, uint16_t fgColor, uint16_t bgColor) {
  int byteWidth = (w + 7) / 8; // hány bájt van egy sorban
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      uint8_t byte = bitmap[j * byteWidth + i / 8];
      bool pixelOn = byte & (0x80 >> (i % 8));
      sprite.drawPixel(x + i, y + j, pixelOn ? fgColor : bgColor);
    }
  }
}

void guiTask(void *pvParameters)
{
  lcd.init();
  lcd.setRotation(1);
  //lcd.setColorDepth(16);

  sprite.setColorDepth(16); // Színmélység beállítása
  sprite.createSprite(lcd.width(), lcd.height()); // Sprite méretének beállítása
  sprite.setTextSize(1);                          // Betűméret beállítása
  sprite.setTextDatum(MC_DATUM); // Szöveg középre igazítása
  sprite.setTextColor(TFT_WHITE,
                      TFT_BLUE); // Szöveg színe: fehér, háttér: kék
  sprite.setFreeFont(&FreeMonoBoldOblique12pt7b); // Betűtípus beállítása

  // Timer létrehozása
  xDisplayTimer =
      xTimerCreate("DisplayTimer",            // Timer neve
                   pdMS_TO_TICKS(KEP_VALTAS), // Periódus (KEP_VALTAS ms)
                   pdTRUE,                    // Auto-reload (ismétlődő)
                   0,                         // Timer ID
                   displayTimerCallback       // Callback függvény
                   );

  if (xDisplayTimer == NULL) {
    ESP_LOGE(TAG, "Failed to create display timer!");
  } else {
    // Timer indítása
    if (xTimerStart(xDisplayTimer, 0) != pdPASS) {
      ESP_LOGE(TAG, "Failed to start display timer!");
    } else {
      ESP_LOGI(TAG, "Display auto-switch timer started (%d ms)", KEP_VALTAS);
    }
  }

  ESP_LOGI(TAG, "Initial TFT ok.");
  ESP_LOGI(TAG, "Waking from deep sleep. Boot count: %u", bootCount);

  char display_buffer[40];
  char me_str[10];

  static DisplayState_t currentDisplayState = DISPLAY_SPEED;
  SensorData_t localSensorData;
  SensorData_t prevSensorData = {-1.0, -1.0, -1.0, -1.0, 0}; // 5 mező: speedKmh, dailyDistanceKm, totalDistanceKm, instantaneousSpeedKmh, movingTimeSeconds
  bool force_redraw = true; // Az első ciklusban mindenképp rajzoljunk

  // Átlagsebesség számításhoz - csak a mérési idő marad lokális
  static int64_t lastMeasurementTimeUs = 0;

  // Mozgási idő változók módosítása - most a sharedSensorData-t használjuk
  static int64_t lastMovementUpdateTimeUs = 0; // Utolsó mozgási idő frissítés ideje
  static bool movementTimeInitialized = false; // Inicializálás flag

  // Gomb kezelési változók - EGYSZERŰSÍTETT (csak display state váltáshoz)
  static bool utolsoGombAllapot = 1;  // ESP-IDF-ben 1/0 értékek
  static bool jelenlegiGombAllapot = 1;
  static TickType_t gombNyomasKezdete = 0;
  static bool gombNyomva = false;
  const TickType_t prellezesiIdo = pdMS_TO_TICKS(50);
  const TickType_t rovidNyomasMaxIdo = pdMS_TO_TICKS(400);
  static TickType_t utolsoPrellezesIdo = 0;

  ESP_LOGI(TAG, "GUI Task started with initial display state: %d", currentDisplayState);

  while (1) {
    bool data_changed = false;
    bool state_switched = false;

    // EGYSZERŰSÍTETT gomb kezelés - csak rövid nyomás (display state váltás)
    int gombOlvasas = gpio_get_level(RESET_DAILY_BTN_PIN);
    
    if (gombOlvasas != utolsoGombAllapot) {
      utolsoPrellezesIdo = xTaskGetTickCount();
    }
    
    TickType_t currentTick = xTaskGetTickCount();
    TickType_t debounceElapsed = currentTick - utolsoPrellezesIdo;
    
    if (debounceElapsed > prellezesiIdo) {
      if (gombOlvasas != jelenlegiGombAllapot) {
        jelenlegiGombAllapot = gombOlvasas;
        
        if (jelenlegiGombAllapot == 0 && !gombNyomva) {  // 0 = lenyomott állapot
          gombNyomasKezdete = xTaskGetTickCount();
          gombNyomva = true;
          ESP_LOGD(TAG, "Button pressed down");
        } else if (jelenlegiGombAllapot == 1 && gombNyomva) {  // 1 = felengedett állapot
          TickType_t nyomasHossza = xTaskGetTickCount() - gombNyomasKezdete;
          gombNyomva = false;
          
          if (nyomasHossza < rovidNyomasMaxIdo) {
            // Rövid nyomás - kijelző állapot váltás
            DisplayState_t oldState = currentDisplayState;
            currentDisplayState = (DisplayState_t)((currentDisplayState + 1) % DISPLAY_STATE_COUNT);
            state_switched = true;
            manualDisplayChange = true;
            // Timer újraindítása a manuális váltás után
            if (xDisplayTimer != NULL) {
              xTimerReset(xDisplayTimer, 0);
              ESP_LOGD(TAG, "Display timer reset after manual change");
            }
            ESP_LOGI(TAG, "GUI: Short button press - display state changed %d -> %d", oldState, currentDisplayState);
            
            // Frissítsük a megosztott display state-et
            if (xDisplayStateMutex != NULL && xSemaphoreTake(xDisplayStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
              sharedDisplayState = currentDisplayState;
              xSemaphoreGive(xDisplayStateMutex);
            }
          }
          // Hosszú nyomás kezelését a reset task végzi
        }
      }
    }
    
    utolsoGombAllapot = gombOlvasas;

    // ÚJ: Automatikus váltás ellenőrzése
    DisplayState_t sharedState;
    if (xDisplayStateMutex != NULL &&
        xSemaphoreTake(xDisplayStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      sharedState = sharedDisplayState;
      xSemaphoreGive(xDisplayStateMutex);

      // Ha a shared state megváltozott (automatikus váltás), frissítsük a local
      // state-et
      if (sharedState != currentDisplayState) {
        currentDisplayState = sharedState;
        state_switched = true;
        ESP_LOGI(TAG, "GUI: Auto display switch detected - new state: %d",
                 currentDisplayState);
      }
    }

    // 1. Adatok kiolvasása a megosztott struktúrából
    if (xDataMutex != NULL &&
        xSemaphoreTake(xDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      localSensorData = sharedSensorData;
      xSemaphoreGive(xDataMutex);
      
      // Mozgási idő frissítése a sharedSensorData-ban
      bool isCurrentlyMovingForTime = (localSensorData.speedKmh > 0.01); // 0.01 km/h felett mozgás
      int64_t currentTimeUs = esp_timer_get_time();
      
      // Mozgási idő inicializálása, ha még nem volt
      if (!movementTimeInitialized) {
        lastMovementUpdateTimeUs = currentTimeUs;
        movementTimeInitialized = true;
        ESP_LOGI(TAG, "Mozgási idő mérés inicializálva - sebesség: %.2f km/h", localSensorData.speedKmh);
      } else {
        // Időkülönbség kiszámítása az utolsó frissítés óta
        int64_t deltaTimeUs = currentTimeUs - lastMovementUpdateTimeUs;
        
        // Ha mozgunk, akkor minden delta időt hozzáadunk másodperc pontossággal
        if (isCurrentlyMovingForTime && deltaTimeUs > 0) {
          // Minden 100ms-nál nagyobb időkülönbségnél frissítünk
          if (deltaTimeUs >= 100000) { // 0.1 másodperc (100ms)
            double deltaSecondsFloat = (double)deltaTimeUs / 1000000.0;
            uint32_t deltaSecondsInt = (uint32_t)(deltaSecondsFloat + 0.5); // Kerekítés
            
            sharedSensorData.movingTimeSeconds += deltaSecondsInt;
            lastMovementUpdateTimeUs = currentTimeUs;
          } else {
            lastMovementUpdateTimeUs = currentTimeUs;
          }
        } else if (!isCurrentlyMovingForTime) {
          // Ha nem mozgunk, akkor frissítjük az időt (de nem adjuk hozzá a mozgási időhöz)
          lastMovementUpdateTimeUs = currentTimeUs;
        }
      }
      
      // Frissített localSensorData lekérése
      localSensorData = sharedSensorData;
      
      xSemaphoreGive(xDataMutex);

      // Átlagsebesség frissítése minden állapotnál (időalapú módszer)
      bool isCurrentlyMoving = (localSensorData.speedKmh > 0.1); // 0.1 km/h felett mozgás
      
      // Ha még nem volt inicializálva a mérés kezdete
      if (lastMeasurementTimeUs == 0) {
        lastMeasurementTimeUs = currentTimeUs;
        startTotalDistanceKm = localSensorData.totalDistanceKm;
        totalMovingTimeUs = 0; // FONTOS: kezdéskor 0
        ESP_LOGI(TAG, "Átlagsebesség mérés inicializálva: %.3f km-től", startTotalDistanceKm);
      } else {
        // Időkülönbség kiszámítása az utolsó mérés óta
        int64_t deltaTimeUs = currentTimeUs - lastMeasurementTimeUs;
        
        // JAVÍTÁS: Ha JELENLEG mozgunk, akkor az időintervallumot hozzáadjuk
        if (isCurrentlyMoving && deltaTimeUs > 0) {
          totalMovingTimeUs += deltaTimeUs;
        }
        
        // Átlagsebesség újraszámítása
        double distanceTraveledKm = localSensorData.totalDistanceKm - startTotalDistanceKm;
        
        if (totalMovingTimeUs > 0 && distanceTraveledKm > 0.001) { // Legalább 1 méter megtételével
          double totalMovingTimeHours = (double)totalMovingTimeUs / (1000000.0 * 3600.0); // mikroszekundum -> óra
          averageSpeedKmh = distanceTraveledKm / totalMovingTimeHours;
          
          // JAVÍTÁS: Ésszerű határok beállítása
          if (averageSpeedKmh > 200.0) { // Ha 200 km/h felett van, valami nem stimmel
            ESP_LOGW(TAG, "Irreális átlagsebesség (%.1f km/h) - nullázás", averageSpeedKmh);
            // Nullázzuk és újrakezdünk
            startTotalDistanceKm = localSensorData.totalDistanceKm;
            totalMovingTimeUs = 0;
            averageSpeedKmh = 0.0;
          }
          
          // Debug log az átlagsebesség számításhoz
          static int debug_avg_counter = 0;
          if (++debug_avg_counter >= 50) { // Minden 50. ciklusban
            /*ESP_LOGI(TAG, "Átlag debug: táv=%.4f km, idő_us=%lld, idő_h=%.6f, átlag=%.2f km/h, mozgás: %s", 
                     distanceTraveledKm, totalMovingTimeUs, totalMovingTimeHours, averageSpeedKmh, 
                     isCurrentlyMoving ? "IGEN" : "NEM");*/
            debug_avg_counter = 0;
          }
        } else {
          // Ha nincs elég távolság vagy idő, 0 marad az átlag
          if (totalMovingTimeUs == 0) {
            averageSpeedKmh = 0.0;
          }
        }
        
        // Állapotok frissítése
        lastMeasurementTimeUs = currentTimeUs;
      }

      // Ellenőrizzük, hogy az aktuálisan kijelzendő adat változott-e
      switch (currentDisplayState) {
      case DISPLAY_SPEED:
        if (fabs(localSensorData.speedKmh - prevSensorData.speedKmh) > 0.01)
          data_changed = true;
        if (localSensorData.speedKmh > maxSpeedKmh) {
          maxSpeedKmh = localSensorData.speedKmh;
        }
        break;
      case DISPLAY_DAILY_DISTANCE:
        if (fabs(localSensorData.dailyDistanceKm - 
                 prevSensorData.dailyDistanceKm) > 0.01)
          data_changed = true;
        if (localSensorData.speedKmh > maxSpeedKmh) {
          maxSpeedKmh = localSensorData.speedKmh;
        }
        break;
      case DISPLAY_TOTAL_DISTANCE:
        if (fabs(localSensorData.totalDistanceKm - 
                 prevSensorData.totalDistanceKm) > 0.0001)
          data_changed = true;
        if (localSensorData.speedKmh > maxSpeedKmh) {
          maxSpeedKmh = localSensorData.speedKmh;
        }
        break;
      case DISPLAY_MAX_SPEED:
        // A max sebesség változását ellenőrizzük
        static double prevMaxSpeed = -1.0;
        if (fabs(maxSpeedKmh - prevMaxSpeed) > 0.01) {
          data_changed = true;
          prevMaxSpeed = maxSpeedKmh;
        }
        if (localSensorData.speedKmh > maxSpeedKmh) {
          maxSpeedKmh = localSensorData.speedKmh;
          data_changed = true;
        }
        break;
      case DISPLAY_AVERAGE_SPEED:
        // Az átlagsebesség változását ellenőrizzük
        static double prevAvgSpeed = -1.0;
        if (fabs(averageSpeedKmh - prevAvgSpeed) > 0.01) {
          data_changed = true;
          prevAvgSpeed = averageSpeedKmh;
        }
        if (localSensorData.speedKmh > maxSpeedKmh) {
          maxSpeedKmh = localSensorData.speedKmh;
        }
        break;
      case DISPLAY_MOVEMENT_TIME:
        // A mozgási idő változását ellenőrizzük (másodpercenként frissül)
        static uint32_t prevMovementTime = UINT32_MAX;
        if (localSensorData.movingTimeSeconds != prevMovementTime) {
          data_changed = true;
          prevMovementTime = localSensorData.movingTimeSeconds;
        }
        if (localSensorData.speedKmh > maxSpeedKmh) {
          maxSpeedKmh = localSensorData.speedKmh;
        }
        break;
      default:
        break;
      }

    } else {
      if (xDataMutex == NULL)
        ESP_LOGE(TAG, "GUI Task: xDataMutex is NULL!");
      else
        ESP_LOGW(TAG, "GUI Task: Could not take mutex.");
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // 3. Kijelző frissítése, ha kell
    if (force_redraw || state_switched || data_changed) {
      // Ha állapot váltás vagy erőltetett rajzolás van, mindig töröljünk mindent
      if (force_redraw || state_switched) {
        sprite.fillScreen(TFT_BLUE); // Teljes képernyő törlése
        sprite.drawRect(0, 0, sprite.width(), sprite.height(), TFT_WHITE);
        ESP_LOGI(TAG, "Screen cleared for state: %d", currentDisplayState);
      }

      // ikon kirajzolás állapottól függően
      sprite.fillSprite(TFT_BLUE);
      sprite.fillScreen(TFT_BLUE); // Teljes képernyő törlése
      sprite.drawRect(0, 0, sprite.width(), sprite.height(), TFT_WHITE);

      switch (currentDisplayState) {
      case DISPLAY_SPEED:
        draw1bitBitmap(7, 82, iconSpeed, iconSpeedWidth, iconSpeedHeight, TFT_WHITE, TFT_BLUE);
        //ESP_LOGD(TAG, "Drawing speed icon");
        break;
      case DISPLAY_DAILY_DISTANCE:
        draw1bitBitmap(7, 82, iconDistance, iconDistanceWidth, iconDistanceHeight, TFT_WHITE, TFT_BLUE);
        //ESP_LOGD(TAG, "Drawing daily distance icon");
        break;
      case DISPLAY_TOTAL_DISTANCE:
        draw1bitBitmap(7, 82, iconDistance, iconDistanceWidth, iconDistanceHeight, TFT_WHITE, TFT_BLUE);
        //ESP_LOGD(TAG, "Drawing total distance icon");
        break;
      case DISPLAY_MAX_SPEED:
        //ESP_LOGD(TAG, "Drawing max speed icon");
        break;
      case DISPLAY_AVERAGE_SPEED:
        //ESP_LOGD(TAG, "Drawing average speed icon");
        break;
      case DISPLAY_MOVEMENT_TIME:
        //ESP_LOGD(TAG, "Drawing movement time (no icon)");
        break;
      default:
        break;
      }

      // Megfelelő szöveg összeállítása az aktuális állapot alapján
      switch (currentDisplayState) {
      case DISPLAY_SPEED:
        snprintf(display_buffer, sizeof(display_buffer), "%.1f",
                 localSensorData.speedKmh);
        strncpy(me_str, "km/h", sizeof(me_str));
        prevSensorData.speedKmh = localSensorData.speedKmh;
        break;
      case DISPLAY_DAILY_DISTANCE:
        snprintf(display_buffer, sizeof(display_buffer), "%.2f",
                 localSensorData.dailyDistanceKm);
        strncpy(me_str, "km day", sizeof(me_str));
        prevSensorData.dailyDistanceKm = localSensorData.dailyDistanceKm;
        break;
      case DISPLAY_TOTAL_DISTANCE:
        snprintf(display_buffer, sizeof(display_buffer), "%.1f",
                 localSensorData.totalDistanceKm);
        strncpy(me_str, "km all", sizeof(me_str));
        prevSensorData.totalDistanceKm = localSensorData.totalDistanceKm;
        break;
      case DISPLAY_MAX_SPEED:
        snprintf(display_buffer, sizeof(display_buffer), "%.1f", maxSpeedKmh);
        strncpy(me_str, "km/h max", sizeof(me_str));
        break;
      case DISPLAY_AVERAGE_SPEED:
        snprintf(display_buffer, sizeof(display_buffer), "%.1f", averageSpeedKmh);
        strncpy(me_str, "km/h avg", sizeof(me_str));
        break;
      case DISPLAY_MOVEMENT_TIME:
        // Mozgási idő óó:pp formátumban - most a sharedSensorData-ból
        {
          int hours = localSensorData.movingTimeSeconds / 3600;
          int minutes = (localSensorData.movingTimeSeconds % 3600) / 60;
          snprintf(display_buffer, sizeof(display_buffer), "%02d:%02d", hours, minutes);
          strncpy(me_str, "fut.ido", sizeof(me_str));
        }
        break;
      default:
        strncpy(display_buffer, "Error", sizeof(display_buffer));
        strncpy(me_str, "ERR", sizeof(me_str));
        break;
      }

      // Szöveg kirajzolása
      sprite.setTextColor(TFT_WHITE, TFT_BLUE);
      sprite.setTextSize(3);
      sprite.setFreeFont(&FreeMonoBold12pt7b);
      sprite.drawString(display_buffer, sprite.width() / 2, sprite.height() / 2 - 27);
      sprite.setFreeFont(&FreeSerif9pt7b);

      // Ellenőrizzük, hogy "km "-rel kezdődik-e
      int me_str_x_pos;
      if (strncmp(me_str, "km ", 3) == 0) {
        me_str_x_pos = sprite.width() / 2 + 20; // Más pozíció km esetén
      } else {
        me_str_x_pos = sprite.width() / 2 + 8; // Eredeti pozíció
      }

      sprite.drawString(me_str, me_str_x_pos, sprite.height() / 2 + 33);
      sprite.setTextSize(2);
      sprite.setFreeFont(nullptr);
      sprite.setTextColor(TFT_LIGHTGREY, TFT_BLUE);
      sprite.drawString("HR", 18, 11);
      
      force_redraw = false;
    }

    // FONTOS: A sprite push mindig történjen meg
    sprite.pushSprite(0, 0);
    
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
