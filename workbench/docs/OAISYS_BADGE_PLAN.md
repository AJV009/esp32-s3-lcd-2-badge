# OAISYS25 Badge - Unified Firmware Implementation Plan

## Overview

Integrate 5 working prototypes into a single ESP32-S3 badge firmware with:
- Animated logo display with gyro rotation
- Deep sleep with shake-to-wake
- Wake word activation ("Hey Daisy")
- Audio → embedding → similarity matching → LLM response pipeline
- Robotic TTS output
- Cloud model updates via FastAPI backend

---

## Part 1: ESP32 Firmware

### 1.1 State Machine Architecture

```
                    ┌─────────────────┐
                    │      BOOT       │
                    └────────┬────────┘
                             │
                    ┌────────▼────────┐
        ┌──────────►│   LOGO_LOOP     │◄──────────┐
        │           │  (wake word ON) │           │
        │           └────────┬────────┘           │
        │                    │                    │
        │         5min idle  │  wake word         │
        │                    │                    │
        │           ┌────────▼────────┐           │
        │           │   DEEP_SLEEP    │           │
        │           │  (accel wake)   │           │
        │           └────────┬────────┘           │
        │                    │ shake              │
        │                    └────────────────────┘
        │
        │  ┌─────────────────────────────────────────────┐
        │  │         INTERACTION FLOW                    │
        │  └─────────────────────────────────────────────┘
        │                    │
        │           ┌────────▼────────┐
        │           │   RECORDING     │ 3-5 seconds
        │           │ (unload wake)   │
        │           └────────┬────────┘
        │                    │
        │           ┌────────▼────────┐
        │           │   EMBEDDING     │ Load YAMNet
        │           │   EXTRACTION    │ Extract 256-dim
        │           └────────┬────────┘
        │                    │
        │           ┌────────▼────────┐
        │           │   SIMILARITY    │ vs 300 embeddings
        │           │     SEARCH      │
        │           └────────┬────────┘
        │                    │
        │        ┌───────────┴───────────┐
        │        │                       │
        │   score ≥ 0.7            score < 0.7
        │        │                       │
        │  ┌─────▼─────┐          ┌──────▼──────┐
        │  │ LLM_INFER │          │ STASH_DATA  │
        │  │ Load LLM  │          │ Save to SD  │
        │  └─────┬─────┘          └──────┬──────┘
        │        │                       │
        │  ┌─────▼─────┐          ┌──────▼──────┐
        │  │ DISPLAY   │          │ TTS_SORRY   │
        │  │ Stop logo │          │ "Couldn't   │
        │  │ Show text │          │  understand"│
        │  └─────┬─────┘          └──────┬──────┘
        │        │                       │
        │  ┌─────▼─────┐                 │
        │  │ TTS_SPEAK │                 │
        │  │ Robot voice│                │
        │  └─────┬─────┘                 │
        │        │                       │
        └────────┴───────────────────────┘
```

### 1.2 Directory Structure

```
workbench/working_protos/oaisys_badge/
├── oaisys_badge.ino           # Main sketch - state machine
├── config.h                   # Pin definitions, constants
│
├── display/
│   ├── video_player.cpp/h     # From 00_video_loop (MjpegClass integrated)
│   └── text_overlay.cpp/h     # NEW: LVGL text on top of video
│
├── audio/
│   ├── audio_recorder.cpp/h   # From 04_yamnet (adapted)
│   ├── mic_stream.cpp/h       # Continuous mic for wake word
│   └── speaker_out.cpp/h      # From 02_speaker_mic_combo
│
├── ml/
│   ├── wake_word.cpp/h        # From 03_custom_wakeword
│   ├── yamnet_embed.cpp/h     # From 04_yamnet
│   ├── embed_search.cpp/h     # NEW: Cosine similarity search
│   ├── llm_core.cpp/h         # From 05_llm_finetuned
│   ├── tokenizer.cpp/h        # From 05_llm_finetuned
│   └── sampler.cpp/h          # From 05_llm_finetuned
│
├── tts/
│   └── robot_tts.cpp/h        # NEW: SAM/LPC robotic voice
│
├── sensors/
│   ├── orientation.cpp/h      # From 00_video_loop (IMU rotation)
│   └── sleep_manager.cpp/h    # NEW: Deep sleep + shake wake
│
├── network/
│   ├── wifi_manager.cpp/h     # NEW: WiFi from SD config
│   ├── model_updater.cpp/h    # NEW: Check/download models
│   └── data_uploader.cpp/h    # NEW: Upload stashed data
│
├── storage/
│   ├── sd_manager.cpp/h       # NEW: SD card abstraction
│   └── stash_manager.cpp/h    # NEW: Manage stashed audio/embeddings
│
└── sd_data/                   # Files to copy to SD card
    ├── config.json            # WiFi creds, thresholds, server URL
    ├── models/
    │   ├── wake_word.tflite
    │   ├── wake_word.json
    │   ├── yamnet.tflite
    │   ├── projection.tflite
    │   ├── llm_model.bin
    │   └── tokenizer.bin
    ├── data/
    │   ├── embeddings.bin     # 300 × 256-dim float32 (307KB)
    │   └── intents.txt        # 300 intent strings
    └── stash/                 # Auto-created for unrecognized audio
```

### 1.3 Memory Management Strategy

**Problem**: PSRAM is ~8MB, but models compete for space:
- Wake word: ~700KB
- YAMNet + projection: ~1MB
- LLM: ~6MB
- Video buffer: ~2MB

**Solution**: Sequential model loading with explicit unload

```cpp
// Memory pool reserved in PSRAM
#define ML_POOL_SIZE (6 * 1024 * 1024)  // 6MB for largest model
static uint8_t* ml_pool = nullptr;

// Each ML module uses pool, not direct ps_malloc
class MLModule {
public:
    virtual bool load(uint8_t* pool, size_t pool_size) = 0;
    virtual void unload() = 0;
    virtual size_t requiredMemory() = 0;
};

// State transitions trigger load/unload:
// LOGO_LOOP: wake_word.load()
// RECORDING: wake_word.unload()
// EMBEDDING: yamnet.load() → yamnet.unload()
// LLM_INFER: llm.load() → llm.unload()
// Return to LOGO_LOOP: wake_word.load()
```

### 1.4 Module Interfaces

Each module follows `begin()`/`end()` pattern:

```cpp
// video_player.h
class VideoPlayer {
public:
    bool begin(const char* mjpeg_path);  // Load video to PSRAM
    void play();                          // Decode one frame
    void pause();
    void resume();
    void setRotation(uint8_t r);
    void showText(const char* text);     // Overlay text, pause video
    void hideText();                      // Resume video
    void end();
};

// wake_word.h
class WakeWordDetector {
public:
    bool begin(uint8_t* pool, const char* model_path, const char* config_path);
    bool detect();       // Returns true on wake word
    void end();          // Free all memory
};

// yamnet_embed.h
class YamnetEmbedder {
public:
    bool begin(uint8_t* pool, const char* model_path, const char* proj_path);
    bool extract(int16_t* audio, size_t samples, float* embedding_out);
    void end();
};

// llm_core.h (already exists, minor adaptations)
class LLMInference {
public:
    bool begin(uint8_t* pool, const char* model_path, const char* tok_path);
    void generate(const char* prompt, void (*on_token)(const char*));
    void end();
};

// robot_tts.h
class RobotTTS {
public:
    bool begin();
    void speak(const char* text);  // Blocking, outputs to I2S speaker
    void end();
};
```

### 1.5 Implementation Phases

#### Phase 1: Core Integration (Video + State Machine)
**Goal**: Logo loops, text overlay works, gyro rotation

Files to create/modify:
- `oaisys_badge.ino` - Main state machine skeleton
- `config.h` - All pin definitions
- `display/video_player.cpp` - Copy from `00_video_loop`, add `showText()`
- `display/text_overlay.cpp` - LVGL label overlay on GFX canvas

Key tasks:
1. Copy `MjpegClass.h` and video player logic
2. Implement text overlay (Matrix green theme)
3. Add gyro-based rotation
4. Test: Logo plays, rotates with device orientation

#### Phase 2: Wake Word + Recording
**Goal**: Wake word detection triggers recording mode

Files to create:
- `audio/mic_stream.cpp` - Continuous I2S mic input for wake word
- `ml/wake_word.cpp` - Adapted from `03_custom_wakeword`
- `audio/audio_recorder.cpp` - 3-5 sec recording to buffer

Key tasks:
1. Adapt wake word detector to use memory pool
2. Implement recording mode (stop wake word, record fixed duration)
3. Save audio to SD card in `/stash/` with timestamp
4. Test: Wake word triggers, records 5 sec, saves to SD

#### Phase 3: Embedding + Similarity Search
**Goal**: Extract embedding, find closest intent

Files to create:
- `ml/yamnet_embed.cpp` - Adapted from `04_yamnet_audio_embedding`
- `ml/embed_search.cpp` - Load embeddings.bin, cosine similarity

Key tasks:
1. Adapt YAMNet to use memory pool
2. Implement projection head (TFLite micro)
3. Load pre-computed embeddings from SD
4. Implement cosine similarity search
5. Test: Record → embed → find closest (print to Serial)

#### Phase 4: LLM Integration
**Goal**: High-confidence matches trigger LLM response

Files to modify:
- `ml/llm_core.cpp` - Adapt to use memory pool
- `display/video_player.cpp` - Stream text to display

Key tasks:
1. Adapt LLM to pool-based allocation
2. Implement streaming callback for token display
3. Stop video, show text character-by-character
4. Test: Matched intent → LLM generates → displays on screen

#### Phase 5: Robotic TTS
**Goal**: Speak responses with 90s robot voice

Files to create:
- `tts/robot_tts.cpp` - SAM or Talkie-based synthesis

**Recommended approach**: **Talkie library** (Arduino LPC speech)
- Proven on ESP32
- Very robotic sound (1980s Speak & Spell)
- Tiny memory footprint (~10KB)
- Pre-encoded phonemes, fast synthesis

Alternative: **ESP32-SAM** (Software Automatic Mouth port)
- More natural but still robotic
- Text-to-phoneme built in
- Slightly larger (~50KB)

Key tasks:
1. Integrate Talkie or SAM library
2. Configure I2S output to MAX98357A
3. Implement `speak()` with text-to-phoneme conversion
4. Test: Text → robotic speech output

#### Phase 6: Deep Sleep + Shake Wake
**Goal**: 5min idle → deep sleep, accelerometer shake → wake

Files to create:
- `sensors/sleep_manager.cpp` - Sleep/wake logic

Key tasks:
1. Configure RTC timer for 5-minute idle detection
2. Configure QMI8658 interrupt for motion detection
3. Use ESP32 ULP or ext1 wake source
4. Save/restore state across sleep
5. Test: Idle → sleep → shake → wake → resume logo

#### Phase 7: WiFi + Model Updates
**Goal**: Check server for new models, download if available

Files to create:
- `network/wifi_manager.cpp` - Connect from SD config
- `network/model_updater.cpp` - HTTP GET model manifest
- `storage/sd_manager.cpp` - File operations abstraction

Key tasks:
1. Parse `config.json` for WiFi credentials and server URL
2. Implement background WiFi scan (every 30s when idle)
3. Check model manifest endpoint
4. Download new models to SD, swap on next boot
5. Test: WiFi connects, checks manifest, downloads test file

#### Phase 8: Stash Upload + Low-Confidence Handling
**Goal**: Unrecognized audio uploaded for cloud transcription

Files to create:
- `storage/stash_manager.cpp` - Queue unrecognized audio
- `network/data_uploader.cpp` - Upload when 10 files accumulated

Key tasks:
1. Save [audio.wav, embedding.bin, score, timestamp] to `/stash/`
2. Count stashed files, trigger upload at 10
3. Package as multipart POST to server
4. Delete local files after successful upload
5. Play "Sorry, couldn't understand" via TTS

---

## Part 2: Cloud Backend (FastAPI + Redis)

### 2.1 Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Lightsail Instance                       │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │   FastAPI   │  │    Redis    │  │   File Storage     │  │
│  │   Server    │◄─┤    Queue    │  │   /data/models/    │  │
│  │   :8000     │  │   :6379     │  │   /data/stash/     │  │
│  └──────┬──────┘  └─────────────┘  │   /data/dataset/   │  │
│         │                          └─────────────────────┘  │
│         │                                                   │
│  ┌──────▼──────┐                                           │
│  │   Worker    │  Background transcription                  │
│  │  (rq/arq)   │  Whisper API or local                     │
│  └─────────────┘                                           │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 API Endpoints

```python
# Model Management
GET  /api/models/manifest
     Returns: {"wake_word": "v1.2", "yamnet": "v1.0", "llm": "v2.1", ...}

GET  /api/models/{model_name}
     Returns: Binary model file
     Headers: X-Model-Version: v1.2

POST /api/models/{model_name}
     Upload new model version (admin auth required)

# Stash Management
POST /api/stash/upload
     Multipart: audio files + metadata JSON
     Triggers: Background transcription job

GET  /api/stash/pending
     Returns: List of files awaiting correction

# Correction UI
GET  /api/corrections
     Returns: HTML page with audio player + text editor

POST /api/corrections/{id}
     Update transcription, add to training dataset

POST /api/retrain
     Trigger model retraining (admin auth required)

# Health
GET  /api/health
     Returns: {"status": "ok", "models_count": 5, "pending_transcriptions": 3}
```

### 2.3 Data Flow

```
ESP32 uploads stash (10 files)
        │
        ▼
POST /api/stash/upload
        │
        ▼
Save to /data/stash/{timestamp}/
Queue transcription job in Redis
        │
        ▼
Worker picks up job
Calls Whisper API (or local whisper.cpp)
Saves transcript to /data/stash/{id}/transcript.txt
Marks as "pending_review"
        │
        ▼
Admin opens /api/corrections
Listens to audio, edits transcript
Clicks "Save" → adds to /data/dataset/qa.csv
        │
        ▼
Admin clicks "Retrain"
Triggers training job (or manual Colab run)
New model uploaded via POST /api/models/llm
        │
        ▼
ESP32 checks GET /api/models/manifest
Sees new version, downloads model
Uses new model on next boot
```

### 2.4 File Structure (Server)

```
/opt/oaisys-backend/
├── main.py                 # FastAPI app
├── workers.py              # Background job handlers
├── requirements.txt
├── .env                    # API keys, secrets
│
├── data/
│   ├── models/
│   │   ├── manifest.json   # Version tracking
│   │   ├── wake_word_v1.2.tflite
│   │   ├── yamnet_v1.0.tflite
│   │   ├── projection_v1.0.tflite
│   │   ├── llm_v2.1.bin
│   │   └── tokenizer_v1.0.bin
│   │
│   ├── stash/
│   │   └── {device_id}_{timestamp}/
│   │       ├── audio_001.wav
│   │       ├── metadata.json
│   │       └── transcript.txt  (after processing)
│   │
│   └── dataset/
│       ├── qa.csv              # Training data
│       └── embeddings.bin      # Pre-computed for ESP32
│
└── templates/
    └── corrections.html    # Simple Jinja2 template
```

---

## Part 3: SD Card Layout (ESP32)

```
SD:/
├── config.json
│   {
│     "wifi_ssid": "MyNetwork",
│     "wifi_pass": "secret123",
│     "server_url": "http://your-lightsail-ip:8000",
│     "device_id": "badge_001",
│     "wake_word_threshold": 0.5,
│     "embed_threshold": 0.7,
│     "llm_temperature": 0.8,
│     "idle_sleep_ms": 300000
│   }
│
├── models/
│   ├── wake_word.tflite    (~130KB)
│   ├── wake_word.json      (config)
│   ├── yamnet.tflite       (~2.8MB)
│   ├── projection.tflite   (~663KB)
│   ├── llm_model.bin       (~6MB)
│   ├── tokenizer.bin       (~13KB)
│   └── versions.json       (local version cache)
│
├── data/
│   ├── output.mjpeg        (~2-5MB logo animation)
│   ├── embeddings.bin      (300 × 256 × 4 = 307KB)
│   ├── intents.txt         (300 lines, intent strings)
│   └── sorry.txt           ("I'm sorry, I couldn't understand that")
│
└── stash/                  (auto-created)
    └── {timestamp}/
        ├── audio.wav
        ├── embedding.bin
        └── meta.json       {score, timestamp}
```

---

## Part 4: Key Implementation Details

### 4.1 Text Overlay on Video (LVGL)

**Chosen: LVGL Overlay** - Rich typography, word wrap, polished look

```cpp
// text_overlay.cpp
#include <lvgl.h>

static lv_obj_t* text_layer = nullptr;
static lv_obj_t* label = nullptr;

void initTextOverlay() {
    // Create semi-transparent container
    text_layer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(text_layer, 230, 120);
    lv_obj_align(text_layer, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(text_layer, LV_OPA_80, 0);
    lv_obj_set_style_bg_color(text_layer, lv_color_hex(0x001100), 0);
    lv_obj_set_style_border_width(text_layer, 2, 0);
    lv_obj_set_style_border_color(text_layer, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_radius(text_layer, 8, 0);

    // Create label with retro monospace font
    label = lv_label_create(text_layer);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, 210);
    lv_obj_set_style_text_color(label, lv_color_hex(0x00FF00), 0);  // Matrix green
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_center(label);

    // Initially hidden
    lv_obj_add_flag(text_layer, LV_OBJ_FLAG_HIDDEN);
}

void showText(const char* text) {
    if (lvgl_lock(-1)) {
        lv_label_set_text(label, text);
        lv_obj_clear_flag(text_layer, LV_OBJ_FLAG_HIDDEN);
        lvgl_unlock();
    }
}

void hideText() {
    if (lvgl_lock(-1)) {
        lv_obj_add_flag(text_layer, LV_OBJ_FLAG_HIDDEN);
        lvgl_unlock();
    }
}

// For streaming LLM output character-by-character
void appendText(const char* chunk) {
    if (lvgl_lock(-1)) {
        const char* current = lv_label_get_text(label);
        String updated = String(current) + chunk;
        lv_label_set_text(label, updated.c_str());
        lvgl_unlock();
    }
}
```

### 4.2 Wake Word → Recording Transition

```cpp
void transitionToRecording() {
    // 1. Stop wake word detection
    wakeWord.end();  // Frees ~700KB PSRAM

    // 2. Reconfigure I2S for recording (same pins, different buffer size)
    recorder.begin(RECORD_DURATION_MS);

    // 3. Visual feedback
    video.showText("Listening...");

    // 4. Record
    int16_t* audio = recorder.record();  // Blocking, returns PSRAM buffer
    size_t samples = recorder.getSampleCount();

    // 5. Save to SD
    saveAudioToSD(audio, samples);

    // 6. Transition to embedding
    transitionToEmbedding(audio, samples);
}
```

### 4.3 Cosine Similarity Search

```cpp
// embed_search.cpp
class EmbeddingSearch {
    float* embeddings;      // 300 × 256 floats from SD
    char** intents;         // 300 strings from SD
    int count;

public:
    bool begin(const char* embed_path, const char* intent_path) {
        // Load embeddings.bin (307KB) to PSRAM
        embeddings = (float*)ps_malloc(300 * 256 * sizeof(float));
        File f = SD.open(embed_path);
        f.read((uint8_t*)embeddings, 300 * 256 * sizeof(float));

        // Load intents.txt (one per line)
        // ...
    }

    SearchResult search(float* query_embed) {
        float best_score = -1;
        int best_idx = -1;

        for (int i = 0; i < count; i++) {
            float score = cosineSimilarity(query_embed, &embeddings[i * 256], 256);
            if (score > best_score) {
                best_score = score;
                best_idx = i;
            }
        }

        return {intents[best_idx], best_score};
    }

private:
    float cosineSimilarity(float* a, float* b, int len) {
        float dot = 0, norm_a = 0, norm_b = 0;
        for (int i = 0; i < len; i++) {
            dot += a[i] * b[i];
            norm_a += a[i] * a[i];
            norm_b += b[i] * b[i];
        }
        return dot / (sqrtf(norm_a) * sqrtf(norm_b));
    }
};
```

### 4.4 Robotic TTS with SAM (Software Automatic Mouth)

**Chosen: SAM** - Any text input, classic 1980s computer voice

SAM is the legendary Commodore 64 / Apple II speech synthesizer. It converts
arbitrary English text to phonemes and synthesizes robotic speech. Perfect for
that retro "talking computer" vibe!

```cpp
// robot_tts.cpp
// Using ESP32-SAM port: https://github.com/pschatzmann/ESP32-SAM

#include "ESP32SAM.h"
#include "AudioOutputI2S.h"

class RobotTTS {
private:
    AudioOutputI2S* out = nullptr;
    ESP32SAM* sam = nullptr;

public:
    bool begin() {
        // Configure I2S for MAX98357A speaker
        out = new AudioOutputI2S();
        out->SetPinout(6, 7, 8);  // BCLK, LRC, DOUT
        out->SetGain(0.5);        // Volume control
        out->begin();

        sam = new ESP32SAM();
        return true;
    }

    void speak(const char* text) {
        // SAM handles text-to-phoneme conversion internally
        // Output: 22050Hz mono audio
        sam->Say(out, text);
    }

    // For more robotic effect, adjust voice parameters
    void setRoboticVoice() {
        sam->SetSpeed(72);      // Default 72, lower = slower
        sam->SetPitch(64);      // Default 64, higher = squeakier
        sam->SetThroat(128);    // Default 128, affects resonance
        sam->SetMouth(128);     // Default 128, affects articulation
    }

    // Ultra robotic: pitch down, slow, mechanical
    void setUltraRobotic() {
        sam->SetSpeed(50);      // Slow and deliberate
        sam->SetPitch(40);      // Deep, mechanical
        sam->SetThroat(160);    // More robotic resonance
        sam->SetMouth(100);     // Slightly muffled
    }

    void end() {
        if (sam) { delete sam; sam = nullptr; }
        if (out) { delete out; out = nullptr; }
    }
};

// Usage in main sketch:
RobotTTS tts;

void setup() {
    tts.begin();
    tts.setUltraRobotic();  // Maximum robot vibes!
}

void speakResponse(const char* text) {
    video.showText(text);      // Show on screen
    tts.speak(text);           // Speak robotically
    delay(2000);               // Pause after speaking
    video.hideText();          // Resume logo
}
```

**Memory footprint**: ~50KB ROM for SAM phoneme tables
**Latency**: Near-instant start (no model loading)
**Quality**: Authentic 1980s computer voice - perfect for badge aesthetic!

### 4.5 Deep Sleep with Accelerometer Wake

```cpp
// sleep_manager.cpp
#include "esp_sleep.h"
#include "driver/rtc_io.h"

#define ACCEL_INT_PIN GPIO_NUM_XX  // QMI8658 INT pin (check schematic)

void enterDeepSleep() {
    // Save state to RTC memory
    RTC_DATA_ATTR static uint8_t last_rotation;
    last_rotation = video.getRotation();

    // Configure QMI8658 motion interrupt
    // (Set high-g threshold, enable interrupt output)
    configureAccelInterrupt();

    // Configure wake source
    esp_sleep_enable_ext0_wakeup(ACCEL_INT_PIN, 1);  // Wake on HIGH

    // Enter deep sleep
    esp_deep_sleep_start();
}

void setup() {
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        // Woke from shake - restore state
        video.setRotation(last_rotation);
    }
    // Normal boot...
}
```

---

## Part 5: Testing Checkpoints

| Phase | Test | Pass Criteria |
|-------|------|---------------|
| 1 | Logo plays | MJPEG loops smoothly at 30+ FPS |
| 1 | Text overlay | Text appears over video |
| 1 | Gyro rotation | Tilting rotates display |
| 2 | Wake word | "Hey Daisy" triggers within 2 sec |
| 2 | Recording | 5 sec audio saved to SD as WAV |
| 3 | Embedding | YAMNet produces 256-dim vector |
| 3 | Similarity | Correct intent found for test phrase |
| 4 | LLM load | Model loads in <5 sec |
| 4 | Generation | Response streams to display |
| 5 | TTS output | Robotic speech audible from speaker |
| 6 | Deep sleep | Current drops to <10mA |
| 6 | Shake wake | Badge wakes and resumes logo |
| 7 | WiFi connect | Connects from SD config |
| 7 | Model check | Detects new version on server |
| 8 | Stash upload | 10 files uploaded successfully |
| Backend | Manifest | GET returns model versions |
| Backend | Transcribe | Uploaded audio gets transcript |
| Backend | Correction | UI allows editing and saving |

---

## Part 6: Risk Mitigation

| Risk | Mitigation |
|------|------------|
| PSRAM fragmentation | Use fixed memory pool, sequential load/unload |
| Wake word false positives | Tune threshold in config.json, add cooldown |
| Embedding mismatch | Start with high threshold (0.8), tune down |
| LLM hallucination | Train on exact Q&A, use low temperature |
| TTS unintelligible | Test Talkie vs SAM, choose clearer one |
| WiFi unreliable | Graceful degradation, work fully offline |
| Deep sleep instability | Test thoroughly, add watchdog timer |
| SD card corruption | Use safe write patterns, checksums |

---

## Implementation Order (Sequential)

**Chosen approach**: Complete each phase before starting next - safer, easier to debug

```
Week 1: Foundation
├── Phase 1: Core (Video + State Machine)              ████████░░  2-3 days
│   └── Checkpoint: Logo plays, gyro rotates, text overlay works
│
└── Phase 2: Wake Word + Recording                     ████████░░  2 days
    └── Checkpoint: "Hey Daisy" triggers, 5 sec recorded to SD

Week 2: ML Pipeline
├── Phase 3: Embedding Extraction                      ████████░░  2-3 days
│   └── Checkpoint: Audio → 256-dim embedding → similarity search
│
└── Phase 4: LLM Integration                           ████░░░░░░  1-2 days
    └── Checkpoint: Matched intent → LLM generates → displays text

Week 3: Audio + Power
├── Phase 5: Robotic TTS (SAM)                         ████████░░  2-3 days
│   └── Checkpoint: Badge speaks responses in robotic voice
│
└── Phase 6: Deep Sleep + Wake                         ████░░░░░░  1-2 days
    └── Checkpoint: 5min idle → sleep → shake → wake

Week 4: Network + Backend
├── Phase 7: WiFi + Model Updates                      ████████░░  2 days
│   └── Checkpoint: Downloads new model from server
│
├── Phase 8: Stash Upload                              ████░░░░░░  1-2 days
│   └── Checkpoint: Uploads 10 unrecognized audio files
│
└── Backend: FastAPI + Redis + Correction UI           ████████████  3-4 days
    └── Checkpoint: Full cloud pipeline working
```

**Total: ~4 weeks** sequential implementation

### Milestone Deliverables

| Milestone | What Works | When |
|-----------|------------|------|
| M1: Demo-ready | Logo + gyro rotation + text overlay | End of Day 3 |
| M2: Voice trigger | Wake word triggers recording | End of Week 1 |
| M3: Smart badge | Full local pipeline (voice → response) | End of Week 2 |
| M4: Talking badge | Same as M3 but speaks responses | End of Week 3 |
| M5: Connected badge | WiFi updates + cloud backend | End of Week 4 |

---

## Critical Files to Reference

From existing prototypes:
- `00_video_loop_btn_pause_gyro_rotate.ino` - Video + IMU patterns
- `02_speaker_mic_combo.ino` - I2S audio I/O
- `03_custom_wakeword.ino` - TFLite streaming inference
- `04_yamnet_audio_embedding/*.cpp` - Embedding extraction
- `05_llm_finetuned/*.cpp` - LLM inference with Q8_0

From training pipelines:
- `audio_semantic_xtts.ipynb` - Embedding dataset generation
- `llm_qa_scaled_training.ipynb` - LLM training and quantization
