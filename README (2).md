# Robot Face Weather Station (ESP32-C3)

A small desk weather station built with an **ESP32-C3**, a **1.8" colour TFT**, a **DHT11 sensor** and **one touch sensor**.

It shows indoor temperature and humidity from the DHT sensor, and connects to WiFi to show the **real outdoor weather and rain chance** from an online forecast. The background looks like the real sky and changes with the time of day and the weather. Touch it and a little robot face reacts with different expressions.

> Video tutorial: https://youtu.be/DeoHOcDM1t4  
> Channel: **Maradi Innovations**

<!-- Add a photo of the finished project: ![Project photo](images/project.jpg) -->

---

## Features

- Indoor temperature and humidity (DHT11)
- Real outdoor temperature, weather condition and rain chance (WiFi, online forecast)
- Real time and date (NTP, internet time)
- Animated realistic backgrounds:
  - Sky colour follows the real clock: night, sunrise, day, sunset, dusk
  - Sun and moon travel across the sky, with hills and pine trees
  - Twinkling stars at night
  - Clear, partly cloudy, cloudy, fog, rain, thunderstorm and snow scenes
- Robot face with 5 expressions, reacting to touch
- Built-in demo mode to show every face and weather scene
- Runs on USB or a LiPo battery

Useful for a desk, greenhouse, farm, agriculture storage room or lab.

---

## Components

| Part | Quantity | Notes |
|---|---|---|
| ESP32-C3 board (SuperMini style) | 1 | Any ESP32-C3 board works, check pin names |
| 1.8" TFT display, ST7735, 128x160 (SPI) | 1 | 8-pin module |
| DHT11 temperature and humidity sensor | 1 | DHT22 also works (change one line in code) |
| Touch sensor module (TTP223 type) | 1 | Digital output |
| LiPo battery 3.7 V, 1000 mAh or more | 1 | Optional, for portable use |
| TP4056 charging module with protection | 1 | Recommended with the battery |
| Slide switch | 1 | Optional |
| PVC foam sheet | 1 | For the panel, easy to cut |
| Jumper wires, double-sided tape | - | |

---

## Connection Details

### TFT display to ESP32-C3

| TFT pin | ESP32-C3 pin |
|---|---|
| VCC | 3V3 |
| GND | GND |
| CS | GPIO 7 |
| SDA (MOSI) | GPIO 6 |
| A0 (DC) | GPIO 0 |
| SCK | GPIO 4 |
| RESET | GPIO 1 |
| LED (backlight) | 3V3 (same as VCC) |

### DHT11 sensor to ESP32-C3

| DHT11 pin | ESP32-C3 pin |
|---|---|
| VCC | 3V3 |
| GND | GND |
| Signal (DATA) | GPIO 3 |

### Touch sensor to ESP32-C3

| Touch sensor pin | ESP32-C3 pin |
|---|---|
| VCC | 3V3 |
| GND | GND |
| Signal (OUT) | GPIO 5 |

Place the touch sensor on the top side of the panel.

### Wiring summary

```
                    ESP32-C3
                 ┌─────────────┐
 TFT CS    ◄─────┤ GPIO 7      │
 TFT SDA   ◄─────┤ GPIO 6      │
 TFT A0    ◄─────┤ GPIO 0      │
 TFT SCK   ◄─────┤ GPIO 4      │
 TFT RESET ◄─────┤ GPIO 1      │
 DHT DATA  ◄─────┤ GPIO 3      │
 TOUCH OUT ──────► GPIO 5      │
                 │             │
 TFT VCC, TFT LED,             │
 DHT VCC, TOUCH VCC ◄────┤ 3V3 │
 All GND   ◄─────┤ GND         │
                 └─────────────┘
```

### Power (optional LiPo battery)

```
 LiPo +  ───► TP4056 B+          TP4056 OUT+ ──► [switch] ──► ESP32-C3 5V
 LiPo -  ───► TP4056 B-          TP4056 OUT- ───────────────► ESP32-C3 GND
 USB charger ───► TP4056 USB port (charging)
```

Warnings:

- **Never connect the LiPo to the 3V3 pin.** A full LiPo is 4.2 V and can damage the chip.
- Check polarity (red = positive, black = negative) before connecting. Reversed wires can destroy the board or the battery.
- A LiPo connected directly to the 5V pin works, but there is no charging and no protection. Use a protected battery or the TP4056 module.
- Keep the battery switch off while uploading code over USB.
- Do not charge a LiPo unattended. Do not use a swollen or damaged battery.

---

## Software Setup

### 1. Install Arduino IDE and the ESP32 board package

Install the Arduino IDE, then add the **esp32 by Espressif Systems** board package from the Boards Manager.

### 2. Install the libraries (Library Manager)

- Adafruit GFX Library (latest version)
- Adafruit ST7735 and ST7789 Library
- DHT sensor library (Adafruit) and Adafruit Unified Sensor
- ArduinoJson (version 7.x)

WiFi, HTTPClient, WiFiClientSecure and SPI come with the ESP32 package.

### 3. Open the code

The sketch is `robot_weather_station_v2.ino`. Keep it inside a folder with the same name:

```
robot_weather_station_v2/
└── robot_weather_station_v2.ino
```

### 4. Edit your settings (top of the code)

```cpp
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

const float LAT = 12.2958;          // your latitude  (set to Mysuru)
const float LON = 76.6394;          // your longitude
const long  GMT_OFFSET_SEC = 19800; // your time zone in seconds (India = 19800)

#define DHT_TYPE DHT11              // change to DHT22 if you use that sensor
```

### 5. Board settings (Tools menu)

| Setting | Value |
|---|---|
| Board | ESP32C3 Dev Module |
| USB CDC On Boot | Enabled |
| Port | the COM port of your ESP32-C3 |

### 6. Upload

Click Upload. When it finishes, press the RESET button once. The robot face appears, and after a few seconds the weather screen shows.

---

## How to Use

### Touch controls

| Action | Result |
|---|---|
| 1 touch | Happy |
| 2 touches | Love (heart eyes) |
| 3 touches | Surprised |
| 4 touches | Angry |
| 5 or more | Dizzy |
| No touch for 3 seconds | Weather screen |
| Touch on weather screen | Back to robot face |
| Hold touch for 1.5 seconds | Demo mode on/off |

### Demo mode

Hold the touch sensor for about 1.5 seconds. The demo shows all 5 faces, then 10 weather scenes: sunrise, sunny, partly cloudy, sunset, clear night, cloudy, rain, thunderstorm, snow and fog. A quick touch jumps to the next step. Hold again to exit.

### Weather screen

- Top: time and date, WiFi icon
- Middle: animated sky and the current condition
- Bottom: indoor temperature and humidity (DHT), outdoor temperature, rain chance with a bar

---

## How It Works

- **Weather:** the ESP32-C3 connects to WiFi and reads the current weather from the free [Open-Meteo](https://open-meteo.com/) forecast service. No API key is needed. It refreshes every 10 minutes. The rain chance is the highest value for the current hour and the next 2 hours.
- **Time:** the clock is set from internet time (NTP).
- **Display:** each frame is drawn in memory and then sent to the screen, so the animation does not flicker.
- **Sky:** the sky colour is calculated from the real time of day. The weather condition adds grey tint, rain, snow, fog or lightning.

---

## Troubleshooting

| Problem | Fix |
|---|---|
| Upload fails: "No serial data received" | Select the correct COM port (not COM1). Use a data USB cable. Hold the BOOT button while plugging in USB, then upload again. |
| Compile error about `setSPISpeed` | Update the Adafruit GFX library to the latest version. |
| Screen blank or white | Check the 8 TFT wires. LED pin must go to 3V3. Try `INITR_GREENTAB` or `INITR_REDTAB` instead of `INITR_BLACKTAB` in `setup()`. |
| Screen flickers white/black | Change `TFT_SPI_HZ` to `8000000`. Keep wires short. Add a 100 µF capacitor between 3V3 and GND near the display. |
| Colours wrong (red and blue swapped) | Try another `INITR_...` tab option, or `tft.invertDisplay(true)`. |
| Image shifted by a few pixels | Try `INITR_GREENTAB`. |
| Indoor values show `--` | Check the DHT wiring on GPIO 3 and that `DHT_TYPE` matches your sensor. |
| "No WiFi" or "Fetching..." stays | Check WiFi name and password. Use a 2.4 GHz network (ESP32-C3 does not support 5 GHz). |
| Time shows `--:--` | Wait a few seconds after WiFi connects. Check the internet connection. |
| Touch does not react | Check the signal wire on GPIO 5. Some modules are active LOW, so change `TOUCH_ACTIVE` to `LOW`. |

---

## Pin Settings in the Code

```cpp
#define TFT_SCK   4
#define TFT_MOSI  6    // display pin labelled SDA
#define TFT_CS    7
#define TFT_DC    0    // display pin labelled A0
#define TFT_RST   1
#define DHT_PIN   3
#define TOUCH_PIN 5
```

If your board is wired differently, change these numbers. Avoid GPIO 2, 8 and 9 (boot pins) and GPIO 18 and 19 (USB).

---

## Repository Contents

```
robot_weather_station_v2/
└── robot_weather_station_v2.ino   Main code
README.md                          This file
images/                            Photos and wiring pictures (add yours)
```

---

## Author

**Maradi Innovations**
DIY electronics, IoT, robotics and automation projects.

- GitHub: [github.com/Maradiranganath](https://github.com/Maradiranganath)
- YouTube: _add your channel link here_

If you like this project, please star the repository and subscribe to the channel. If you have any doubts, ask in the video comments.

## License

Add a `LICENSE` file (for example MIT) before publishing.
