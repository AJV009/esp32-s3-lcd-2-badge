/*******************************************************************************
 * OAISYS Badge - Wake Word Detection Module
 * TFLite inference with sliding window probability detection
 ******************************************************************************/

#pragma once

#include <Arduino.h>

// Forward declarations
namespace tflite {
    class MicroInterpreter;
    class MicroAllocator;
    class MicroResourceVariables;
}
struct TfLiteTensor;

//==============================================================================
// Wake Word Configuration (loaded from JSON)
//==============================================================================
struct WakeWordConfig {
    char wake_word[32];
    float probability_cutoff;
    int sliding_window_size;
    int min_high_frames;
    float min_frame_prob;
    int cooldown_ms;
};

//==============================================================================
// WakeWordDetector - Handles wake word model inference and detection
//==============================================================================
class WakeWordDetector {
public:
    WakeWordDetector();
    ~WakeWordDetector();

    // Lifecycle - uses provided memory pool
    bool begin(uint8_t* pool, size_t poolSize,
               const char* modelPath, const char* configPath);
    void end();

    // Run inference on feature buffer
    // features: FEATURE_FRAMES * FEATURE_BINS uint16_t values
    // Returns true if wake word detected
    bool detect(const uint16_t* features);

    // Get last inference probability
    float getLastProbability() const { return _lastProbability; }

    // Get average probability from sliding window
    float getAverageProbability() const;

    // Get wake word name
    const char* getWakeWord() const { return _config.wake_word; }

    // Check if loaded
    bool isLoaded() const { return _loaded; }

    // Memory requirement for pool
    static size_t requiredMemory() { return TENSOR_ARENA_SIZE + VAR_ARENA_SIZE; }

private:
    // Memory sizes (with 2KB padding for alignment headroom)
    static constexpr size_t TENSOR_ARENA_SIZE = 502000;
    static constexpr size_t VAR_ARENA_SIZE = 52000;
    static constexpr int MAX_RESOURCE_VARS = 100;
    static constexpr int MAX_SLIDING_WINDOW = 20;

    // Configuration
    WakeWordConfig _config;

    // Model data
    uint8_t* _modelData;
    uint32_t _modelSize;

    // TFLite components
    uint8_t* _tensorArena;
    uint8_t* _varArena;
    tflite::MicroAllocator* _varAllocator;
    tflite::MicroResourceVariables* _resourceVars;
    tflite::MicroInterpreter* _interpreter;
    TfLiteTensor* _inputTensor;
    TfLiteTensor* _outputTensor;

    // Detection state
    float _probabilityWindow[MAX_SLIDING_WINDOW];
    int _windowPos;
    bool _windowFilled;
    unsigned long _lastDetectionMs;
    float _lastProbability;

    // State
    bool _loaded;

    // Internal helpers
    bool _loadConfig(const char* path);
    bool _loadModel(const char* path);
    bool _initInterpreter();
    float _runInference(const uint16_t* features);
    void _resetWindow();
};
