# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-S3-LCD-2 conference badge firmware for OAISYS25. Voice-activated badge with wake word detection, audio embedding similarity search, on-device LLM inference, and robotic TTS output.

## Hardware Target

**ESP32-S3-LCD-2 Board (Waveshare):**
- 240x320 ST7789 LCD (SPI), QMI8658 IMU (I2C), SD card, 16MB Flash, 8MB PSRAM

**Pin Map:**
| Function | Pins |
|----------|------|
| LCD | CS=45, DC=42, BL=1, SCK=39, MOSI=38, MISO=40 |
| SD Card | CS=41 (shares SPI bus with LCD) |
| I2C | SCL=47, SDA=48 |
| Mic (INMP441) | BCK=2, WS=4, DIN=18 |
| Speaker (MAX98357A) | BCLK=6, LRC=7, DIN=8 |
| Serial | TX=43, RX=44 |
| **Unavailable** | GPIO 19, 20 (USB D-/D+) |

## Repository Structure

```
badge/
├── local_llm_badge/           # ★ MAIN UNIFIED FIRMWARE ★
│   ├── local_llm_badge.ino    # Main sketch - state machine
│   ├── config.h               # Pin definitions, constants
│   ├── src/                   # Arduino compiles recursively
│   │   ├── display/           # video_player
│   │   ├── sensors/           # orientation (IMU)
│   │   ├── audio/             # mic_stream, audio_recorder
│   │   ├── ml/                # wake_word, audio_embed, llm_*, embed_search
│   │   └── tts/               # robot_tts (SAM)
│   └── sd_data/               # Files to copy to SD card
├── workbench/
│   ├── working_protos/        # Reference implementations (00-05)
│   ├── tests/                 # Hardware tests and training notebooks
│   └── docs/
│       ├── OAISYS_BADGE_PLAN.md   # Architecture plan
│       └── OAISYS_BADGE_TODO.md   # Progress tracker
└── ~/Arduino/libraries/       # Required: Arduino_GFX, JPEGDEC, EdgeNeuron,
                               # TensorFlowLite_ESP32, FastIMU, SAM
```

## Development Commands

### Arduino IDE Build
```bash
# Board: ESP32S3 Dev Module
# Flash: 16MB, Partition: 16MB Flash (3MB APP/9MB FATFS)
# PSRAM: OPI PSRAM
# Upload Speed: 921600
```

### arduino-cli (Headless Build)
```bash
# Compile
arduino-cli compile --fqbn esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi \
  local_llm_badge/

# Upload
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32s3 local_llm_badge/

# Monitor
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

### Upload Data to FFat Partition
```bash
cd workbench/working_protos/00_video_loop
./upload_to_flash.sh  # Requires mkfatfs and esptool.py
# Flashes data/ folder to offset 0x611000
```

### ESP-IDF (Alternative)
```bash
cd workbench/docs/WaveShare-ESP32-S3-LCD-2-Demo/ESP-IDF/<example>
idf.py build && idf.py flash monitor
```

**Arduino Sketch Rule:** `.ino` file MUST be in folder with same name (e.g., `my_sketch/my_sketch.ino`)

## Code Architecture

### State Machine (`local_llm_badge/local_llm_badge.ino`)

The main firmware implements this flow:
```
BOOT → LOGO_LOOP ←→ DEEP_SLEEP (5min idle)
         ↓ (wake word: "Hey Daisy")
      RECORDING (3-5 sec, RED screen)
         ↓
      EMBEDDING → SIMILARITY
         ↓
   score ≥ 0.7 → LLM_INFERENCE → DISPLAY → TTS → LOGO_LOOP
   score < 0.7 → TTS_SORRY → STASH_DATA → LOGO_LOOP
```

### Module Pattern

All modules follow `begin()`/`end()` with memory pool for sequential model loading:
```cpp
class MLModule {
    bool begin(uint8_t* pool, size_t size);  // Load to shared 6MB pool
    void end();                               // Free for next model
};
```

### Key Implementations

| Component | Location | Pattern |
|-----------|----------|---------|
| Video Player | `src/display/video_player.*` | MemoryStream + JPEGDEC callback |
| Wake Word | `src/ml/wake_word.*` | EdgeNeuron + sliding window |
| Audio Embed | `src/ml/audio_embed.*` | ESP-DSP FFT + TFLite CNN |
| LLM | `src/ml/llm_core.*` | llama2.c port, Q8_0 quantized |
| TTS | `src/tts/robot_tts.*` | ESP32-SAM at 22050Hz |
| Orientation | `src/sensors/orientation.*` | QMI8658 via FastIMU |

### Reference Prototypes

Working examples in `workbench/working_protos/`:
- `00_video_loop*` - MJPEG playback variants
- `01_llm_inference*` - LLM text generation
- `02_speaker_mic_combo` - Dual I2S audio
- `03_custom_wakeword` - Wake word detection
- `04_yamnet_audio_embedding` - Audio embeddings
- `05_llm_finetuned` - Fine-tuned Q8_0 LLM

## Memory & Partitions

### PSRAM Budget (~8MB)
- Video buffer: ~2MB
- ML Pool: 6MB (shared, one model at a time)
- Embedding DB: ~300KB

### FFat Partition (16MB Flash)
```
Offset     Size       Name
0x610000   0x9E0000   ffat (9.9MB)
```
**Flash at 0x611000** (+0x1000 from partition start)

## Common Patterns

### Dual I2S Audio
```cpp
// I2S0: Microphones (RX), 16kHz, 32-bit (INMP441 sends 24-bit in 32-bit frame)
// I2S1: Speaker (TX), 22050Hz for SAM TTS
```

### SPI Bus Sharing (LCD + SD)
```cpp
// Use semaphore when shared: bsp_spi_lock(-1) / bsp_spi_unlock()
```

### PSRAM Allocation
```cpp
ps_malloc(size);   // Large buffers (>100KB)
malloc(size);      // Small work buffers
```

## SD Card Layout
```
SD:/
├── config.json              # Runtime config (WiFi, thresholds)
├── media/logo.mjpeg         # Boot animation
├── models/
│   ├── wake_word.tflite     # 130KB
│   ├── audio_encoder.tflite # 100-300KB (custom CNN)
│   ├── llm_model.bin        # 6MB (Q8_0)
│   └── tokenizer.bin        # 13KB
└── data/
    ├── embeddings.bin       # 300×256 float32
    └── intents.txt          # Intent strings
```

## Implementation Progress

See `workbench/docs/OAISYS_BADGE_TODO.md` for current status.

| Phase | Status |
|-------|--------|
| 1: Video + Gyro | COMPLETE |
| 2: Wake Word | COMPLETE |
| 3: Embedding | IN PROGRESS (training) |
| 4: LLM | COMPLETE |
| 5: TTS (SAM) | COMPLETE |
| 6-8: Sleep/WiFi/Stash | PENDING |

## Design Principles

1. **Single model in memory** - Sequential load/unload to 6MB pool
2. **No debug spam** - Production builds remove Serial.print()
3. **Encapsulation** - Classes with begin()/end() pattern
4. **Fail-fast** - Return values, minimal error screens
5. **SD-first** - Models and config on SD card, not flash
