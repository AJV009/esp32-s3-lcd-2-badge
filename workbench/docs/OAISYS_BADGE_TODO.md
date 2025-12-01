# OAISYS25 Badge - Implementation Progress Tracker

> **Plan Document:** [OAISYS_BADGE_PLAN.md](./OAISYS_BADGE_PLAN.md)
> **Main Firmware:** `workbench/working_protos/oaisys_badge/`

---

## Overall Progress

| Phase | Description | Status | Notes |
|-------|-------------|--------|-------|
| Phase 1 | Core (Video + Text + Gyro) | **COMPLETE** | Tested, working |
| Phase 2 | Wake Word + Recording | **COMPLETE** | Code complete, model on SD |
| Phase 3 | Embedding + Similarity | **IN PROGRESS** | Firmware done, training audio encoder |
| Phase 4 | LLM Integration | **COMPLETE** | Code + models ready |
| Phase 5 | Robotic TTS (SAM) | **COMPLETE** | SAM with I2S output |
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

## Phase 3: Embedding + Similarity - IN PROGRESS

### Completed Tasks
- [x] Create `ml/audio_embed.cpp/h` - Custom CNN audio embedding (ESP-DSP FFT + TFLite)
- [x] Create `ml/embed_search.cpp/h` - Cosine similarity search with loop unrolling
- [x] Add STATE_EMBEDDING, STATE_SIMILARITY, STATE_TTS_SORRY to state machine
- [x] Integrate embedding/search into main sketch
- [x] Create `train_audio_encoder.ipynb` notebook for vast.ai (contrastive learning)

### Pending Tasks
- [ ] **Run training on vast.ai** - Train custom audio encoder CNN
- [ ] Copy trained models to SD card:
  - `audio_encoder.tflite` → `/models/audio_encoder.tflite`
  - `embeddings.bin` → `/data/embeddings.bin`
  - `intents.txt` → `/data/intents.txt`
- [ ] Test: Record → embed → find closest intent

### Files Created
```
ml/
├── audio_embed.cpp/h      # Custom CNN embedding (64 mel × 96 frames → 256-dim)
└── embed_search.cpp/h     # Cosine similarity search

tests/audio_embedding_dataset_ipynb/
└── train_audio_encoder.ipynb  # Training notebook for vast.ai
```

### Model Architecture (Final)
**Original plan:** TF Hub YAMNet (1024-dim, ~13MB) → Projection → Search
**Discarded:** STM32 YAMNet-256 (was a classifier, not embedder)
**Final approach:** Custom CNN trained with contrastive learning

Custom audio encoder architecture:
- Input: (64, 96, 1) mel-spectrogram
- Conv2D 32 → MaxPool → Conv2D 64 → MaxPool → Conv2D 128 → MaxPool → Conv2D 128 → GlobalAvgPool → Dense 256
- Output: 256-dim L2-normalized embedding
- Size: ~100-300KB TFLite

### Training Steps (vast.ai)
1. Upload `audio_data/` folder to vast.ai instance
2. Run `train_audio_encoder.ipynb` notebook
3. Download outputs: `audio_encoder.tflite`, `embeddings.bin`, `intents.txt`
4. Copy to SD card `/models/` and `/data/` folders

### Reference Prototypes
- `04_yamnet_audio_embedding/*.cpp` (for mel-spectrogram code)

---

## Phase 4: LLM Integration - COMPLETE

### Completed Tasks
- [x] Copy `llm_core.cpp/h` from `05_llm_finetuned` (optimized SIMD code)
- [x] Copy `tokenizer.cpp/h` from `05_llm_finetuned`
- [x] Copy `sampler.cpp/h` from `05_llm_finetuned`
- [x] Create `llm_inference.cpp/h` - Thin wrapper for badge integration
- [x] Add `STATE_LLM_INFERENCE` state handler
- [x] Implement streaming token callback (`onLLMToken`)
- [x] Display response character-by-character via `video.appendText()`
- [x] Update similarity handler to transition to LLM state
- [x] Copy model files to sd_data/models/ (from 05_llm_finetuned)

### Model Files (Ready)
```
sd_data/models/
├── llm_model.bin     # 6.0MB - Q8_0 quantized LLM (ajv009_6M_1024TK)
└── tokenizer.bin     # 13KB - BPE tokenizer
```

### Pending Tasks
- [ ] Test: Intent → LLM generates → displays

### Files Created
```
ml/
├── llm_core.cpp/h       # Copied from 05_llm_finetuned (unchanged)
├── tokenizer.cpp/h      # Copied from 05_llm_finetuned (unchanged)
├── sampler.cpp/h        # Copied from 05_llm_finetuned (unchanged)
└── llm_inference.cpp/h  # New wrapper class for badge
```

### Model Requirements
The LLM expects a Q8_0 quantized model file with:
- Magic: `0x616b3432` (version 2 format)
- Config header: dim, hidden_dim, n_layers, n_heads, n_kv_heads, vocab_size, seq_len
- Weights: RMS norms (fp32) + quantized attention/FFN tensors (int8 + scales)

See `tests/llm_qa_scaled_training/` for training notebook.

### Reference Prototypes
- `05_llm_finetuned/*.cpp`

---

## Phase 5: Robotic TTS (SAM) - COMPLETE

### Completed Tasks
- [x] Install ESP32-SAM library (cloned to Arduino/libraries/SAM)
- [x] Create `tts/robot_tts.cpp/h` - SAM wrapper with custom I2S output
- [x] Configure I2S1 output to MAX98357A at 22050 Hz (SAM's native rate)
- [x] Created ultra-robotic voice preset (speed=100, pitch=50, throat=200, mouth=200)
- [x] Integrated into state machine (STATE_TTS_OUTPUT, STATE_TTS_SORRY)
- [x] Startup greeting "Hello. I am ready."

### Files Created
```
src/tts/
├── robot_tts.h        # RobotTTS class + SAMI2SOutput class
└── robot_tts.cpp      # Implementation with voice presets
```

### Voice Presets
| Voice | Speed | Pitch | Throat | Mouth |
|-------|-------|-------|--------|-------|
| ROBOT (ultra) | 100 | 50 | 200 | 200 |
| LITTLE_ROBOT | 92 | 60 | 190 | 190 |
| ALIEN | 100 | 64 | 150 | 200 |

### Library
- ESP32-SAM: https://github.com/pschatzmann/ESP32-SAM

### Test Checklist
- [ ] Startup says "Hello. I am ready."
- [ ] TTS_OUTPUT speaks LLM response
- [ ] TTS_SORRY speaks apology message
- [ ] Audio plays clearly through MAX98357A

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

### 2024-XX-XX - Phase 3 Firmware Complete
- Switched from TF Hub YAMNet (13MB) to STM32 YAMNet-256 (182KB)
- Created yamnet_embed.cpp/h with ESP-DSP FFT for mel-spectrogram
- Created embed_search.cpp/h with cosine similarity search
- Added STATE_EMBEDDING, STATE_SIMILARITY, STATE_TTS_SORRY states
- Created retrain_yamnet256.ipynb for vast.ai retraining
- Eliminated projection layer (direct 256-dim search)

### 2024-XX-XX - Phase 4 Firmware Complete
- Copied optimized llm_core/tokenizer/sampler from 05_llm_finetuned
- Created llm_inference.cpp/h thin wrapper class
- Added STATE_LLM_INFERENCE state with streaming token callback
- LLM generates response from matched intent
- Tokens stream character-by-character to display via appendText()
- Transitions to TTS after generation

### 2024-XX-XX - Phase 5 Firmware Complete
- Cloned ESP32-SAM library from pschatzmann/arduino-SAM
- Created custom SAMI2SOutput class for legacy I2S driver
- Created robot_tts.cpp/h with voice presets
- Integrated into state machine: STATE_TTS_OUTPUT and STATE_TTS_SORRY
- Ultra-robotic voice: speed=100, pitch=50, throat=200, mouth=200
- Startup greeting says "Hello. I am ready."
- Full pipeline: Wake word → Record → Embed → Search → LLM → TTS → Logo

### 2024-11-29 - Audio Encoder + Config Loading
- **BREAKING**: Replaced YAMNet-256 with custom CNN audio encoder
  - STM32 YAMNet-256 was a classifier (10 classes), not an embedder
  - New approach: Train small CNN with contrastive learning
  - Renamed `yamnet_embed.cpp/h` → `audio_embed.cpp/h`
  - Model path: `/models/audio_encoder.tflite`
- Added `train_audio_encoder.ipynb` notebook for training on vast.ai
- **Added runtime config loading from SD card**:
  - `config.json` now loaded at startup
  - Runtime-configurable: embed_threshold, llm_temperature, llm_max_tokens, record_duration_ms
- Copied LLM models from 05_llm_finetuned to sd_data/models/
- Full code review and verification of state machine flow

---

## Notes

### SD Card Layout
```
SD:/
├── config.json              # WiFi + runtime thresholds (now loaded!)
├── media/
│   └── logo.mjpeg           # Boot animation
├── models/
│   ├── wake_word.tflite     # ✅ Ready (130KB)
│   ├── wake_word.json       # ✅ Ready
│   ├── audio_encoder.tflite # ⏳ From training (~100-300KB)
│   ├── llm_model.bin        # ✅ Ready (6.0MB)
│   └── tokenizer.bin        # ✅ Ready (13KB)
├── data/
│   ├── embeddings.bin       # ⏳ From training (300 × 256-dim)
│   └── intents.txt          # ⏳ From training (300 intent strings)
└── stash/                   # Unrecognized audio (Phase 8)
```

### Memory Budget (PSRAM ~8MB)
- Video buffer: ~2MB (logo.mjpeg)
- ML Pool: 6MB (shared, one model at a time)
  - Wake word: ~550KB (tensor arena)
  - Audio encoder: ~300KB model + ~300KB arena
  - LLM: ~6MB
- Query embedding buffer: 1KB (256 × float32)
- Embedding database: ~307KB (300 × 256-dim)

### Key Design Decisions
- **TTS**: SAM (1980s robotic voice)
- **Text Overlay**: Arduino_GFX direct draw (not LVGL)
- **Model Storage**: SD card + Lightsail FastAPI
- **WiFi Config**: JSON on SD card
