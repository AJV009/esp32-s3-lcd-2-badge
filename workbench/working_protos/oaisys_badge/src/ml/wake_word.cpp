/*******************************************************************************
 * OAISYS Badge - Wake Word Detection Implementation
 ******************************************************************************/

#include "wake_word.h"
#include "../../config.h"
#include "../audio/mic_stream.h"  // For FEATURE_* constants

#include <ArduinoJson.h>
#include <SD.h>

// EdgeNeuron includes
#include <EdgeNeuron.h>
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"

// TFLite resolver
static tflite::AllOpsResolver resolver;

WakeWordDetector::WakeWordDetector()
    : _modelData(nullptr)
    , _modelSize(0)
    , _tensorArena(nullptr)
    , _varArena(nullptr)
    , _varAllocator(nullptr)
    , _resourceVars(nullptr)
    , _interpreter(nullptr)
    , _inputTensor(nullptr)
    , _outputTensor(nullptr)
    , _windowPos(0)
    , _windowFilled(false)
    , _lastDetectionMs(0)
    , _lastProbability(0.0f)
    , _loaded(false)
{
    // Default config
    strcpy(_config.wake_word, "Hey Daisy");
    _config.probability_cutoff = 0.5f;
    _config.sliding_window_size = 10;
    _config.min_high_frames = 6;
    _config.min_frame_prob = 0.5f;
    _config.cooldown_ms = 1500;

    memset(_probabilityWindow, 0, sizeof(_probabilityWindow));
}

WakeWordDetector::~WakeWordDetector() {
    end();
}

bool WakeWordDetector::begin(uint8_t* pool, size_t poolSize,
                              const char* modelPath, const char* configPath) {
    if (_loaded) return true;

    // Check pool size
    if (poolSize < TENSOR_ARENA_SIZE + VAR_ARENA_SIZE) {
        Serial.println("WakeWord: Pool too small");
        return false;
    }

    // Assign pool regions
    _tensorArena = pool;
    _varArena = pool + TENSOR_ARENA_SIZE;

    // Load config (optional - uses defaults if fails)
    if (configPath) {
        _loadConfig(configPath);
    }

    // Load model from SD
    if (!_loadModel(modelPath)) {
        Serial.println("WakeWord: Model load failed");
        return false;
    }

    // Initialize TFLite interpreter
    if (!_initInterpreter()) {
        Serial.println("WakeWord: Interpreter init failed");
        if (_modelData) {
            free(_modelData);
            _modelData = nullptr;
        }
        return false;
    }

    _resetWindow();
    _loaded = true;
    // Start with extended cooldown (3 sec) to let mic stabilize and avoid false triggers
    _lastDetectionMs = millis() + 1500;  // Extra 1.5s beyond normal cooldown

    Serial.printf("WakeWord: Loaded \"%s\" (%.1fKB model)\n",
                  _config.wake_word, _modelSize / 1024.0f);
    return true;
}

void WakeWordDetector::end() {
    // Interpreter is static, just reset pointers
    _interpreter = nullptr;
    _inputTensor = nullptr;
    _outputTensor = nullptr;
    _varAllocator = nullptr;
    _resourceVars = nullptr;

    if (_modelData) {
        free(_modelData);
        _modelData = nullptr;
    }

    _tensorArena = nullptr;
    _varArena = nullptr;
    _loaded = false;
}

bool WakeWordDetector::_loadConfig(const char* path) {
    File f = SD.open(path);
    if (!f) {
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, f);
    f.close();

    if (error) {
        return false;
    }

    // Load wake word name
    const char* wake_word = doc["wake_word"] | "Hey Daisy";
    strncpy(_config.wake_word, wake_word, sizeof(_config.wake_word) - 1);
    _config.wake_word[sizeof(_config.wake_word) - 1] = '\0';

    // Load detection parameters from "micro" object
    JsonObject micro = doc["micro"];
    if (micro) {
        _config.probability_cutoff = micro["probability_cutoff"] | 0.5f;
        _config.sliding_window_size = micro["sliding_window_average_size"] | 10;
        _config.min_high_frames = micro["min_high_frames"] | 6;
        _config.min_frame_prob = micro["min_frame_prob"] | 0.5f;
        _config.cooldown_ms = micro["cooldown_ms"] | 1500;
    }

    // Clamp sliding window size
    if (_config.sliding_window_size > MAX_SLIDING_WINDOW) {
        _config.sliding_window_size = MAX_SLIDING_WINDOW;
    }

    return true;
}

bool WakeWordDetector::_loadModel(const char* path) {
    File f = SD.open(path);
    if (!f) {
        Serial.printf("WakeWord: Cannot open %s\n", path);
        return false;
    }

    _modelSize = f.size();
    _modelData = (uint8_t*)ps_malloc(_modelSize);
    if (!_modelData) {
        Serial.println("WakeWord: Model alloc failed");
        f.close();
        return false;
    }

    if (f.read(_modelData, _modelSize) != _modelSize) {
        free(_modelData);
        _modelData = nullptr;
        f.close();
        return false;
    }
    f.close();
    return true;
}

bool WakeWordDetector::_initInterpreter() {
    const tflite::Model* model = tflite::GetModel(_modelData);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        Serial.printf("WakeWord: Model version mismatch: %d vs %d\n",
                      model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    _varAllocator = tflite::MicroAllocator::Create(_varArena, VAR_ARENA_SIZE);
    if (!_varAllocator) {
        Serial.println("WakeWord: Var allocator failed");
        return false;
    }

    _resourceVars = tflite::MicroResourceVariables::Create(_varAllocator, MAX_RESOURCE_VARS);
    if (!_resourceVars) {
        Serial.println("WakeWord: Resource vars failed");
        return false;
    }

    // Static interpreter to avoid memory fragmentation
    static tflite::MicroInterpreter static_interpreter(
        model, resolver, _tensorArena, TENSOR_ARENA_SIZE, _resourceVars);
    _interpreter = &static_interpreter;

    if (_interpreter->AllocateTensors() != kTfLiteOk) {
        Serial.println("WakeWord: AllocateTensors failed");
        return false;
    }

    _inputTensor = _interpreter->input(0);
    _outputTensor = _interpreter->output(0);

    if (!_inputTensor || !_outputTensor) {
        Serial.println("WakeWord: Failed to get tensors");
        return false;
    }

    return true;
}

float WakeWordDetector::_runInference(const uint16_t* features) {
    int total_features = FEATURE_FRAMES * FEATURE_BINS;

    // Quantize features to input tensor format
    if (_inputTensor->type == kTfLiteInt8) {
        int8_t* input_data = _inputTensor->data.int8;
        for (int i = 0; i < total_features; i++) {
            int32_t scaled = ((int32_t)features[i] * 256 + 333) / 666 - 128;
            input_data[i] = constrain(scaled, -128, 127);
        }
    } else {
        // Float input
        float* input_data = _inputTensor->data.f;
        for (int i = 0; i < total_features; i++) {
            input_data[i] = features[i] / 25.6f;
        }
    }

    // Run inference
    if (_interpreter->Invoke() != kTfLiteOk) {
        return 0.0f;
    }

    // Dequantize output
    float probability;
    if (_outputTensor->type == kTfLiteUInt8) {
        uint8_t output_value = _outputTensor->data.uint8[0];
        probability = (output_value - _outputTensor->params.zero_point) *
                      _outputTensor->params.scale;
    } else if (_outputTensor->type == kTfLiteInt8) {
        int8_t output_value = _outputTensor->data.int8[0];
        probability = (output_value - _outputTensor->params.zero_point) *
                      _outputTensor->params.scale;
    } else {
        probability = _outputTensor->data.f[0];
    }

    return probability;
}

bool WakeWordDetector::detect(const uint16_t* features) {
    if (!_loaded) return false;

    // Run inference
    float prob = _runInference(features);
    _lastProbability = prob;

    // Update sliding window
    _probabilityWindow[_windowPos] = prob;
    _windowPos = (_windowPos + 1) % _config.sliding_window_size;

    if (_windowPos == 0) {
        _windowFilled = true;
    }

    // Need full window before detection
    if (!_windowFilled) {
        return false;
    }

    // Check cooldown
    if (millis() - _lastDetectionMs < (unsigned long)_config.cooldown_ms) {
        return false;
    }

    // Calculate average and count high frames
    float avg = 0.0f;
    int high_frames = 0;
    for (int i = 0; i < _config.sliding_window_size; i++) {
        avg += _probabilityWindow[i];
        if (_probabilityWindow[i] >= _config.min_frame_prob) {
            high_frames++;
        }
    }
    avg /= _config.sliding_window_size;

    // Detection criteria
    if (avg >= _config.probability_cutoff && high_frames >= _config.min_high_frames) {
        _lastDetectionMs = millis();
        _resetWindow();
        return true;
    }

    return false;
}

float WakeWordDetector::getAverageProbability() const {
    if (!_windowFilled) return 0.0f;

    float sum = 0.0f;
    for (int i = 0; i < _config.sliding_window_size; i++) {
        sum += _probabilityWindow[i];
    }
    return sum / _config.sliding_window_size;
}

void WakeWordDetector::_resetWindow() {
    for (int i = 0; i < MAX_SLIDING_WINDOW; i++) {
        _probabilityWindow[i] = 0.0f;
    }
    _windowPos = 0;
    _windowFilled = false;
}
