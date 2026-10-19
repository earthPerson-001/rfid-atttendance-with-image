# RFID Attendance with Image Capture

ESP-IDF firmware (built with [PlatformIO](https://platformio.org/)) for an RFID
attendance terminal: an MFRC522 tag scan triggers an ESP32-CAM photo capture,
the JPEG is stored on a microSD card and uploaded over HTTPS as
`multipart/form-data` to a backend, so each attendance record can be verified
against a picture of the person who scanned in.

The firmware is event-driven (an `esp_event` base named `RFID_A_S_EVENTS`) and
uses FreeRTOS tasks pinned to separate cores so the camera feed never blocks the
scan → capture → upload pipeline.

Firmware updates are handled over the air ([see below](#firmware-updates-ota)):
on every boot the device checks an HTTPS manifest on the backend and, if a newer
build exists, installs it and reboots.

## How it works

1. Station-mode WiFi connects on boot (`include/wifi.h`).
2. A boot-time OTA check runs before anything else (highest-priority task) — see
   *Firmware updates (OTA)*.
3. An RFID scan posts `RFID_A_S_RFID_SCANNED` with the tag serial number
   (from the RC522 reader or a mocked scan while prototyping, see
   *Current status*).
4. The handler captures a JPEG frame from the camera (continuous frame-buffer
   mode) and hands it to the photo-registration task.
5. If `USE_SDCARD` is enabled, the image is written to SD as
   `/sdcard/images/<tag_serial>_<timestamp>.jpg`.
6. The backend is pinged; once reachable, `upload_jpeg_task` POSTs the frame
   (chunked, `multipart/form-data`, HTTPS) to `https://<SERVER_ADDRESS>/post`
   with the tag serial in an `rfid-serial-number` header.
7. `mock_server/server.py` accepts the upload, decodes the image with OpenCV
   and displays it, a stand-in until the real backend exists.

Core split and task priorities live in `include/globals.h`: camera feed is
pinned to core 1, photo registration and upload to core 0, and the OTA task runs
at the highest priority so an in-flight update is not interrupted.

## Firmware updates (OTA)

Over-the-air updates use ESP-IDF's `esp_https_ota`, driven by a JSON manifest
served by the same backend as the image uploads.

1. On boot the OTA task (`src/ota.c`, `start_ota_process()`, priority
   `OTA_TASK_PRIORITY` — above every other task) GETs
   `https://<SERVER_ADDRESS>/manifest.json`.
2. The manifest is parsed with cJSON and entries are filtered against:
   - `name` / `board` — which build the entry targets,
   - `build-type` — `alpha`, `beta`, or `stable`, matched against
     `CURRENT_OTA_CHANNEL` (`OTA_CHANNEL_APLHA` accepts all, `OTA_CHANNEL_BETA`
     accepts beta + stable, `OTA_CHANNEL_STABLE` only stable),
   - `criticality` — higher means *less* critical; entries above
     `OTA_ACCEPT_CRICICALITY_BELOW` are ignored,
   - `version` (semver `X.Y.Z`) — must be newer than the running firmware;
     entries whose `version-long` equals the running build are skipped (this is
     the fix for the earlier endless-update loop).
3. The best acceptable `firmware-url` is downloaded with `esp_https_ota` and the
   device reboots into the new image. The bootloader and running-firmware
   SHA-256 digests are logged before each check.

Manifest entries look like this (the build system writes them for you):

```json
[
  {
    "name": "rfid-attendance-with-image",
    "build-type": "alpha",
    "board": "esp32cam",
    "firmware-url": "esp32cam/esp32cam_esp32_firmware_v0.1.0-alpha-1-g70c0330.bin",
    "version-short": 0.1,
    "version": "0.1.0",
    "version-long": "v0.1.0-alpha-1-g70c0330",
    "criticality": 10
  }
]
```

**On build:** `mock_server/copy_firmware_for_ota.py`
(wired in through `extra_scripts = post:mock_server/copy_firmware_for_ota.py`)
runs `esptool elf2image` on the output ELF, writes the app image to
`mock_server/http_server_root/<board>/`, and appends/updates the entry in
`manifest.json`. The version string comes from `git describe`, so **annotated
git tags** drive the OTA version.

**Over HTTPS:** the client validates TLS. Leave `USE_CA_CERTIFICATE_BUNDLE` at
`0` to use the embedded dev certificate `server_certs/ca_cert.pem` (embedded via
`board_build.embed_txtfiles`, matched against `SERVER_COMMON_NAME`), or set it to
`1` to use the ESP-IDF certificate bundle for public servers. The mock server
serves `http_server_root/` over TLS with a matching self-signed certificate, so
it works with the embedded cert out of the box.

**Partition requirement:** OTA needs a dual-app layout. `partitions_custom.csv`
provides `ota_0` + `ota_1` + `otadata`; the 8 MB S3 layout
(`partitions_8MB_flash.csv`) is a single `factory` app with no OTA partitions, so
**OTA is not available on the `esp32-s3-devkitc-1` environment** as configured.

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

| Environment | Board | Flash layout | OTA |
|---|---|---|---|
| `az-delivery-devkit-v4` | AZ-Delivery ESP32 DevKit V4 | `partitions_custom.csv` | yes |
| `esp32doit-devkit-v1` | DOIT ESP32 DevKit v1 | `partitions_custom.csv` | yes |
| `esp32cam` | AI-Thinker ESP32-CAM | `partitions_custom.csv` | yes |
| `esp32-s3-devkitc-1` | ESP32-S3-DevKitC-1 (8 MB) | `partitions_8MB_flash.csv` | no (single `factory` app, no OTA partitions) |

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
- Peripheral selection: `USE_ESP32CAM` / `USE_RC522` / `USE_SDCARD` in `include/globals.h`
- Camera options: `pio run -e <env> --target menuconfig` ("Camera configuration" menu)
- OTA: `CURRENT_OTA_CHANNEL` and `OTA_ACCEPT_CRICICALITY_BELOW` in `include/ota.h`;
  TLS (`USE_CA_CERTIFICATE_BUNDLE`, `SERVER_COMMON_NAME`) and the CA in
  `include/globals.h` / `server_certs/ca_cert.pem` — see *Firmware updates (OTA)*

### 3. Build, flash, monitor

```sh
pio run -e esp32cam                 # build
pio run -e esp32cam -t upload       # flash
pio device monitor                  # 115200 baud
```

### 4. Mock server (local backend + OTA host)

```sh
cd mock_server
pip install -r requirements.txt     # numpy, opencv-python
python server.py                    # listens on 0.0.0.0:8000 (HTTPS), shows received images
```

On startup the server rewrites `SERVER_ADDRESS` in `include/globals.h` to your
machine's LAN IP, then serves `http_server_root/` over TLS — both the image
upload endpoint (`POST /post`) and the OTA manifest/firmware
(`GET /manifest.json`, `GET /<board>/...bin`).

### Wokwi

`wokwi.toml` points at the `az-delivery-devkit-v4` build. Build that
environment, then run it with the Wokwi extension/CLI.

## Project layout

```
src/
  main.c          boot: NVS, WiFi, OTA kick-off, camera/SD/RC522 init, event loop
  ota.c           manifest-driven HTTPS OTA (esp_https_ota) + version selection
  camera.c        camera init, frame capture, scan/photo event handlers, ping-gated upload
  rfid-rc522.c    RC522 polling -> RFID_A_S_RFID_SCANNED events
  upload.c        chunked multipart/form-data HTTP POST client
  wifi.c          station-mode WiFi (event-group based)
  sd-card.c       FAT mount + /sdcard/images/<tag>_<time>.jpg writes
  Kconfig.projbuild   per-sensor camera configuration
include/          headers incl. globals.h (flags, task/core layout), ota.h, events.h
components/       submodules: esp-idf-rc522, esp32-camera
server_certs/     ca_cert.pem embedded into the firmware for OTA TLS
mock_server/
  server.py       Python test backend + HTTPS OTA host (OpenCV image display)
  copy_firmware_for_ota.py   post-build hook: emit .bin + update manifest.json
  http_server_root/          served OTA root (manifest.json, <board>/*.bin, certs)
partitions_*.csv  flash layouts (4 MB dual-OTA / 8 MB S3)
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
- OTA is implemented and merged (manifest check on boot, TLS download, reboot).
  `manifest.json` ships empty, so nothing updates until a tagged build publishes
  an entry. The mock server's certificate is a self-signed dev cert; a real
  deployment should use `USE_CA_CERTIFICATE_BUNDLE` with a trusted server.
- OTA does not work on `esp32-s3-devkitc-1` until its partition table gets
  `otadata` + two app slots.
