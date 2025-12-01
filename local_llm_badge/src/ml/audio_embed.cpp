/*******************************************************************************
 * OAISYS Badge - Audio Embedding Implementation
 *
 * Small CNN model trained with contrastive learning to align audio with text.
 ******************************************************************************/

#include "audio_embed.h"
#include "../../config.h"

#include <new>  // For placement new
#include <SD.h>
#include <esp_dsp.h>
#include <dsps_dotprod.h>  // For dsps_dotprod_f32_aes3 SIMD
#include <esp_heap_caps.h>  // For aligned PSRAM allocation
#include <math.h>

// TensorFlow Lite includes (via EdgeNeuron library, same as wake_word.cpp)
#include <EdgeNeuron.h>
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

AudioEmbedder::AudioEmbedder()
    : _melFilterbank(nullptr)
    , _fftInput(nullptr)
    , _fftOutput(nullptr)
    , _window(nullptr)
    , _melFeatures(nullptr)
    , _melInitialized(false)
    , _modelData(nullptr)
    , _modelSize(0)
    , _tensorArena(nullptr)
    , _interpreter(nullptr)
    , _inputTensor(nullptr)
    , _outputTensor(nullptr)
    , _loaded(false)
{
}

AudioEmbedder::~AudioEmbedder() {
    end();
}

bool AudioEmbedder::begin(uint8_t* pool, size_t poolSize, const char* modelPath) {
    if (_loaded) return true;

    // Check pool size
    if (poolSize < AUDIO_TENSOR_ARENA_SIZE) {
        Serial.println("AudioEmbed: Pool too small");
        return false;
    }

    _tensorArena = pool;

    // Initialize mel-spectrogram
    if (!_initMelSpectrogram()) {
        Serial.println("AudioEmbed: Mel init failed");
        return false;
    }

    // Load model from SD
    if (!_loadModel(modelPath)) {
        Serial.println("AudioEmbed: Model load failed");
        _endMelSpectrogram();
        return false;
    }

    // Initialize TFLite interpreter
    if (!_initInterpreter()) {
        Serial.println("AudioEmbed: Interpreter init failed");
        if (_modelData) {
            free(_modelData);
            _modelData = nullptr;
        }
        _endMelSpectrogram();
        return false;
    }

    _loaded = true;
    Serial.printf("AudioEmbed: Loaded (%.1fKB model)\n", _modelSize / 1024.0f);
    return true;
}

void AudioEmbedder::end() {
    _interpreter = nullptr;
    _inputTensor = nullptr;
    _outputTensor = nullptr;
    _tensorArena = nullptr;

    if (_modelData) {
        free(_modelData);
        _modelData = nullptr;
    }

    _endMelSpectrogram();
    _loaded = false;
}

//==============================================================================
// Mel-Spectrogram Implementation
//==============================================================================

bool AudioEmbedder::_initMelSpectrogram() {
    int numFreqBins = AUDIO_FFT_SIZE / 2 + 1;

    // Allocate mel filterbank (16-byte aligned for ESP-DSP SIMD)
    size_t filterSize = AUDIO_MEL_BINS * numFreqBins * sizeof(float);
    _melFilterbank = (float*)heap_caps_aligned_alloc(16, filterSize, MALLOC_CAP_SPIRAM);
    if (!_melFilterbank) return false;

    // Allocate working buffers
    _fftInput = (float*)ps_malloc(AUDIO_FFT_SIZE * sizeof(float));
    _fftOutput = (float*)ps_malloc(AUDIO_FFT_SIZE * 2 * sizeof(float));
    _window = (float*)ps_malloc(AUDIO_FFT_SIZE * sizeof(float));
    _melFeatures = (float*)ps_malloc(AUDIO_MEL_BINS * AUDIO_MEL_FRAMES * sizeof(float));

    if (!_fftInput || !_fftOutput || !_window || !_melFeatures) {
        _endMelSpectrogram();
        return false;
    }

    // Initialize ESP-DSP FFT
    esp_err_t ret = dsps_fft2r_init_fc32(NULL, AUDIO_FFT_SIZE);
    if (ret != ESP_OK) {
        _endMelSpectrogram();
        return false;
    }

    // Create Hann window
    for (int i = 0; i < AUDIO_FFT_SIZE; i++) {
        _window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (AUDIO_FFT_SIZE - 1)));
    }

    // Initialize mel filterbank
    float minFreq = 125.0f;
    float maxFreq = 7500.0f;
    float minMel = _hzToMel(minFreq);
    float maxMel = _hzToMel(maxFreq);

    // Create mel filter centers
    float* melCenters = (float*)malloc((AUDIO_MEL_BINS + 2) * sizeof(float));
    for (int i = 0; i < AUDIO_MEL_BINS + 2; i++) {
        float mel = minMel + (maxMel - minMel) * i / (AUDIO_MEL_BINS + 1);
        melCenters[i] = _melToHz(mel);
    }

    // Build triangular filters
    float freqRes = (float)AUDIO_SAMPLE_RATE / AUDIO_FFT_SIZE;

    for (int m = 0; m < AUDIO_MEL_BINS; m++) {
        float left = melCenters[m];
        float center = melCenters[m + 1];
        float right = melCenters[m + 2];

        for (int k = 0; k < numFreqBins; k++) {
            float freq = k * freqRes;
            float weight = 0.0f;

            if (freq >= left && freq <= center) {
                weight = (freq - left) / (center - left);
            } else if (freq > center && freq <= right) {
                weight = (right - freq) / (right - center);
            }

            _melFilterbank[m * numFreqBins + k] = weight;
        }
    }

    free(melCenters);
    _melInitialized = true;
    return true;
}

void AudioEmbedder::_endMelSpectrogram() {
    if (_melFilterbank) { free(_melFilterbank); _melFilterbank = nullptr; }
    if (_fftInput) { free(_fftInput); _fftInput = nullptr; }
    if (_fftOutput) { free(_fftOutput); _fftOutput = nullptr; }
    if (_window) { free(_window); _window = nullptr; }
    if (_melFeatures) { free(_melFeatures); _melFeatures = nullptr; }
    _melInitialized = false;
}

bool AudioEmbedder::_computeMelSpectrogram(const int16_t* audio, size_t samples, float* melOutput) {
    if (!_melInitialized) return false;

    float globalMin = 1e10f;
    float globalMax = -1e10f;

    // Generate frames - output format: (bins, frames) = (64, 96)
    for (int frame = 0; frame < AUDIO_MEL_FRAMES; frame++) {
        int startIdx = frame * AUDIO_HOP_LENGTH;

        if (startIdx + AUDIO_FFT_SIZE > (int)samples) {
            // Zero-pad remaining frames
            for (int i = 0; i < AUDIO_MEL_BINS; i++) {
                melOutput[i * AUDIO_MEL_FRAMES + frame] = -80.0f;
            }
            continue;
        }

        // Compute power spectrum
        float powerSpectrum[AUDIO_FFT_SIZE / 2 + 1];
        _computeFFTFrame(audio, startIdx, powerSpectrum);

        // Apply mel filterbank
        float melFrame[AUDIO_MEL_BINS];
        _applyMelFilterbank(powerSpectrum, melFrame);

        // Store in (bins, frames) format for CNN input
        for (int i = 0; i < AUDIO_MEL_BINS; i++) {
            melOutput[i * AUDIO_MEL_FRAMES + frame] = melFrame[i];
            if (melFrame[i] < globalMin) globalMin = melFrame[i];
            if (melFrame[i] > globalMax) globalMax = melFrame[i];
        }
    }

    // Debug: Print mel-spectrogram stats BEFORE normalization
    Serial.printf("AudioEmbed: Mel-spec raw: min=%.2f, max=%.2f, range=%.2f\n",
                  globalMin, globalMax, globalMax - globalMin);

    // Normalize to [0, 1] range (like training)
    float range = globalMax - globalMin;
    if (range > 1e-8f) {
        for (int i = 0; i < AUDIO_MEL_BINS * AUDIO_MEL_FRAMES; i++) {
            melOutput[i] = (melOutput[i] - globalMin) / range;
        }
    }

    // Debug: Print a few values after normalization
    Serial.printf("AudioEmbed: Mel-spec normalized sample: [%.3f, %.3f, %.3f, %.3f]\n",
                  melOutput[0], melOutput[AUDIO_MEL_FRAMES],
                  melOutput[2*AUDIO_MEL_FRAMES], melOutput[3*AUDIO_MEL_FRAMES]);

    return true;
}

void AudioEmbedder::_computeFFTFrame(const int16_t* audio, int startIdx, float* powerSpectrum) {
    // Apply window and convert to float
    for (int i = 0; i < AUDIO_FFT_SIZE; i++) {
        _fftInput[i] = (float)audio[startIdx + i] * _window[i] / 32768.0f;
    }

    // Prepare complex input (real part only)
    for (int i = 0; i < AUDIO_FFT_SIZE; i++) {
        _fftOutput[i * 2] = _fftInput[i];
        _fftOutput[i * 2 + 1] = 0.0f;
    }

    // Compute FFT using ESP-DSP
    dsps_fft2r_fc32(_fftOutput, AUDIO_FFT_SIZE);
    dsps_bit_rev_fc32(_fftOutput, AUDIO_FFT_SIZE);

    // Compute power spectrum: |X[k]|^2
    for (int k = 0; k < AUDIO_FFT_SIZE / 2 + 1; k++) {
        float real = _fftOutput[k * 2];
        float imag = _fftOutput[k * 2 + 1];
        powerSpectrum[k] = real * real + imag * imag;
    }
}

void AudioEmbedder::_applyMelFilterbank(float* powerSpectrum, float* melOutput) {
    int numFreqBins = AUDIO_FFT_SIZE / 2 + 1;

    for (int m = 0; m < AUDIO_MEL_BINS; m++) {
        float sum = 0.0f;

        // ESP-DSP SIMD dot product for mel filter application
        // Each mel filter row dot-producted with power spectrum
        dsps_dotprod_f32_aes3(&_melFilterbank[m * numFreqBins],
                              powerSpectrum, &sum, numFreqBins);

        // Log transform
        melOutput[m] = log10f(sum + 1e-10f);
    }
}

float AudioEmbedder::_hzToMel(float hz) {
    return 2595.0f * log10f(1.0f + hz / 700.0f);
}

float AudioEmbedder::_melToHz(float mel) {
    return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

//==============================================================================
// TFLite Implementation
//==============================================================================

bool AudioEmbedder::_loadModel(const char* path) {
    File f = SD.open(path);
    if (!f) {
        Serial.printf("AudioEmbed: Cannot open %s\n", path);
        return false;
    }

    _modelSize = f.size();
    _modelData = (uint8_t*)ps_malloc(_modelSize);
    if (!_modelData) {
        Serial.println("AudioEmbed: Model alloc failed");
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

bool AudioEmbedder::_initInterpreter() {
    const tflite::Model* model = tflite::GetModel(_modelData);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        Serial.printf("AudioEmbed: Model version mismatch: %d vs %d\n",
                      model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    // CRITICAL: Clear the arena to reset MicroAllocator state from any previous model
    // This prevents "Model allocation started before finishing" errors
    memset(_tensorArena, 0, AUDIO_TENSOR_ARENA_SIZE);

    // Op resolver for small CNN audio encoder
    // Architecture: Conv2D -> MaxPool -> Conv2D -> MaxPool -> Conv2D -> MaxPool ->
    //               Conv2D -> GlobalAvgPool -> Dense -> L2Normalize
    // Static resolver persists - only add ops once to avoid "AddBuiltin" warnings
    static tflite::MicroMutableOpResolver<12> resolver;
    static bool opsAdded = false;
    if (!opsAdded) {
        resolver.AddConv2D();
        resolver.AddMaxPool2D();
        resolver.AddFullyConnected();
        resolver.AddReshape();
        resolver.AddMean();              // For GlobalAveragePooling2D
        resolver.AddL2Normalization();   // For L2 normalize layer
        resolver.AddQuantize();
        resolver.AddDequantize();
        resolver.AddRelu();              // Activation
        resolver.AddPad();
        resolver.AddSqueeze();
        resolver.AddExpandDims();
        opsAdded = true;
    }

    // Use placement new to properly reconstruct the interpreter each time
    // This ensures the constructor runs with fresh model and arena state
    static uint8_t interpreterBuffer[sizeof(tflite::MicroInterpreter)] __attribute__((aligned(16)));
    _interpreter = new (interpreterBuffer) tflite::MicroInterpreter(
        model, resolver, _tensorArena, AUDIO_TENSOR_ARENA_SIZE);

    if (_interpreter->AllocateTensors() != kTfLiteOk) {
        Serial.println("AudioEmbed: AllocateTensors failed");
        return false;
    }

    _inputTensor = _interpreter->input(0);
    _outputTensor = _interpreter->output(0);

    if (!_inputTensor || !_outputTensor) {
        Serial.println("AudioEmbed: Failed to get tensors");
        return false;
    }

    // Log tensor shapes
    Serial.printf("AudioEmbed: Input [%d, %d, %d, %d]\n",
                  _inputTensor->dims->data[0],
                  _inputTensor->dims->data[1],
                  _inputTensor->dims->data[2],
                  _inputTensor->dims->data[3]);
    Serial.printf("AudioEmbed: Output [%d, %d]\n",
                  _outputTensor->dims->data[0],
                  _outputTensor->dims->data[1]);

    return true;
}

bool AudioEmbedder::_runInference(const float* melFeatures, float* embedding) {
    // Input shape: [1, 64, 96, 1] = [batch, bins, frames, channel]
    int inputSize = AUDIO_MEL_BINS * AUDIO_MEL_FRAMES;

    // Fill input tensor
    if (_inputTensor->type == kTfLiteInt8) {
        int8_t* inputData = _inputTensor->data.int8;
        float scale = _inputTensor->params.scale;
        int32_t zeroPoint = _inputTensor->params.zero_point;

        for (int i = 0; i < inputSize; i++) {
            int32_t quantized = (int32_t)(melFeatures[i] / scale + zeroPoint);
            inputData[i] = constrain(quantized, -128, 127);
        }
    } else {
        // Float input
        float* inputData = _inputTensor->data.f;
        memcpy(inputData, melFeatures, inputSize * sizeof(float));
    }

    // Run inference
    if (_interpreter->Invoke() != kTfLiteOk) {
        Serial.println("AudioEmbed: Invoke failed");
        return false;
    }

    // Extract embedding (256-dim)
    int outputDim = _outputTensor->dims->data[1];

    if (_outputTensor->type == kTfLiteInt8) {
        int8_t* outputData = _outputTensor->data.int8;
        float scale = _outputTensor->params.scale;
        int32_t zeroPoint = _outputTensor->params.zero_point;

        for (int i = 0; i < outputDim && i < AUDIO_EMBEDDING_DIM; i++) {
            embedding[i] = (outputData[i] - zeroPoint) * scale;
        }
    } else {
        float* outputData = _outputTensor->data.f;
        for (int i = 0; i < outputDim && i < AUDIO_EMBEDDING_DIM; i++) {
            embedding[i] = outputData[i];
        }
    }

    return true;
}

//==============================================================================
// Public API
//==============================================================================

bool AudioEmbedder::embed(const int16_t* audio, size_t sampleCount, float* output) {
    if (!_loaded) return false;

    unsigned long startTime = millis();

    // Debug: Print audio input stats
    int16_t audioMin = audio[0], audioMax = audio[0];
    int64_t audioSum = 0;
    for (size_t i = 0; i < sampleCount; i++) {
        if (audio[i] < audioMin) audioMin = audio[i];
        if (audio[i] > audioMax) audioMax = audio[i];
        audioSum += abs(audio[i]);
    }
    float audioAvg = (float)audioSum / sampleCount;
    Serial.printf("AudioEmbed: Input audio: min=%d, max=%d, avg=%.1f, samples=%d\n",
                  audioMin, audioMax, audioAvg, sampleCount);

    // Step 1: Compute mel-spectrogram
    if (!_computeMelSpectrogram(audio, sampleCount, _melFeatures)) {
        Serial.println("AudioEmbed: Mel computation failed");
        return false;
    }

    unsigned long melTime = millis();
    Serial.printf("AudioEmbed: Mel-spec took %lu ms\n", melTime - startTime);

    // Step 2: Run CNN inference
    if (!_runInference(_melFeatures, output)) {
        Serial.println("AudioEmbed: Inference failed");
        return false;
    }

    unsigned long inferTime = millis();
    Serial.printf("AudioEmbed: Inference took %lu ms\n", inferTime - melTime);
    Serial.printf("AudioEmbed: Total time: %lu ms\n", inferTime - startTime);

    // Debug: Print embedding stats and first few values
    float minVal = output[0], maxVal = output[0], sum = 0;
    for (int i = 0; i < AUDIO_EMBEDDING_DIM; i++) {
        if (output[i] < minVal) minVal = output[i];
        if (output[i] > maxVal) maxVal = output[i];
        sum += output[i] * output[i];
    }
    float norm = sqrtf(sum);
    Serial.printf("AudioEmbed: Embedding norm=%.4f, min=%.4f, max=%.4f\n", norm, minVal, maxVal);
    Serial.printf("AudioEmbed: First 8 values: [%.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f]\n",
                  output[0], output[1], output[2], output[3],
                  output[4], output[5], output[6], output[7]);

    return true;
}
