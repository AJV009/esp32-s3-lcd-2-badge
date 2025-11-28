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
 ******************************************************************************/

#include "config.h"
#include "src/display/video_player.h"
#include "src/sensors/orientation.h"
#include "src/audio/mic_stream.h"
#include "src/audio/audio_recorder.h"
#include "src/ml/wake_word.h"

#include <SD.h>
#include <SPI.h>

//==============================================================================
// Global Objects
//==============================================================================

VideoPlayer video;
OrientationManager orientation;

// Phase 2: Audio and wake word
MicStream micStream;
AudioRecorder recorder;
WakeWordDetector wakeWord;

// Memory pool for ML models (6MB in PSRAM)
uint8_t* mlPool = nullptr;

// Current application state
BadgeState currentState = STATE_BOOT;
unsigned long lastActivityTime = 0;

// Wake word active flag (disabled during certain states)
bool wakeWordActive = false;

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
            // Start wake word detection
            if (mlPool) {
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
            }
            break;
        case STATE_RECORDING:
            video.pause();
            video.refreshDisplay();  // Ensure display works after any SD access
            // Fill screen RED to indicate recording (text overlay TBD)
            video.fillScreen(0xF800);  // RED in RGB565
            recorder.startRecording(3.0f);  // 3 second recording
            break;
        case STATE_DISPLAY_RESPONSE:
            video.pause();
            video.refreshDisplay();  // Ensure display works
            // Fill screen GREEN to indicate done (text overlay TBD)
            video.fillScreen(0x07E0);  // GREEN in RGB565
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

void handleRecording() {
    // Update recording
    if (recorder.update()) {
        // Recording complete
        Serial.printf("Recording done: %d samples (%.2fs)\n",
                      recorder.getSampleCount(), recorder.getDurationSec());

        // For Phase 2, show green screen then return to logo
        // Phase 3 will add embedding extraction
        setState(STATE_DISPLAY_RESPONSE);
    }
}

void handleDisplayResponse() {
    // Auto-return to logo loop after 2 seconds
    if (millis() - lastActivityTime > 2000) {
        setState(STATE_LOGO_LOOP);
    }
}

//==============================================================================
// Main Loop State Machine
//==============================================================================

void loop() {
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

        // Future states (Phase 3+)
        case STATE_EMBEDDING:
        case STATE_SIMILARITY:
        case STATE_LLM_INFERENCE:
        case STATE_TTS_OUTPUT:
        case STATE_STASH_DATA:
        case STATE_TTS_SORRY:
        case STATE_DEEP_SLEEP:
            // Not implemented yet - return to logo
            setState(STATE_LOGO_LOOP);
            break;
    }
}

//==============================================================================
// Setup
//==============================================================================

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== OAISYS25 Badge Starting ===");

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

    // Allocate ML memory pool (after video to avoid fragmentation)
    if (!initMemoryPool()) {
        Serial.println("WARNING: ML features disabled (no pool)");
    }

    // Initialize audio recorder (allocates PSRAM buffer)
    if (!recorder.begin()) {
        Serial.println("WARNING: Audio recorder init failed!");
    } else {
        Serial.println("Recorder: OK");
    }

    // Ready to go
    lastActivityTime = millis();
    currentState = STATE_BOOT;

    Serial.println("\n=== Badge Ready (Phase 2) ===");
    Serial.println("Say 'Hey Daisy' to start recording");
}
