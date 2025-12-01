/*******************************************************************************
 * OAISYS Badge - Audio Embedding Module
 *
 * Generates 256-dimensional embeddings from recorded audio using:
 * 1. Mel-spectrogram generation (64 bins x 96 frames)
 * 2. Small CNN TFLite inference (~100-300KB model)
 *
 * Model architecture (trained with contrastive learning):
 *   Input: (64, 96, 1) mel-spectrogram
 *   Conv2D 32 -> MaxPool -> Conv2D 64 -> MaxPool ->
 *   Conv2D 128 -> MaxPool -> Conv2D 128 -> GlobalAvgPool -> Dense 256
 *   Output: 256-dim L2-normalized embedding
 ******************************************************************************/

#pragma once

#include <Arduino.h>

// Forward declarations
namespace tflite {
    class MicroInterpreter;
}
struct TfLiteTensor;

//==============================================================================
// Audio Encoder Specifications
//==============================================================================

// Mel-spectrogram parameters (must match training)
#define AUDIO_MEL_BINS 64
#define AUDIO_MEL_FRAMES 96
#define AUDIO_FFT_SIZE 512
#define AUDIO_HOP_LENGTH 160    // 10ms @ 16kHz
#define AUDIO_SAMPLE_RATE 16000

// Embedding dimension
#define AUDIO_EMBEDDING_DIM 256

// TensorFlow Lite memory (small CNN needs ~100-200KB)
#define AUDIO_TENSOR_ARENA_SIZE (300 * 1024)  // 300KB arena

//==============================================================================
// AudioEmbedder - Audio to 256-dim embedding
//==============================================================================
class AudioEmbedder {
public:
    AudioEmbedder();
    ~AudioEmbedder();

    // Lifecycle - uses provided memory pool
    bool begin(uint8_t* pool, size_t poolSize, const char* modelPath);
    void end();

    // Process recorded audio -> 256-dim embedding
    // audio: int16_t samples at 16kHz
    // sampleCount: number of samples (should be ~3 seconds = 48000)
    // output: float[256] embedding vector
    bool embed(const int16_t* audio, size_t sampleCount, float* output);

    // Check if loaded
    bool isLoaded() const { return _loaded; }

    // Memory requirement for pool
    static size_t requiredMemory() { return AUDIO_TENSOR_ARENA_SIZE + 100000; }

private:
    // Mel-spectrogram generation
    bool _initMelSpectrogram();
    void _endMelSpectrogram();
    bool _computeMelSpectrogram(const int16_t* audio, size_t samples, float* melOutput);
    void _computeFFTFrame(const int16_t* audio, int startIdx, float* powerSpectrum);
    void _applyMelFilterbank(float* powerSpectrum, float* melOutput);
    float _hzToMel(float hz);
    float _melToHz(float mel);

    // TFLite inference
    bool _loadModel(const char* path);
    bool _initInterpreter();
    bool _runInference(const float* melFeatures, float* embedding);

    // Mel-spectrogram state
    float* _melFilterbank;       // MEL_BINS × (FFT_SIZE/2 + 1)
    float* _fftInput;            // FFT_SIZE
    float* _fftOutput;           // FFT_SIZE × 2 (complex)
    float* _window;              // FFT_SIZE (Hann window)
    float* _melFeatures;         // MEL_BINS × MEL_FRAMES
    bool _melInitialized;

    // Model data
    uint8_t* _modelData;
    uint32_t _modelSize;

    // TFLite components
    uint8_t* _tensorArena;
    tflite::MicroInterpreter* _interpreter;
    TfLiteTensor* _inputTensor;
    TfLiteTensor* _outputTensor;

    // State
    bool _loaded;
};
