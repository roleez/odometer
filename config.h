#ifndef CONFIG_H
#define CONFIG_H

// --- GPIO Pin Definitions ---
// These are the physical connections to the SD card adapter
#define SD_SCK_PIN GPIO_NUM_15
#define SD_MISO_PIN GPIO_NUM_12
#define SD_MOSI_PIN GPIO_NUM_13
#define SD_CS_PIN GPIO_NUM_33

// Define the pins for the GPS UART
#define GPS_RX_PIN GPIO_NUM_17  // GPS TX -> ESP32 RX
#define GPS_TX_PIN GPIO_NUM_25  // GPS RX -> ESP32 TX
#define GPS_UART_NUM UART_NUM_2 // Using UART2 for GPS

// --- Konfiguráció ---
#define REED_SWITCH_PIN     GPIO_NUM_26
#define BUTTON_PIN          GPIO_NUM_0  // Ébresztő gomb IO0-n
#define RESET_DAILY_BTN_PIN GPIO_NUM_35 // Napi út nullázó gomb (!! Külső PULL-UP szükséges !!)
#define WHEEL_DIAMETER_M    0.348        // Kerék átmérője méterben (!! FONTOS: Állítsd be a valós értéket !!)
#define PULSES_PER_REVOLUTION 1         // Impulzusok száma egy teljes kerékfordulat alatt

#define REPORTING_INTERVAL_MS 1000      // Adatküldés, számítás gyakorisága (1 mp)
#define INACTIVITY_TIMEOUT_S  (5 * 60) // Inaktivitási időkorlát másodpercben (5 perc)
#define INACTIVITY_TIMEOUT_US (INACTIVITY_TIMEOUT_S * 1000000ULL)
#define NVS_SAVE_INTERVAL_MS (15 * 60 * 1000) // NVS mentési intervallum (15 perc)
#define RESET_BUTTON_HOLD_TIME_MS 1000    // Napi számláló nullázásához nyomva tartás ideje
#define RESET_BUTTON_POLL_INTERVAL_MS 50  // Napi nullázó gomb figyelési gyakorisága

// Kijelző váltási intervallum (már nem használt, de a kompatibilitás miatt megtartva)
#define KEP_VALTAS 3000  // 3 másodperc milliszekundumban

// --- Szimulációs Konfiguráció ---
#define SIMULATE_REED_INPUT 0       // 1 = Szimuláció aktív, 0 = Szimuláció inaktív
#define SIMULATED_SPEED_KMH 14.5     // Szimulált sebesség km/h-ban
#define SIMULATION_DURATION_MINUTES 3 // Szimuláció időtartama percben (csak szimulációhoz)

#endif