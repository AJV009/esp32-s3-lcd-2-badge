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
- I2C: SCL=47, SDA=48 (IMU and Touch Panel)
- Serial: TXD=43, RXD=44
- USB: D-=19, D+=20 (occupied, not available for GPIO)

## Repository Structure

```
badge/
├── workbench/
│   ├── working_protos/        # Production-ready prototypes
│   │   ├── 00_video_loop*/    # MJPEG video player variants
│   │   ├── 01_llm_inference*/ # On-device LLM (llama2.c port)
│   │   └── 02_speaker_mic_combo/ # Audio I/O with downmix
│   ├── tests/                 # Experiments and PoCs
│   │   ├── wakeword_*_test/   # microWakeWord detection
│   │   ├── yamnet_*_ipynb_poc/ # YAMNet audio embeddings
│   │   ├── llm_qa_memorization_ipynb_poc/ # Q&A fine-tuning
│   │   └── *_mic_*/speaker_*/ # Audio hardware tests
│   └── docs/                  # Schematics, datasheets
│       └── WaveShare-ESP32-S3-LCD-2-Demo/ # Vendor reference
└── ~/Arduino/libraries/       # External Arduino libraries
    ├── Arduino_GFX_Library/   # Display driver
    ├── JPEGDEC/               # JPEG decoder
    ├── EdgeNeuron/            # TFLite inference (wake word)
    ├── TensorFlowLite_ESP32/  # TFLite (YAMNet, larger models)
    ├── FastIMU/               # QMI8658 IMU support
    ├── lvgl/                  # LVGL GUI library
    └── OneButton/             # Button handling
```

## Development Commands

### Arduino IDE

**IMPORTANT: Arduino Sketch Requirements:**
- Each `.ino` file MUST be in a folder with the same name
- Example: `my_sketch.ino` must be in folder `my_sketch/`
- Incorrect structure will cause "Sketch not found" errors

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

### Video Player Architecture (`working_protos/00_video_loop*`)

Optimized MJPEG player variants with minimal code footprint.

**Design Pattern:**
- **MemoryStream class**: Minimal Stream implementation for PSRAM buffer access
- **VideoPlayer class**: Encapsulates display, flash I/O, and MJPEG decoding
- **Singleton pattern**: Static instance for JPEG callback access
- **RAII initialization**: All setup in `begin()`, simple `play()` for loop

**Variants:**
- `00_video_loop/` - Base video loop
- `00_video_loop_btn_pause/` - Button pause/resume
- `00_video_loop_btn_pause_gyro_rotate/` - IMU-based orientation

### LLM Inference Architecture (`working_protos/01_llm_inference*`)

Port of llama2.c for on-device text generation.

**Files:**
- `llm_core.cpp/h` - Transformer forward pass, attention, RoPE
- `tokenizer.cpp/h` - BPE tokenizer (SentencePiece format)
- `sampler.cpp/h` - Temperature/top-p sampling
- `sd_data/` - Model and tokenizer binaries for SD card

**Model Specs:**
- `stories15m` - 15M param TinyStories (~15MB)
- `stories260k` - 260K param TinyStories (~300KB, faster)

**Usage:** Load model from SD card to PSRAM, generate token-by-token.

### Wake Word Detection (`tests/wakeword_pretrained_test/`)

microWakeWord-based detection using EdgeNeuron TFLite runtime.

**Pipeline:**
1. INMP441 mic → 16kHz audio capture
2. Microfrontend → 40-bin log-mel spectrogram (30ms window, 10ms stride)
3. EdgeNeuron → TFLite inference every 10ms
4. Sliding window → Probability averaging → Detection trigger

**Configuration:** JSON config on SD card controls thresholds:
```json
{"probability_cutoff": 0.5, "sliding_window_average_size": 10, ...}
```

**Training:** See `tests/wakeword_training_ipynb_poc/` for custom wake word training via Colab.

### Audio Embedding Architecture (`tests/yamnet_audio_embedding_ipynb_poc/`)

YAMNet-based 1024-D audio embeddings on ESP32-S3.

**Pipeline:**
1. Dual INMP441 mics → stereo capture → mono downmix
2. ESP-DSP accelerated FFT → 64-mel × 96-frame spectrogram
3. TensorFlowLite_ESP32 → YAMNet inference (dual-core optimized)
4. Extract 1024-D embedding → JSON output

**Performance:** ~10-14 seconds total (inference is the bottleneck at 6-10s).

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

### Dual I2S Audio Architecture

ESP32-S3 supports separate I2S buses for simultaneous input/output:

```cpp
// I2S0 for microphones (RX)
i2s_config_t mic_config = {
  .mode = I2S_MODE_MASTER | I2S_MODE_RX,
  .sample_rate = 16000,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // INMP441 sends 24-bit in 32-bit frame
  .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
  // ...
};
i2s_driver_install(I2S_NUM_0, &mic_config, 0, NULL);

// I2S1 for speaker (TX)
i2s_config_t spk_config = {
  .mode = I2S_MODE_MASTER | I2S_MODE_TX,
  .sample_rate = 16000,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  // ...
};
i2s_driver_install(I2S_NUM_1, &spk_config, 0, NULL);
```

See `workbench/working_protos/02_speaker_mic_combo/` for full duplex example.

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

## GPIO Pin Availability

Based on ESP32-S3-Touch-LCD-2 schematic **PinOut** section:

**Pins broken out to headers:**
- IO2, IO4, IO6-18, IO21, IO43, IO44, IO47, IO48

**Truly free pins** (no peripherals attached):
- **GPIO 18** - Completely free
- **GPIO 2, 4, 6-17, 21** - Camera pins (safe if camera not attached)

**Occupied pins:**
- GPIO 0 (BOOT), 1 (BL), 5 (BAT) - System
- GPIO 19, 20 - USB D-, D+ (**not available**)
- GPIO 33-37 - **NOT broken out** to headers
- GPIO 38-42, 45 - SPI (LCD/SD card)
- GPIO 43, 44 - Serial console (U0_TXD/RXD)
- GPIO 46 - Touch panel interrupt
- GPIO 47, 48 - I2C (IMU and touch panel)

## Audio Hardware

### Microphone (INMP441)
- **Pins:** GPIO 2 (BCK), 4 (WS), 18 (DIN)
- **I2S Port:** I2S_NUM_0 (RX mode)
- **Config:** Stereo capable, 24-bit samples, 16kHz sample rate
- **Test sketch:** `workbench/mic_pin_test/` or `workbench/single_mic_test/`

### Speaker (MAX98357A)
- **Pins:** GPIO 6 (BCLK), 7 (LRC), 8 (DIN)
- **I2S Port:** I2S_NUM_1 (TX mode)
- **Power:** 5V recommended for full power (3-5W capable)
- **Config:** 16-bit samples, 44.1kHz sample rate, mono/stereo
- **Test sketch:** `workbench/tests/speaker_beep_test/`
- **Note:** GAIN pin controls volume (Float=9dB, GND=12dB, VDD=15dB)

## ML Model Deployment

### SD Card Layout for Models
```
SD:/
├── models/
│   ├── hey_daisy.tflite     # Wake word model (~130KB)
│   └── hey_daisy.json       # Detection config
├── yamnet.tflite            # Audio embedding model (167KB-4MB)
├── stories260k.bin          # LLM model weights
└── tokenizer.bin            # LLM tokenizer
```

### TensorFlow Lite Libraries
- **EdgeNeuron**: Lightweight, good for small streaming models (wake word)
- **TensorFlowLite_ESP32**: Full-featured, needed for larger models (YAMNet)

### Colab Training Pipelines
- `wakeword_training_ipynb_poc/wakeword_training.ipynb` - Custom wake word (~30-60 min)
- `llm_qa_memorization_ipynb_poc/qa_memorization_poc.ipynb` - Fine-tune tiny LLM
- `yamnet_audio_embedding_ipynb_poc/` - Audio embedding extraction
