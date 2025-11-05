# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESP32-S3-LCD-2 development repository for creating OAISYS25 conference badge firmware. The project focuses on optimized video playback and hardware interfacing with the Waveshare ESP32-S3-LCD-2 development board.

## Hardware Target

**ESP32-S3-LCD-2 Board:**
- 240x320 ST7789 LCD display (SPI)
- QMI8658 IMU sensor (I2C)
- Camera support
- SD card slot
- 16MB Flash with FFat partition layout

**Key Pins:**
- LCD: CS=45, DC=42, BL=1, SCK=39, MOSI=38, MISO=40
- SPI shared with SD card and LCD

## Repository Structure

```
badge/
├── workbench/
│   ├── working_protos/        # Active development prototypes
│   │   └── video_loop/        # Optimized MJPEG video player (103 lines)
│   └── ESP32-S3-LCD-2-Demo/   # Reference examples from Waveshare
│       ├── Arduino/           # Arduino framework examples
│       │   ├── examples/01_factory/  # Full-featured factory demo
│       │   └── libraries/     # Bundled LVGL library
│       └── ESP-IDF/           # ESP-IDF framework examples
└── /home/alphons/Arduino/libraries/  # External Arduino libraries
    ├── Arduino_GFX_Library/   # Display driver
    ├── JPEGDEC/               # JPEG decoder
    ├── FastIMU/               # QMI8658 IMU support
    ├── lvgl/                  # LVGL GUI library
    └── OneButton/             # Button handling
```

## Development Commands

### Arduino IDE

**Build and Upload:**
```bash
# Use Arduino IDE 2.x or arduino-cli
# Select Board: ESP32S3 Dev Module
# Partition Scheme: 16MB Flash (3MB APP/9MB FATFS) - from app3M_fat9M_16MB.csv
# PSRAM: OPI PSRAM
```

**Upload Data to FFat Partition:**
```bash
cd workbench/working_protos/video_loop
./upload_to_flash.sh
```

This script:
1. Packages `data/` folder using mkfatfs
2. Flashes to FFat partition at offset 0x611000 (0x610000 + 0x1000)
3. Requires `esptool.py` and `mkfatfs` tools

**Serial Monitor:**
```bash
# Most sketches use 115200 baud
```

### ESP-IDF Framework

```bash
cd workbench/ESP32-S3-LCD-2-Demo/ESP-IDF/<example_name>
idf.py build
idf.py flash monitor
```

## Code Architecture

### Video Player Architecture (`working_protos/video_loop`)

This is the primary working prototype - heavily optimized from 250+ lines to 103 lines.

**Design Pattern:**
- **MemoryStream class**: Minimal Stream implementation for PSRAM buffer access
- **VideoPlayer class**: Encapsulates display, flash I/O, and MJPEG decoding
- **Singleton pattern**: Static instance for JPEG callback access
- **RAII initialization**: All setup in `begin()`, simple `play()` for loop

**Workflow:**
1. `setup()`: Call `player.begin()` - initializes display, mounts FFat, loads `/output.mjpeg` to PSRAM
2. `loop()`: Call `player.play()` - decodes next frame, auto-loops on EOF

**Memory Management:**
- Video loaded to PSRAM via `ps_malloc()` at startup
- Decode buffer in heap: `320 * 240 / 2` bytes
- No reloading during playback - pure memory streaming

### Factory Demo Architecture (`ESP32-S3-LCD-2-Demo/Arduino/examples/01_factory`)

Multi-module reference implementation showing full hardware capabilities.

**BSP Layer (Board Support Package):**
- `bsp_spi.h/.cpp`: Shared SPI bus with semaphore protection
- `bsp_i2c.h/.cpp`: I2C bus for IMU
- `bsp_lv_port.h/.cpp`: LVGL display driver integration
- `bsp_button.h/.cpp`: Hardware button handling

**App Layer:**
- `app_qmi8658`: IMU data reading and visualization
- `app_system`: System monitoring (battery, brightness)
- `app_camera`: Camera integration
- `app_wifi`: WiFi scanning and connection

**UI Layer:**
- `lvgl_ui/`: LVGL tabview UI
- Each tab corresponds to an app module

**Initialization Sequence:**
```cpp
setup() {
  bsp_i2c_init();      // I2C bus
  bsp_lv_port_init();  // Display + LVGL
  bsp_spi_init();      // Shared SPI
  bsp_button_init();   // Buttons

  lvgl_ui_init();      // UI components

  app_*_init();        // Module initialization
  app_*_run();         // Start module tasks
}
```

### Key Libraries

**Arduino_GFX_Library:**
- Hardware abstraction for ST7789 display
- SPI bus configuration: `Arduino_ESP32SPI`
- Display driver: `Arduino_ST7789` (240x320, IPS mode)

**LVGL (Light and Versatile Graphics Library):**
- Must configure `lv_conf.h` before use
- Factory example uses LVGL 8.x API
- Thread safety via `lvgl_lock()`/`lvgl_unlock()`

**JPEGDEC:**
- Software JPEG decoder
- Used by MjpegClass for frame-by-frame decoding
- Callback-based rendering: `JPEG_DRAW_CALLBACK`

**MjpegClass:**
- Custom MJPEG container parser (not a library)
- Searches for JPEG markers (0xFFD8/0xFFD9) in stream
- Extracts individual frames for JPEGDEC

## FFat Partition Layout

From `app3M_fat9M_16MB.csv`:
```
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x5000
otadata,  data, ota,     0xe000,  0x2000
app0,     app,  ota_0,   0x10000, 0x300000
app1,     app,  ota_1,   0x310000,0x300000
ffat,     data, fat,     0x610000,0x9E0000
```

**Important:** Flash FFat images at offset **0x611000** (+0x1000 from partition start)

## PSRAM Usage

**Enabled via Arduino IDE settings:** OPI PSRAM

**Allocation:**
```cpp
uint8_t* buffer = (uint8_t*)ps_malloc(size);  // PSRAM
uint8_t* buffer = (uint8_t*)malloc(size);     // Heap
```

**Best Practice:**
- Large buffers (>100KB): Use PSRAM
- Frame buffers, video data: PSRAM
- Small work buffers: Heap

## Common Patterns

### SPI Bus Sharing

Multiple devices (LCD, SD card) share SPI bus - must use semaphore:

```cpp
if (bsp_spi_lock(-1)) {
  // SPI operations
  bsp_spi_unlock();
}
```

### LVGL Thread Safety

All LVGL calls must be protected:

```cpp
if (lvgl_lock(-1)) {
  // LVGL UI updates
  lvgl_unlock();
}
```

### Stream-based File I/O

Prefer Stream abstraction for memory/file interchangeability:

```cpp
class MemoryStream : public Stream {
  // Implement available(), read(), peek(), readBytes()
};
```

## Optimization Philosophy

The `video_loop` prototype demonstrates aggressive optimization:

**Removed:**
- All Serial.print() debug output
- Verbose error screens
- Statistics tracking
- Redundant comments and temporary variables

**Retained:**
- All functionality
- Error detection (via return values)
- Minimal LED feedback for critical failures

**Result:** 59% code reduction (250→103 lines) with zero performance impact

When modifying code, prioritize:
1. Encapsulation over globals
2. Classes over procedural code
3. Minimal interfaces (begin/run pattern)
4. Fail-fast error handling
5. Production code = no debug spam
