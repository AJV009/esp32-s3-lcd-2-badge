# OAISYS25 Badge Firmware

Unified firmware integrating all badge functionality.

> **Plan:** `workbench/docs/OAISYS_BADGE_PLAN.md`
> **Progress:** `workbench/docs/OAISYS_BADGE_TODO.md`

## Phase 2: Wake Word + Recording (Current)
- Video loop playback (MJPEG from SD card)
- Gyro-based screen rotation
- Wake word detection ("Hey Daisy")
- Audio recording after wake word trigger

## Hardware
- ESP32-S3-LCD-2 (Waveshare)
- ST7789 240x320 LCD
- QMI8658 IMU
- INMP441 Mics (x2)
- MAX98357A Speaker
- SD Card (for media, models, config)

## Building
1. Open `oaisys_badge.ino` in Arduino IDE 2.x
2. Select Board: ESP32S3 Dev Module
3. Settings:
   - Flash: 16MB
   - Partition: 16MB Flash (3MB APP/9MB FATFS)
   - PSRAM: OPI PSRAM
4. Upload sketch
5. Copy `sd_data/*` contents to SD card root

## Controls (Phase 2)
- **Wake word**: Say "Hey Daisy" to start recording
- Recording shows RED screen, completion shows GREEN
- Auto-returns to video loop after 2 seconds

## Directory Structure
```
oaisys_badge/
├── oaisys_badge.ino    # Main sketch
├── config.h            # Pin definitions
├── src/                # Source files (Arduino compiles recursively)
│   ├── display/
│   │   └── video_player.*  # Video + text overlay
│   ├── sensors/
│   │   └── orientation.*   # IMU rotation
│   ├── audio/              # (Phase 2)
│   ├── ml/                 # (Phase 3-4)
│   ├── tts/                # (Phase 5)
│   ├── network/            # (Phase 7)
│   └── storage/            # (Phase 7-8)
└── sd_data/            # Files for SD card
```

**Note:** Arduino IDE 1.6.10+ recursively compiles `.cpp` files in `src/` subfolder.

## SD Card Layout
```
SD:/
├── config.json           # WiFi + thresholds
├── media/
│   └── logo.mjpeg        # Boot animation (Phase 1)
├── models/               # ML models (Phase 2+)
│   ├── wake_word.tflite
│   ├── wake_word.json
│   ├── yamnet.tflite
│   ├── projection.tflite
│   ├── llm_model.bin
│   └── tokenizer.bin
├── data/                 # Embeddings (Phase 3+)
│   ├── embeddings.bin
│   └── intents.txt
└── stash/                # Auto-created for unrecognized audio
```
