
# ESP32 TTGO Odometer Program

## Overview

This project is a FreeRTOS-based microcontroller odometer built on the ESP32 TTGO v1.1 board, designed for bicycle or other vehicle applications. The device features a single sensor input that monitors a reed switch to detect wheel rotation. Measured data is displayed on an integrated TFT screen.

The hardware is housed in a 3D-printed enclosure, making it durable, easy to mount, and visually appealing. The device operates over a wide voltage range (3.3V – 5V), allowing power from USB or Li-ion batteries. Two onboard pushbuttons (GPIO0 and GPIO35) allow user interaction: display switching, reset, and wake-up from deep sleep.

It's simple to use and requires minimal wiring: only the reed contact needs to be connected to the appropriate GPIO pin. The software runs on FreeRTOS, enabling reliable multitasking.

---

## Features and Capabilities

### Main Features
1. **Real-time speed measurement** (km/h).
2. **Distance tracking**:
   - Daily (resettable) and total cumulative value (stored in NVS).
3. **Average speed calculation** based on movement time and distance.
4. **Maximum speed display** (not persistent after reset).
5. **Movement time** display (total active riding time).
6. **Graphical TFT display** with icons.
7. **Power-saving mode**:
   - Automatically enters deep sleep after 5 minutes of inactivity.
   - Wake-up via reed switch impulse or GPIO0 button.
8. **Display cycling button** (GPIO35): short press cycles through:
   - Current speed (km/h)
   - Daily distance (km)
   - Total distance (km)
   - Maximum speed (km/h)
   - Average speed (km/h)
   - Movement time (hh:mm)
9. **Daily counter reset**:
   - Long press (>1 second) on GPIO35 button.
10. **Simulation mode**:
    - Enable before compilation (`config.h`: `SIMULATE_REED_INPUT 1`)
    - Simulated speed: 14.5 km/h, duration: 3 minutes.

---

## Inputs and Outputs

### Inputs
- **Reed switch (GPIO26)**: detects wheel rotation.
- **Display/reset button (GPIO35)**:
  - Short press: change displayed value.
  - Long press: reset current value (if supported: e.g., daily distance, max speed, movement time).
- **Wake-up button (GPIO0)**: manual wake from deep sleep.

### Outputs
- **TFT display**: shows all measured and computed data.
- **Serial port**: used for debugging and logging (via ESP_LOG macros).

---

## Installation and Execution

### Required Components

- **Hardware**:
  - ESP32 TTGO v1.1 board with integrated TFT display.
  - Reed switch.
  - External pull-up resistor (10kΩ) for GPIO35.
  - Power supply (USB or battery).

- **Software**:
  - PlatformIO IDE.
  - `LovyanGFX` library.
  - ESP-IDF or Arduino framework.

### Installation Steps

1. Clone the repository:
   ```bash
   git clone https://github.com/your-repo/esp32TTGO_odometer.git
   cd esp32TTGO_odometer
   ```

2. Open the project in PlatformIO and install the required libraries.

3. In `config.h`, configure the appropriate wheel diameter (`WHEEL_DIAMETER_M`) and enable simulation mode if needed.

4. Connect the hardware:
   - Reed: GPIO26
   - Button (GPIO35): with external pull-up
   - Wake-up button (GPIO0): internal pull-up enabled

5. Build and upload the firmware to the ESP32 TTGO board.

6. Open the serial monitor for debugging output.

---

## Usage Instructions

1. **Upon power-up**, the TFT will automatically display the initial data (speed).
2. **Switch display mode**: short press on GPIO35 button.
3. **Reset displayed value**: long press (>1 second) on GPIO35 (daily distance, movement time, or max speed).
4. **Power saving**: enters deep sleep after 5 minutes of inactivity.
5. **Wake-up**: via reed impulse or GPIO0 button.

---

## Code Overview

### Main Components
- **`main.cpp`**: system initialization, task setup, deep sleep logic.
- **`displaytft.cpp`**: display updates, button handling, switching between displayed values.
- **`config.h`**: hardware configuration and simulation options.
- **FreeRTOS tasks**:
  - `guiTask`: handles screen updates and button events.
  - `calculation_and_control_task`: calculates speed and distance.
  - `reset_button_monitor_task`: handles long-press resets.
  - `reed_simulation_task`: simulates reed input (if enabled).

---

## Future Improvements

- **Bluetooth LE support** for mobile app integration.
- **GPS integration** for accurate location tracking.
- **Battery monitoring** and display of battery status.
- **Web interface** for remote monitoring and configuration.
- **Customizable display** layout and fields.

---

## Summary

The ESP32 TTGO Odometer is an easy-to-use, FreeRTOS-based system that provides accurate speed and distance data for cyclists or other vehicle use cases. Its compact 3D-printed enclosure, minimal wiring, and simulation option make it ideal for hobbyists, educational projects, or DIY dashboard integration.
