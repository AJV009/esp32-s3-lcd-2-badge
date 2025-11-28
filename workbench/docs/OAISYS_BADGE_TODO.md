# OAISYS25 Badge - Implementation Progress Tracker

> **Plan Document:** [OAISYS_BADGE_PLAN.md](./OAISYS_BADGE_PLAN.md)
> **Main Firmware:** `workbench/working_protos/oaisys_badge/`

---

## Overall Progress

| Phase | Description | Status | Notes |
|-------|-------------|--------|-------|
| Phase 1 | Core (Video + Text + Gyro) | **COMPLETE** | Tested, working |
| Phase 2 | Wake Word + Recording | **IN PROGRESS** | Code complete, needs testing |
| Phase 3 | Embedding + Similarity | PENDING | |
| Phase 4 | LLM Integration | PENDING | |
| Phase 5 | Robotic TTS (SAM) | PENDING | |
| Phase 6 | Deep Sleep + Wake | PENDING | |
| Phase 7 | WiFi + Model Updates | PENDING | |
| Phase 8 | Stash Upload | PENDING | |
| Backend | FastAPI + Redis + UI | PENDING | |

---

## Phase 1: Core Integration - COMPLETE

### Completed Tasks
- [x] Create `oaisys_badge/` directory structure
- [x] Create `config.h` with all pin definitions
- [x] Adapt VideoPlayer from `00_video_loop_btn_pause_gyro_rotate`
- [x] Implement text overlay system (Matrix green theme)
- [x] Adapt OrientationManager for gyro rotation
- [x] Create main state machine skeleton
- [x] Create SD card config.json template
- [x] Copy logo.mjpeg to sd_data/media/
- [x] Create README documentation
- [x] Update video loading to use SD card instead of FFat

### Files Created
```
oaisys_badge/
├── oaisys_badge.ino           # Main sketch
├── config.h                   # Pin definitions
├── README.md                  # Build instructions
├── src/                       # Arduino compiles src/ recursively
│   ├── display/
│   │   ├── video_player.h
│   │   └── video_player.cpp
│   └── sensors/
│       ├── orientation.h
│       └── orientation.cpp
└── sd_data/
    ├── config.json
    └── media/
        └── logo.mjpeg
```

### Test Checklist
- [x] Build compiles without errors
- [x] Logo plays smoothly (30+ FPS)
- [x] Text overlay displays correctly
- [x] Tilting rotates display (gyro)

---

## Phase 2: Wake Word + Recording - IN PROGRESS

### Completed Tasks
- [x] Create `audio/mic_stream.cpp/h` - Continuous I2S mic input with microfrontend
- [x] Create `ml/wake_word.cpp/h` - TFLite wake word detection with sliding window
- [x] Create `audio/audio_recorder.cpp/h` - Fixed duration recording to PSRAM
- [x] Implement memory pool for ML model swapping (6MB PSRAM pool)
- [x] Add wake word state to state machine
- [x] Integrate into main sketch with wake word triggering

### Pending Tasks
- [ ] Test: "Hey Daisy" triggers recording
- [ ] Copy wake word model to SD card (`/models/wake_word.tflite`)
- [ ] Copy wake word config to SD card (`/models/wake_word.json`)

### Files Created
```
src/audio/
├── mic_stream.cpp/h       # I2S capture + microfrontend feature extraction
└── audio_recorder.cpp/h   # Fixed-duration recording to PSRAM buffer
src/ml/
└── wake_word.cpp/h        # TFLite inference + sliding window detection
```

### SD Card Requirements
Copy from `03_custom_wakeword/sd_data/models/`:
- `hey_daisy.tflite` → `/models/wake_word.tflite`
- `hey_daisy.json` → `/models/wake_word.json`

### Test Checklist
- [ ] Video plays while wake word listens
- [ ] "Hey Daisy" triggers recording state
- [ ] Recording shows RED screen
- [ ] Recording completes after 3 seconds
- [ ] Auto-returns to logo after completion (GREEN → logo)

### Reference Prototypes
- `03_custom_wakeword/03_custom_wakeword.ino`

---

## Phase 3: Embedding + Similarity - PENDING

### Tasks
- [ ] Create `ml/yamnet_embed.cpp/h` - Adapt from `04_yamnet_audio_embedding`
- [ ] Create `ml/embed_search.cpp/h` - Cosine similarity search
- [ ] Load pre-computed embeddings from SD card
- [ ] Implement 300-vector similarity search
- [ ] Test: Record → embed → find closest intent

### Files to Create
```
ml/
├── yamnet_embed.cpp/h
└── embed_search.cpp/h
```

### Reference Prototypes
- `04_yamnet_audio_embedding/*.cpp`

---

## Phase 4: LLM Integration - PENDING

### Tasks
- [ ] Adapt `ml/llm_core.cpp/h` to use memory pool
- [ ] Adapt `ml/tokenizer.cpp/h`
- [ ] Adapt `ml/sampler.cpp/h`
- [ ] Implement streaming token callback
- [ ] Display response character-by-character
- [ ] Test: Intent → LLM generates → displays

### Files to Adapt
```
ml/
├── llm_core.cpp/h
├── tokenizer.cpp/h
└── sampler.cpp/h
```

### Reference Prototypes
- `05_llm_finetuned/*.cpp`

---

## Phase 5: Robotic TTS (SAM) - PENDING

### Tasks
- [ ] Install ESP32-SAM library
- [ ] Create `tts/robot_tts.cpp/h`
- [ ] Configure I2S output to MAX98357A
- [ ] Tune SAM parameters for "ultra robotic" voice
- [ ] Test: Text → robotic speech output

### Files to Create
```
tts/
└── robot_tts.cpp/h
```

### Library
- ESP32-SAM: https://github.com/pschatzmann/ESP32-SAM

---

## Phase 6: Deep Sleep + Wake - PENDING

### Tasks
- [ ] Create `sensors/sleep_manager.cpp/h`
- [ ] Configure QMI8658 motion interrupt
- [ ] Implement 5-minute idle timeout
- [ ] Save/restore state across sleep (RTC memory)
- [ ] Test: Idle → sleep → shake → wake

### Files to Create
```
sensors/
└── sleep_manager.cpp/h
```

---

## Phase 7: WiFi + Model Updates - PENDING

### Tasks
- [ ] Create `network/wifi_manager.cpp/h` - Connect from SD config
- [ ] Create `network/model_updater.cpp/h` - HTTP GET manifest
- [ ] Create `storage/sd_manager.cpp/h` - File abstraction
- [ ] Background WiFi scan (every 30s when idle)
- [ ] Download new models to SD card
- [ ] Test: WiFi connects, detects new version

### Files to Create
```
network/
├── wifi_manager.cpp/h
└── model_updater.cpp/h
storage/
└── sd_manager.cpp/h
```

---

## Phase 8: Stash Upload - PENDING

### Tasks
- [ ] Create `storage/stash_manager.cpp/h` - Queue unrecognized audio
- [ ] Create `network/data_uploader.cpp/h` - Multipart POST
- [ ] Save [audio.wav, embedding.bin, score] to SD
- [ ] Trigger upload when 10 files accumulated
- [ ] TTS "Sorry, couldn't understand" response
- [ ] Test: Low score → stash → upload

### Files to Create
```
storage/
└── stash_manager.cpp/h
network/
└── data_uploader.cpp/h
```

---

## Backend: FastAPI + Redis - PENDING

### Tasks
- [ ] Set up Lightsail instance
- [ ] Create FastAPI app with endpoints:
  - [ ] `GET /api/models/manifest`
  - [ ] `GET /api/models/{name}`
  - [ ] `POST /api/models/{name}` (admin)
  - [ ] `POST /api/stash/upload`
  - [ ] `GET /api/corrections` (HTML UI)
  - [ ] `POST /api/corrections/{id}`
- [ ] Set up Redis for job queue
- [ ] Implement Whisper transcription worker
- [ ] Create correction UI (audio player + text editor)
- [ ] Test: Full upload → transcribe → correct flow

### Files to Create
```
/opt/oaisys-backend/
├── main.py
├── workers.py
├── requirements.txt
├── templates/
│   └── corrections.html
└── data/
    ├── models/
    ├── stash/
    └── dataset/
```

---

## Changelog

### 2024-XX-XX - Phase 1 Complete
- Created unified firmware structure
- Implemented video player with text overlay
- Added gyro-based rotation
- SD card loading for logo.mjpeg
- State machine foundation in place

### 2024-XX-XX - Phase 2 Code Complete
- Added mic_stream for continuous audio with microfrontend
- Added wake_word with TFLite sliding window detection
- Added audio_recorder for fixed-duration recording
- Implemented 6MB memory pool for ML models
- Removed button functionality (caused false triggers with I2S)

---

## Notes

### SD Card Layout
```
SD:/
├── config.json           # WiFi + thresholds
├── media/
│   └── logo.mjpeg        # Boot animation
├── models/
│   ├── wake_word.tflite
│   ├── wake_word.json
│   ├── yamnet.tflite
│   ├── projection.tflite
│   ├── llm_model.bin
│   └── tokenizer.bin
├── data/
│   ├── embeddings.bin    # 300 × 256-dim
│   └── intents.txt       # 300 intent strings
└── stash/                # Unrecognized audio
```

### Memory Budget (PSRAM ~8MB)
- Video buffer: ~2MB (logo.mjpeg)
- ML Pool: 6MB (shared, one model at a time)
  - Wake word: ~700KB
  - YAMNet: ~1MB
  - LLM: ~6MB

### Key Design Decisions
- **TTS**: SAM (1980s robotic voice)
- **Text Overlay**: Arduino_GFX direct draw (not LVGL)
- **Model Storage**: SD card + Lightsail FastAPI
- **WiFi Config**: JSON on SD card
