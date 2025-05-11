# ESP32 TTGO Odométer Program

## Áttekintés

Ez a program egy ESP32 TTGO mikrokontrolleren futó digitális odométer, amely a jármű vagy kerékpár sebességét, napi megtett távolságát és teljes távolságát méri. A program reed kapcsolós szenzort használ a kerék forgásának érzékelésére, és az adatokat egy TFT kijelzőn jeleníti meg.

### Cél

A program célja, hogy valós idejű adatokat biztosítson a sebességről és a megtett távolságról olyan járművek vagy kerékpárok számára, amelyek nem rendelkeznek beépített odométerrel. Az eszköz energiatakarékos módot is tartalmaz, amely inaktivitás esetén mélyalvásba helyezi a rendszert.

---

## Funkciók és Jellemzők

### Fő Funkciók
1. **Valós idejű sebességmérés**:
   - A kerék forgásából számítja ki a sebességet km/h-ban.

2. **Távolságmérés**:
   - Napi és teljes távolságot követ km-ben.

3. **Grafikus kijelző**:
   - A TFT kijelzőn megjeleníti a sebességet, napi és teljes távolságot.

4. **Energiatakarékos mód**:
   - Inaktivitás esetén mélyalvásba helyezi az eszközt.

5. **Napi távolság nullázása**:
   - Egy dedikált gombbal nullázható a napi távolság.

6. **Nem felejtő memória (NVS)**:
   - A teljes távolság adatait menti, hogy újraindítás után is elérhetőek legyenek.

7. **Szimulációs mód**:
   - Teszteléshez szimulálja a reed kapcsoló bemenetét.

---

### Bemenetek és Kimenetek

#### Bemenetek:
- **Reed kapcsoló (GPIO32)**: Érzékeli a kerék forgását.
- **Nullázó gomb (GPIO35)**: A napi távolság nullázására szolgál.
- **Ébresztő gomb (GPIO0)**: Mélyalvásból ébreszti az eszközt.

#### Kimenetek:
- **TFT kijelző**: Megjeleníti a sebességet, napi és teljes távolságot.
- **Soros port**: Naplózza az adatokat és hibákat.

---

## Telepítés és Futás

### Szükséges Eszközök
- **Hardver**:
  - ESP32 TTGO mikrokontroller TFT kijelzővel.
  - Reed kapcsoló szenzor.
  - Külső pull-up ellenállás a GPIO35-höz.
  - Tápellátás (pl. akkumulátor vagy USB).

- **Szoftver**:
  - PlatformIO IDE.
  - LovyanGFX könyvtár a TFT kijelzőhöz.
  - ESP-IDF vagy Arduino keretrendszer.

### Telepítési Lépések
1. **Kód Klónozása**:
   ```bash
   git clone https://github.com/your-repo/esp32TTGO_odometer.git
   cd esp32TTGO_odometer
   ```

2. **PlatformIO Megnyitása**:
   - Nyisd meg a projektet a PlatformIO IDE-ben.

3. **Könyvtárak Telepítése**:
   - Győződj meg róla, hogy a `LovyanGFX` könyvtár telepítve van.

4. **Hardver Konfiguráció**:
   - Kösd össze a reed kapcsolót a GPIO32-vel.
   - Kösd össze a nullázó gombot a GPIO35-tel, külső pull-up ellenállással.

5. **Fordítás és Feltöltés**:
   - Fordítsd le a projektet, és töltsd fel az ESP32 TTGO-ra.

6. **Soros Monitor**:
   - Nyisd meg a soros monitort a naplózás megtekintéséhez.

---

## Használati Utasítás

### Alapvető Használat
1. Kapcsold be az ESP32 TTGO-t.
2. A TFT kijelzőn megjelenik a sebesség, napi és teljes távolság.
3. A napi távolság nullázásához tartsd lenyomva a nullázó gombot 1 másodpercig.
4. Az eszköz automatikusan mélyalvásba lép 2 perc inaktivitás után.

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

## Fejlesztési Lehetőségek

### Javasolt Fejlesztések
1. **Bluetooth Integráció**:
   - Adatok továbbítása mobilalkalmazásba.

2. **GPS Integráció**:
   - Pontosabb távolságmérés GPS segítségével.

3. **Akkumulátorfigyelés**:
   - Az akkumulátor állapotának kijelzése a TFT-n.

4. **Testreszabható Kijelző**:
   - A megjelenített adatok és elrendezés testreszabása.

5. **Webes Felület**:
   - Távoli monitorozás és konfiguráció webes interfészen keresztül.

---

## Kód Áttekintés

### Főbb Komponensek
1. **`main.cpp`**:
   - Hardver inicializálása, feladatok indítása, mélyalvás logika.

2. **`displaytft.cpp`**:
   - Grafikus felület kezelése a TFT kijelzőn.

3. **NVS Funkciók**:
   - Adatok mentése és betöltése nem felejtő memóriából.

4. **Feladatok**:
   - `calculation_and_control_task`: Sebesség és távolság számítása.
   - `serial_output_task`: Adatok naplózása a soros portra.
   - `reset_button_monitor_task`: Nullázó gomb figyelése.
   - `reed_simulation_task`: Reed kapcsoló bemenet szimulálása.

---

## Összegzés

Az ESP32 TTGO odométer program egy hatékony és energiatakarékos megoldás a sebesség és távolság mérésére. A program könnyen bővíthető további funkciókkal, mint például Bluetooth vagy GPS integráció.