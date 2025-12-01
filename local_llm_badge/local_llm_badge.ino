/*******************************************************************************
 * OAISYS25 Badge - Main Application
 *
 * A smart conference badge with:
 * - Animated logo display with gyro rotation
 * - Wake word activation ("Hey Daisy")
 * - Voice command processing via embeddings + LLM
 * - Robotic TTS output
 * - Deep sleep with shake-to-wake
 *
 * Phase 1: Video loop + text overlay + gyro rotation
 * Phase 2: Wake word detection + recording
 * Phase 3: Embedding extraction + similarity search
 * Phase 4: LLM inference + streaming text display
 * Phase 5: Robotic TTS output
 ******************************************************************************/

#include "config.h"
#include "src/display/video_player.h"
#include "src/sensors/orientation.h"
#include "src/audio/mic_stream.h"
#include "src/audio/audio_recorder.h"
#include "src/ml/wake_word.h"
#include "src/ml/audio_embed.h"
#include "src/ml/embed_search.h"
#include "src/ml/llm_inference.h"
#include "src/tts/robot_tts.h"

#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>  // For aligned PSRAM allocation
#include <esp_sleep.h>      // For deep sleep

//==============================================================================
// Runtime Configuration (loaded from SD card)
//==============================================================================
struct RuntimeConfig {
    float wakeWordThreshold = DEFAULT_WAKE_THRESHOLD;
    float embedThreshold = DEFAULT_EMBED_THRESHOLD;
    float llmTemperature = DEFAULT_LLM_TEMPERATURE;
    float llmTopP = DEFAULT_LLM_TOPP;
    int llmMaxTokens = 128;
    unsigned long idleSleepMs = IDLE_SLEEP_MS;
    int recordDurationMs = RECORD_DURATION_MS;
} runtimeConfig;

//==============================================================================
// Global Objects
//==============================================================================

VideoPlayer video;
OrientationManager orientation;

// Phase 2: Audio and wake word
MicStream micStream;
AudioRecorder recorder;
WakeWordDetector wakeWord;

// Phase 3: Embedding + similarity search
AudioEmbedder audioEmbed;
EmbeddingSearch embedSearch;
float* queryEmbedding = nullptr;  // 256-dim embedding buffer
SearchResult lastSearchResult;

// Phase 4: LLM inference
LLMInference llmInference;
String llmResponse;  // Accumulated response for display
bool llmGenerating = false;

// Phase 5: TTS
RobotTTS tts;
bool ttsSpeaking = false;

// Memory pool for ML models (6MB in PSRAM)
uint8_t* mlPool = nullptr;

// Current application state
BadgeState currentState = STATE_BOOT;
unsigned long lastActivityTime = 0;

// Wake word active flag (disabled during certain states)
bool wakeWordActive = false;

// Mode switches
bool voiceDisabled = false;  // BTN1: when true, logo loops forever

//==============================================================================
// Memory Pool Management
//==============================================================================

bool initMemoryPool() {
    mlPool = (uint8_t*)ps_malloc(ML_POOL_SIZE);
    if (!mlPool) {
        Serial.println("ERROR: ML pool allocation failed");
        return false;
    }
    Serial.printf("ML Pool: %dKB allocated\n", ML_POOL_SIZE / 1024);
    return true;
}

//==============================================================================
// Mode Switch Helpers
//==============================================================================

void initSwitches() {
    // Configure output pins (always LOW)
    pinMode(BTN1_OUT, OUTPUT);
    digitalWrite(BTN1_OUT, LOW);
    pinMode(BTN2_OUT, OUTPUT);
    digitalWrite(BTN2_OUT, LOW);

    // Configure input pins with internal pull-up
    pinMode(BTN1_IN, INPUT_PULLUP);
    pinMode(BTN2_IN, INPUT_PULLUP);
}

bool isBTN1Bridged() {
    // Returns true when switch is bridged (input pulled LOW by output)
    return digitalRead(BTN1_IN) == LOW;
}

bool isBTN2Bridged() {
    // Returns true when switch is bridged (input pulled LOW by output)
    return digitalRead(BTN2_IN) == LOW;
}

void enterDeepSleep() {
    Serial.println("Entering deep sleep mode...");

    // Turn off display backlight
    digitalWrite(LCD_BL, LOW);

    // Configure BTN2_IN as wake-up source (when switch is opened, pin goes HIGH)
    // Wake on HIGH (switch opened)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN2_IN, HIGH);

    // Enter deep sleep
    Serial.println("Sleeping...");
    delay(100);  // Let serial flush
    esp_deep_sleep_start();
}

//==============================================================================
// Configuration Loading
//==============================================================================

bool loadConfig() {
    File f = SD.open(CONFIG_PATH);
    if (!f) {
        Serial.println("Config: Using defaults (no config.json)");
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, f);
    f.close();

    if (error) {
        Serial.printf("Config: Parse error - %s\n", error.c_str());
        return false;
    }

    // Load values with defaults as fallback
    runtimeConfig.wakeWordThreshold = doc["wake_word_threshold"] | DEFAULT_WAKE_THRESHOLD;
    runtimeConfig.embedThreshold = doc["embed_threshold"] | DEFAULT_EMBED_THRESHOLD;
    runtimeConfig.llmTemperature = doc["llm_temperature"] | DEFAULT_LLM_TEMPERATURE;
    runtimeConfig.llmTopP = doc["llm_topp"] | DEFAULT_LLM_TOPP;
    runtimeConfig.llmMaxTokens = doc["llm_max_tokens"] | 128;
    runtimeConfig.idleSleepMs = doc["idle_sleep_ms"] | IDLE_SLEEP_MS;
    runtimeConfig.recordDurationMs = doc["record_duration_ms"] | RECORD_DURATION_MS;

    Serial.println("Config: Loaded from SD card");
    Serial.printf("  embed_threshold: %.2f\n", runtimeConfig.embedThreshold);
    Serial.printf("  llm_temperature: %.2f\n", runtimeConfig.llmTemperature);
    Serial.printf("  llm_max_tokens: %d\n", runtimeConfig.llmMaxTokens);

    return true;
}

//==============================================================================
// LLM Token Callback (Phase 4)
//==============================================================================

void onLLMToken(const char* token, void* userData) {
    // Append token to response buffer and display
    llmResponse += token;
    video.appendText(token);
    Serial.print(token);  // Also print to serial for debugging
}

//==============================================================================
// State Machine Helpers
//==============================================================================

void setState(BadgeState newState) {
    if (currentState == newState) return;

    Serial.printf("State: %d -> %d\n", currentState, newState);

    // Exit current state
    switch (currentState) {
        case STATE_LOGO_LOOP:
            // Stop wake word detection and mic stream
            if (wakeWordActive) {
                wakeWord.end();
                micStream.end();
                wakeWordActive = false;
            }
            break;
        case STATE_DISPLAY_RESPONSE:
            video.hideText();
            break;
        case STATE_RECORDING:
            recorder.stopRecording();
            break;
        default:
            break;
    }

    currentState = newState;
    lastActivityTime = millis();

    // Enter new state
    switch (newState) {
        case STATE_LOGO_LOOP:
            video.resume();
            // Start wake word detection (unless voice disabled by BTN1)
            if (!voiceDisabled && mlPool) {
                if (micStream.begin()) {
                    if (wakeWord.begin(mlPool, ML_POOL_SIZE, WAKE_MODEL_PATH, WAKE_CONFIG_PATH)) {
                        wakeWordActive = true;
                        Serial.printf("Listening for \"%s\"...\n", wakeWord.getWakeWord());
                        // Refresh display after SD card access (wake word model loading)
                        video.refreshDisplay();
                    } else {
                        Serial.println("Wake word init failed");
                        micStream.end();
                    }
                } else {
                    Serial.println("Mic stream init failed");
                }
            } else if (voiceDisabled) {
                Serial.println("Voice disabled - logo loop only");
            }
            break;
        case STATE_RECORDING:
            // Pause video during recording
            video.pause();
            recorder.startRecording(runtimeConfig.recordDurationMs / 1000.0f);
            break;
        case STATE_DISPLAY_RESPONSE:
            // Keep logo playing - response already spoken via TTS
            video.resume();
            video.hideText();
            break;
        case STATE_EMBEDDING:
            // Keep video paused during processing (no text - looks cleaner)
            video.pause();
            video.hideText();
            printPSRAM("before audio encoder");
            // Load audio encoder model
            if (!audioEmbed.begin(mlPool, ML_POOL_SIZE, AUDIO_ENCODER_PATH)) {
                Serial.println("Audio encoder init failed - returning to logo");
                setState(STATE_TTS_SORRY);
            } else {
                printPSRAM("after audio encoder");
            }
            break;
        case STATE_SIMILARITY:
            // Audio encoder unloaded, search against embeddings
            audioEmbed.end();
            // Keep video paused (no text)
            break;
        case STATE_TTS_SORRY:
            // Keep logo playing - just speak the apology, no text needed
            video.resume();
            video.hideText();
            ttsSpeaking = true;
            break;
        case STATE_TTS_OUTPUT:
            // Pause video to show LLM response text
            video.pause();
            video.refreshDisplay();
            video.fillScreen(0x0000);  // BLACK background for text
            if (llmResponse.length() > 0) {
                video.showText(llmResponse.c_str());
            }
            ttsSpeaking = true;
            break;
        case STATE_LLM_INFERENCE:
            // Pause video to show streaming LLM text
            video.pause();
            video.refreshDisplay();
            video.fillScreen(0x0000);  // BLACK background for text
            video.clearText();
            video.showText("");  // Initialize empty text box
            llmResponse = "";
            llmGenerating = true;

            // CRITICAL: Free everything possible to make room for LLM model (~7MB needed)
            // The search result is already saved in lastSearchResult.intent
            Serial.println("Freeing memory for LLM...");

            // Free embeddings (~2MB)
            embedSearch.end();
            printPSRAM("after embed unload");

            // Free ML pool (~1MB) - LLM doesn't use it
            if (mlPool) {
                free(mlPool);
                mlPool = nullptr;
                Serial.println("Freed ML pool");
            }

            // Free video buffer (~1.2MB) - we only need display for text
            video.unloadVideo();
            printPSRAM("after video unload");

            // Load LLM model
            Serial.println("Loading LLM model...");
            if (!llmInference.begin(nullptr, 0, LLM_MODEL_PATH, TOKENIZER_PATH)) {
                Serial.println("LLM init failed - showing intent only");
                video.showText(lastSearchResult.intent);
                llmGenerating = false;
                // Reload video buffer
                video.reloadVideo();
                // Reload embeddings since LLM failed
                embedSearch.begin(EMBEDDINGS_PATH, INTENTS_PATH);
                // Reallocate ML pool
                if (!mlPool) {
                    mlPool = (uint8_t*)ps_malloc(ML_POOL_SIZE);
                }
                setState(STATE_DISPLAY_RESPONSE);
            } else {
                // Apply runtime config
                llmInference.setTemperature(runtimeConfig.llmTemperature);
                llmInference.setTopP(runtimeConfig.llmTopP);
                llmInference.setMaxTokens(runtimeConfig.llmMaxTokens);
            }
            break;
        default:
            break;
    }
}

void resetActivityTimer() {
    lastActivityTime = millis();
}

//==============================================================================
// State Handlers
//==============================================================================

void handleLogoLoop() {
    // Update orientation
    orientation.update();
    if (orientation.hasChanged()) {
        video.setRotation(orientation.getRotation());
    }

    // Play video frame
    video.play();

    // Check wake word if active
    if (wakeWordActive) {
        if (micStream.update()) {
            // New features available, run wake word detection
            if (wakeWord.detect(micStream.getFeatures())) {
                Serial.println(">>> WAKE WORD DETECTED! <<<");
                setState(STATE_RECORDING);
            }
        }
    }
}

// Counter for recording filenames
static int recordingCounter = 0;

void handleRecording() {
    // Update recording (video paused)
    if (recorder.update()) {
        // Recording complete
        Serial.printf("Recording done: %d samples (%.2fs)\n",
                      recorder.getSampleCount(), recorder.getDurationSec());

        // Save recording to SD card for debugging
        char filepath[64];
        snprintf(filepath, sizeof(filepath), "/recordings/rec_%04d.wav", recordingCounter++);

        // Create recordings directory if needed
        if (!SD.exists("/recordings")) {
            SD.mkdir("/recordings");
        }
        recorder.saveToSD(filepath);

        // Phase 3: Extract embedding from recording
        setState(STATE_EMBEDDING);
    }
}

void handleEmbedding() {
    // Run audio embedding extraction (blocking operation)
    if (audioEmbed.embed(recorder.getBuffer(), recorder.getSampleCount(), queryEmbedding)) {
        Serial.println("Embedding extracted successfully");
        setState(STATE_SIMILARITY);
    } else {
        Serial.println("Embedding extraction failed");
        setState(STATE_TTS_SORRY);
    }
}

void handleSimilarity() {
    // Search for closest embedding
    lastSearchResult = embedSearch.search(queryEmbedding);

    Serial.printf("Best match: %s (score: %.3f)\n",
                  lastSearchResult.intent, lastSearchResult.score);

    if (lastSearchResult.score >= runtimeConfig.embedThreshold) {
        // Good match - run LLM inference
        Serial.println("High confidence match!");
        setState(STATE_LLM_INFERENCE);
    } else {
        // Low confidence - stash and apologize
        Serial.println("Low confidence - stashing");
        // TODO Phase 8: stash audio and embedding
        setState(STATE_TTS_SORRY);
    }
}

void handleLLMInference() {
    if (!llmGenerating) {
        // Generation done - shouldn't normally get here as we transition immediately
        setState(STATE_TTS_OUTPUT);
        return;
    }

    // Run LLM generation (blocking, but streams tokens via callback)
    Serial.printf("\nLLM generating response for: %s\n", lastSearchResult.intent);
    int tokensGenerated = llmInference.generate(lastSearchResult.intent, onLLMToken, nullptr);
    Serial.printf("\nLLM done: %d tokens\n", tokensGenerated);

    llmGenerating = false;

    // Unload LLM to free memory
    llmInference.end();
    printPSRAM("after LLM unload");

    // Reload video buffer
    Serial.println("Reloading video...");
    if (!video.reloadVideo()) {
        Serial.println("WARNING: Failed to reload video!");
    }
    printPSRAM("after video reload");

    // Reload embeddings now that LLM is unloaded
    Serial.println("Reloading embeddings...");
    if (!embedSearch.begin(EMBEDDINGS_PATH, INTENTS_PATH)) {
        Serial.println("WARNING: Failed to reload embeddings!");
    }
    printPSRAM("after embed reload");

    // Reallocate ML pool for wake word / audio embed
    if (!mlPool) {
        mlPool = (uint8_t*)ps_malloc(ML_POOL_SIZE);
        if (mlPool) {
            Serial.println("ML pool reallocated");
        } else {
            Serial.println("WARNING: ML pool reallocation failed!");
        }
    }
    printPSRAM("after pool realloc");

    // If no tokens generated, use the intent as fallback
    if (tokensGenerated == 0 && llmResponse.length() == 0) {
        llmResponse = lastSearchResult.intent;
    }

    // Transition to TTS to speak the response
    setState(STATE_TTS_OUTPUT);
}

void handleDisplayResponse() {
    // Auto-return to logo loop after 3 seconds
    if (millis() - lastActivityTime > 3000) {
        setState(STATE_LOGO_LOOP);
    }
}

void handleTtsSorry() {
    if (ttsSpeaking) {
        // Speak apology (blocking)
        tts.speak("Sorry, I could not understand that.");
        ttsSpeaking = false;
        lastActivityTime = millis();
    }

    // Wait a bit after speaking, then return to logo
    if (millis() - lastActivityTime > 1000) {
        setState(STATE_LOGO_LOOP);
    }
}

void handleTtsOutput() {
    if (ttsSpeaking) {
        // Speak the LLM response (blocking)
        if (llmResponse.length() > 0) {
            tts.speak(llmResponse.c_str());
        }
        ttsSpeaking = false;
        lastActivityTime = millis();
    }

    // Wait a bit after speaking, then return to logo
    if (millis() - lastActivityTime > 1000) {
        setState(STATE_LOGO_LOOP);
    }
}

//==============================================================================
// Main Loop State Machine
//==============================================================================

void loop() {
    // Check mode switches first
    // BTN2: Deep sleep when bridged
    if (isBTN2Bridged()) {
        enterDeepSleep();
        // Won't reach here - ESP will reset on wake
    }

    // BTN1: Voice disable - check and update flag
    bool btn1State = isBTN1Bridged();
    if (btn1State != voiceDisabled) {
        voiceDisabled = btn1State;
        if (voiceDisabled) {
            Serial.println("BTN1: Voice DISABLED");
            // Stop wake word if currently active
            if (wakeWordActive) {
                wakeWord.end();
                micStream.end();
                wakeWordActive = false;
            }
        } else {
            Serial.println("BTN1: Voice ENABLED");
            // Will start wake word on next LOGO_LOOP transition
        }
    }

    // State-specific handling
    switch (currentState) {
        case STATE_BOOT:
            // Initialization complete, transition to logo loop
            setState(STATE_LOGO_LOOP);
            break;

        case STATE_LOGO_LOOP:
            handleLogoLoop();
            break;

        case STATE_RECORDING:
            handleRecording();
            break;

        case STATE_DISPLAY_RESPONSE:
            handleDisplayResponse();
            break;

        case STATE_EMBEDDING:
            handleEmbedding();
            break;

        case STATE_SIMILARITY:
            handleSimilarity();
            break;

        case STATE_TTS_SORRY:
            handleTtsSorry();
            break;

        case STATE_LLM_INFERENCE:
            handleLLMInference();
            break;

        case STATE_TTS_OUTPUT:
            handleTtsOutput();
            break;

        // Future states (Phase 6+)
        case STATE_STASH_DATA:
        case STATE_DEEP_SLEEP:
            // Not implemented yet - return to logo
            setState(STATE_LOGO_LOOP);
            break;
    }
}

//==============================================================================
// Debug Helper
//==============================================================================

void printPSRAM(const char* label) {
    Serial.printf("PSRAM [%s]: %dKB free / %dKB total\n",
                  label,
                  ESP.getFreePsram() / 1024,
                  ESP.getPsramSize() / 1024);
}

//==============================================================================
// Setup
//==============================================================================

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== OAISYS25 Badge Starting ===");
    printPSRAM("boot");

    // Initialize mode switches first (before any other init)
    initSwitches();

    // Check if BTN2 is bridged - go to deep sleep immediately
    if (isBTN2Bridged()) {
        Serial.println("BTN2 bridged - entering deep sleep");
        enterDeepSleep();
    }

    // Check BTN1 for voice disable mode
    voiceDisabled = isBTN1Bridged();
    if (voiceDisabled) {
        Serial.println("BTN1 bridged - voice detection DISABLED");
    }

    // Initialize orientation sensor (non-fatal if fails)
    if (!orientation.begin()) {
        Serial.println("IMU init failed - rotation disabled");
    } else {
        Serial.println("IMU: OK");
    }

    // Initialize video player FIRST (this also inits display and SD card)
    // Must be before ML pool to avoid PSRAM fragmentation
    Serial.println("Loading video...");
    if (!video.begin()) {
        Serial.println("ERROR: Video init failed!");
        // Blink LED to indicate error
        pinMode(LCD_BL, OUTPUT);
        while (1) {
            digitalWrite(LCD_BL, !digitalRead(LCD_BL));
            delay(500);
        }
    }
    Serial.println("Video: OK");
    printPSRAM("after video");

    // Load runtime configuration from SD card
    loadConfig();

    // Initialize audio recorder (allocates PSRAM buffer)
    if (!recorder.begin()) {
        Serial.println("WARNING: Audio recorder init failed!");
    } else {
        Serial.println("Recorder: OK");
    }
    printPSRAM("after recorder");

    // Phase 3: Allocate query embedding buffer (16-byte aligned for ESP-DSP SIMD)
    queryEmbedding = (float*)heap_caps_aligned_alloc(16,
        AUDIO_EMBEDDING_DIM * sizeof(float), MALLOC_CAP_SPIRAM);
    if (!queryEmbedding) {
        Serial.println("WARNING: Query embedding buffer alloc failed!");
    } else {
        Serial.println("Query buffer: OK (aligned)");
    }

    // Phase 3: Load embedding search database (stays in memory)
    // IMPORTANT: Load embeddings BEFORE ML pool - they stay resident permanently
    if (!embedSearch.begin(EMBEDDINGS_PATH, INTENTS_PATH)) {
        Serial.println("WARNING: Embedding search init failed!");
        Serial.println("  Make sure SD card has /data/embeddings.bin and /data/intents.txt");
    } else {
        Serial.printf("EmbedSearch: %d intents loaded\n", embedSearch.getCount());
    }
    printPSRAM("after embeddings");

    // Allocate ML memory pool (after embeddings to ensure they get contiguous memory)
    // Remaining PSRAM is used for ML models that load/unload dynamically
    if (!initMemoryPool()) {
        Serial.println("WARNING: ML features disabled (no pool)");
    }
    printPSRAM("after ML pool");

    // Phase 5: Initialize TTS (non-fatal if fails)
    if (!tts.begin()) {
        Serial.println("WARNING: TTS init failed - speech disabled");
    } else {
        Serial.println("TTS: OK (SAM robotic voice)");
        // Say hello on startup
        tts.speak("Hello. I am ready.");
    }

    // Ready to go
    lastActivityTime = millis();
    currentState = STATE_BOOT;

    Serial.println("\n=== Badge Ready (Phase 5) ===");
    printPSRAM("final");
    Serial.println("Say 'Hey Daisy' to start recording");
    Serial.println("Recording -> Embedding -> Similarity -> LLM -> TTS");
}
