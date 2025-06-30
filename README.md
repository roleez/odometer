
# ESP32 TTGO Odométer Program

## Tartalomjegyzék
- [Áttekintés](#áttekintés)
- [Funkciók és Jellemzők](#funkciók-és-jellemzők)
  - [Fő Funkciók](#fő-funkciók)
- [Bemenetek és Kimenetek](#bemenetek-és-kimenetek)
  - [Bemenetek](#bemenetek)
  - [Kimenetek](#kimenetek)
- [Telepítés és Futás](#telepítés-és-futás)
  - [Szükséges Eszközök](#szükséges-eszközök)
  - [Telepítési Lépések](#telepítési-lépések)
- [Használati Utasítás](#használati-utasítás)
  - [Szimulációs Mód](#szimulációs-mód)
- [Hibakezelés és Gyakori Problémák](#hibakezelés-és-gyakori-problémák)
  - [Gyakori Hibák](#gyakori-hibák)
  - [Hibakeresési Tippek](#hibakeresési-tippek)
- [Kód Áttekintés](#kód-áttekintés)
  - [Főbb Komponensek](#főbb-komponensek)
- [Fejlesztési Lehetőségek](#fejlesztési-lehetőségek)
- [Összegzés](#összegzés)

## Áttekintés

Ez a projekt egy ESP32 TTGO v1.1 panelre épülő, FreeRTOS-alapú, mikrovezérlős odométer, amely kerékpáros vagy egyéb járműves alkalmazásra készült. Az eszköz egyetlen szenzorbemenettel rendelkezik, amely egy reed kapcsolót figyel a kerék forgásának érzékeléséhez. A mért adatokat egy beépített TFT kijelzőn jeleníti meg.

A hardver 3D nyomtatott házban kapott helyet, így tartós, könnyen rögzíthető, ugyanakkor esztétikus kivitelű. Az eszköz széles tápfeszültség-tartományban működőképes (3.3V – 5V), ami alkalmassá teszi USB-ről vagy Li-ion celláról történő üzemeltetésre is. Két beépített nyomógomb (GPIO0 és GPIO35) szolgál a felhasználói interakciókhoz: kijelzés váltás, nullázás, illetve ébresztés deep sleep állapotból.

Könnyű használni, minimális bekötést igényel: csak a reed kontaktus kell rákötni a megfelelő GPIO-lábra. A program FreeRTOS alatt fut, így több feladat párhuzamosan és megbízhatóan működik.

---

## Funkciók és Jellemzők

### Fő Funkciók
1. **Valós idejű sebességmérés** (km/h).
2. **Távolságmérés**:
   - Napi (nullázható) és teljes összegzett érték (NVS-ben tárolva).
3. **Átlagsebesség kiszámítása** a mozgás ideje és a megtett táv alapján.
4. **Maximális sebesség kijelzése** (nem kerül mentésre újraindítás után).
5. **Mozgási idő** kijelzése (összesített aktív menetidő).
6. **Grafikus TFT kijelző** ikonokkal.
7. **Energiatakarékos mód**:
   - Automatikusan deep sleep állapotba lép 5 perc inaktivitás után.
   - Ébresztés reed kapcsoló impulzussal vagy a GPIO0 gombbal.
8. **Kijelző váltás gombbal** (GPIO35): egyszeri megnyomásra az alábbi értékek között lépked:
   - Pillanatnyi sebesség (km/h)
   - Napi távolság (km)
   - Teljes megtett távolság (km)
   - Maximális sebesség (km/h)
   - Átlagsebesség (km/h)
   - Mozgási idő (óra:perc)
9. **Napi számláló nullázása**:
   - GPIO35 hosszú (>1 másodperces) nyomásával.
10. **Szimulációs mód**:
    - Fordítás előtt aktiválható (`config.h`: `SIMULATE_REED_INPUT 1`)
    - Szimulált sebesség: 14.5 km/h, időtartam: 3 perc.

---

## Bemenetek és Kimenetek

### Bemenetek
- **Reed kapcsoló (GPIO26)**: a kerék forgásának érzékeléséhez.
- **Kijelzésváltó/nullázó gomb (GPIO35)**:
  - Rövid nyomás: kijelzett adat váltása.
  - Hosszú nyomás: aktuális kijelzett érték nullázása, ha támogatott (pl. napi táv, max sebesség, mozgási idő).
- **Ébresztő gomb (GPIO0)**: deep sleep állapotból való manuális ébresztés.

### Kimenetek
- **TFT kijelző**: megjeleníti az összes mért és számított adatot.
- **Soros port**: hibakeresési és naplózási célra használható (ESP_LOG macrokkal).

---

## Telepítés és Futás

### Szükséges Eszközök

- **Hardver**:
  - ESP32 TTGO v1.1 panel beépített TFT kijelzővel.
  - Reed kapcsoló.
  - Külső pull-up ellenállás (10kΩ) a GPIO35-höz.
  - Tápellátás (USB vagy akkumulátor).

- **Szoftver**:
  - PlatformIO IDE.
  - `LovyanGFX` könyvtár.
  - ESP-IDF vagy Arduino keretrendszer.

### Telepítési Lépések

1. Kód klónozása:
   ```bash
   git clone https://github.com/roleez/odometer.git
   cd esp32TTGO_odometer
   ```

2. PlatformIO megnyitása, szükséges könyvtárak telepítése.

3. A `config.h` fájlban állítsd be a megfelelő kerékátmérőt (`WHEEL_DIAMETER_M`), valamint a szimulációs módot, ha szükséges.

4. Hardver bekötése:
   - Reed: GPIO26
   - Gomb (GPIO35): külső pull-up-pal
   - Ébresztőgomb (GPIO0): belső pull-up aktív

5. Fordítás és feltöltés az ESP32 TTGO eszközre.

6. Soros monitor megnyitása hibakereséshez.

---

## Használati Utasítás

1. **Bekapcsolás után** a TFT kijelzőn automatikusan megjelenik az első adat (sebesség).
2. **Kijelzett adat váltása**: rövid nyomás a GPIO35 gombon.
3. **Nullázás**: hosszú nyomás (1 mp felett) a GPIO35 gombon (csak napi táv, mozgási idő, max sebesség).
4. **Energiatakarékosság**: 5 perc tétlenség után deep sleep.
5. **Ébresztés**: Reed kapcsoló vagy GPIO0 gomb.

### Szimulációs Mód
- A szimulációs mód engedélyezéséhez állítsd a `SIMULATE_REED_INPUT` értékét `1`-re a `main.cpp` fájlban.
- A program 20 km/h sebességet szimulál 1 percig.

---

## Hibakezelés és Gyakori Problémák

### Gyakori Hibák
1. **Nincs adat a TFT kijelzőn**:
   - Ellenőrizd a TFT kijelző csatlakozását és inicializálását.

2. **Reed kapcsoló nem érzékel**:
   - Ellenőrizd a reed kapcsoló bekötését és a GPIO32 konfigurációját.

3. **NVS hibák**:
   - Ha az NVS inicializálása sikertelen, töröld az NVS partíciót, és inicializáld újra.

4. **Nullázó gomb nem működik**:
   - Győződj meg róla, hogy külső pull-up ellenállás van a GPIO35-höz csatlakoztatva.

### Hibakeresési Tippek
- Használd a soros monitort a naplózás és hibák megtekintéséhez.
- Ellenőrizd a `TAG` logokat a feladatok állapotának nyomon követéséhez.

---

## Kód Áttekintés

### Főbb Komponensek
- **`main.cpp`**: rendszerinicializálás, feladatok indítása, deep sleep kezelés.
- **`displaytft.cpp`**: kijelző frissítése, gombkezelés, kijelzett értékek váltása.
- **`config.h`**: hardveres beállítások és szimulációs opciók.
- **FreeRTOS feladatok**:
  - `guiTask`: kijelző és gomb logika.
  - `calculation_and_control_task`: sebesség- és távszámítás.
  - `reset_button_monitor_task`: hosszú nyomás kezelése.
  - `reed_simulation_task`: szimulációs bemenet (ha engedélyezett).

---

## Fejlesztési Lehetőségek

- **Bluetooth LE adatátvitel** mobilalkalmazáshoz.
- **GPS integráció** pontos helymeghatározáshoz.
- **Akkumulátorfigyelés** és töltöttségi szint kijelzés.
- **Webes interfész** a konfigurációhoz és adatlekérdezéshez.
- **Testreszabható megjelenítés** egyéni igényekhez.

---

## Összegzés

Az ESP32 TTGO Odométer egy könnyen használható, FreeRTOS alapú eszköz, amely pontos adatokat szolgáltat a kerékpáros vagy egyéb járműves mozgásokról. A kompakt 3D-nyomtatott ház, a minimális bekötés, valamint a szimulációs lehetőség miatt ideális választás hobbistáknak, oktatási projektekhez vagy akár saját fejlesztésű fedélzeti kijelzőhöz.
