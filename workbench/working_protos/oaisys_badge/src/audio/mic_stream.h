/*******************************************************************************
 * OAISYS Badge - Microphone Stream Module
 * Continuous I2S audio capture with microfrontend feature extraction
 ******************************************************************************/

#pragma once

#include <Arduino.h>

// Forward declaration for microfrontend state
struct FrontendState;

//==============================================================================
// Feature extraction constants (matches microWakeWord training)
//==============================================================================
#define FEATURE_WINDOW_MS   30
#define FEATURE_STRIDE_MS   10
#define FEATURE_BINS        40
#define FEATURE_FRAMES      3

//==============================================================================
// MicStream - Handles I2S capture and audio feature extraction
//==============================================================================
class MicStream {
public:
    MicStream();
    ~MicStream();

    // Lifecycle
    bool begin();
    void end();

    // Call in loop - captures audio and extracts features
    // Returns true when a new feature frame is ready
    bool update();

    // Get the current feature buffer (FEATURE_FRAMES * FEATURE_BINS uint16_t values)
    // Only valid after update() returns true
    const uint16_t* getFeatures() const { return _featureBuffer; }

    // Get raw audio samples for recording (call after update())
    // Returns number of new samples available
    int getNewSamples(int16_t* buffer, size_t maxSamples);

    // Check if stream is running
    bool isRunning() const { return _running; }

    // Debug: Get current audio amplitude
    int32_t getAmplitude() const { return _amplitude; }

private:
    // Audio buffer
    static constexpr int AUDIO_BUFFER_SIZE = 1024;
    int16_t* _audioBuffer;
    int _audioBufferPos;

    // Feature buffer (3 frames × 40 bins)
    uint16_t _featureBuffer[FEATURE_FRAMES * FEATURE_BINS];
    int _featureFrameCount;

    // I2S read buffer
    int32_t* _i2sBuffer;

    // Microfrontend state
    FrontendState* _frontendState;

    // State
    bool _running;
    int32_t _amplitude;

    // Internal helpers
    bool _initI2S();
    bool _initFrontend();
    bool _captureAudio();
};
