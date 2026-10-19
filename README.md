# RFID Attendance with Image Capture

ESP-IDF firmware (built with [PlatformIO](https://platformio.org/)) for an RFID
attendance terminal: an MFRC522 tag scan triggers an ESP32-CAM photo capture,
the JPEG is stored on a microSD card and uploaded over HTTP as
`multipart/form-data` to a backend, so each attendance record can be verified
against a picture of the person who scanned in.

The firmware is event-driven (an `esp_event` base named `RFID_A_S_EVENTS`) and
uses FreeRTOS tasks pinned to separate cores so the camera feed never blocks the
scan → capture → upload pipeline.

## How it works

1. Station-mode WiFi connects on boot (`include/wifi.h`).
2. An RFID scan posts `RFID_A_S_RFID_SCANNED` with the tag serial number
   (from the RC522 reader or a mocked scan while prototyping, see
   *Current status*).
3. The handler captures a JPEG frame from the camera (continuous frame-buffer
   mode) and hands it to the photo-registration task.
4. The image is written to SD as `/sdcard/images/<tag_serial>_<timestamp>.jpg`.
5. The backend is pinged; once reachable, `upload_jpeg_task` POSTs the frame
   (chunked, `multipart/form-data`) to `http://<SERVER_ADDRESS>/post` with the
   tag serial in an `rfid-serial-number` header.
6. `mock_server/server.py` accepts the upload, decodes the image with OpenCV
   and displays it, a stand-in until the real backend exists.

Core split and task priorities live in `include/globals.h`: camera feed is
pinned to core 1, photo registration and upload to core 0.

## Hardware

- ESP32-CAM (OV2640 by default; other sensors selectable via Kconfig) or a
  plain ESP32 dev board
- MFRC522 RFID reader (SPI)
- microSD card (SDMMC)
- Optional simulation: Wokwi (`diagram.json` + `wokwi.toml`, ESP32 DevKit C)

**Wiring note:** on the current prototype the RC522 and the camera share the
SPI bus, so a build enables one of `USE_ESP32CAM` / `USE_RC522` in
`include/globals.h`.

## Supported boards (PlatformIO environments)

| Environment | Board | Flash layout |
|---|---|---|
| `az-delivery-devkit-v4` | AZ-Delivery ESP32 DevKit V4 | `partitions_custom.csv` |
| `esp32doit-devkit-v1` | DOIT ESP32 DevKit v1 | `partitions_custom.csv` |
| `esp32cam` | AI-Thinker ESP32-CAM | `partitions_custom.csv` |
| `esp32-s3-devkitc-1` | ESP32-S3-DevKitC-1 (8 MB) | `partitions_8MB_flash.csv` |

## Getting started

### 1. Clone with submodules

`components/esp-idf-rc522` and `components/esp32-camera` are git submodules.

```sh
git clone --recurse-submodules git@github.com:earthPerson-001/rfid-atttendance-with-image.git
cd rfid-atttendance-with-image
# if you already cloned without them:
git submodule update --init --recursive
```

### 2. Configure

- WiFi credentials: `EXAMPLE_ESP_WIFI_SSID` / `EXAMPLE_ESP_WIFI_PASS` in `include/wifi.h`
- Backend address: `SERVER_ADDRESS` in `include/globals.h` (default `192.168.1.107:8000`);
  the mock server rewrites this line to your machine's LAN IP on startup
- Peripheral selection: `USE_ESP32CAM` / `USE_RC522` in `include/globals.h`
- Camera options: `pio run -e <env> --target menuconfig` ("Camera configuration" menu)

### 3. Build, flash, monitor

```sh
pio run -e esp32cam                 # build
pio run -e esp32cam -t upload       # flash
pio device monitor                  # 115200 baud
```

### 4. Mock server (local backend)

```sh
cd mock_server
pip install -r requirements.txt     # numpy, opencv-python
python server.py                    # listens on 0.0.0.0:8000, shows received images
```

### Wokwi

`wokwi.toml` points at the `az-delivery-devkit-v4` build. Build that
environment, then run it with the Wokwi extension/CLI.

## Project layout

```
src/
  main.c          boot: NVS, WiFi, camera/SD/RC522 init, event loop
  camera.c        camera init, frame capture, scan/photo event handlers, ping-gated upload
  rfid-rc522.c    RC522 polling -> RFID_A_S_RFID_SCANNED events
  upload.c        chunked multipart/form-data HTTP POST client
  wifi.c          station-mode WiFi (event-group based)
  sd-card.c       FAT mount + /sdcard/images/<tag>_<time>.jpg writes
  Kconfig.projbuild   per-sensor camera configuration
include/          headers incl. globals.h (mode flags, task/core layout) and events.h
components/       submodules: esp-idf-rc522, esp32-camera
mock_server/      Python test backend (OpenCV image display)
partitions_*.csv  flash layouts (4 MB custom / 8 MB S3)
diagram.json, wokwi.toml   Wokwi simulation
```

## Current status

Work in progress: known open items:

- With `USE_ESP32CAM` (current default) RFID scans are **mocked** periodically
  to exercise the capture → save → upload pipeline, since RC522 and camera
  share SPI on the prototype wiring.
- The multipart upload can be sent multiple times per request (open bug).
- Earlier image-corruption issues during upload motivated PSRAM/SPIFFS
  buffering; mitigated but not fully closed.
