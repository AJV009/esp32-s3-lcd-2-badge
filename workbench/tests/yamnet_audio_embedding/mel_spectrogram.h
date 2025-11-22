// mel_spectrogram.h - Mel-spectrogram generation for YAMNet
// Generates 64 mel bins × 96 frames from 16kHz audio
// Uses ESP-DSP for accelerated FFT

#ifndef MEL_SPECTROGRAM_H
#define MEL_SPECTROGRAM_H

#include <Arduino.h>

// YAMNet input requirements
#define MEL_BINS 64          // Number of mel filterbanks
#define MEL_FRAMES 96        // Number of time frames
#define FFT_SIZE 512         // FFT window size (25ms @ 16kHz)
#define HOP_LENGTH 160       // Hop size (10ms @ 16kHz)
#define SAMPLE_RATE 16000    // Audio sample rate

class MelSpectrogram {
public:
    MelSpectrogram();
    ~MelSpectrogram();

    // Initialize with sample rate
    bool begin(int sample_rate);

    // Compute mel-spectrogram from audio samples
    // Output: mel_features[MEL_BINS * MEL_FRAMES] in row-major order
    bool compute(int16_t* audio, int num_samples, float* mel_features);

    // Cleanup
    void end();

private:
    // Initialize mel filterbank
    void initMelFilterbank();

    // Apply mel filterbank to power spectrum
    void applyMelFilterbank(float* power_spectrum, float* mel_output);

    // Compute single FFT frame
    void computeFFTFrame(int16_t* audio, int start_idx, float* power_spectrum);

    // Convert frequency to mel scale
    float hzToMel(float hz);
    float melToHz(float mel);

    int sample_rate_;
    bool initialized_;

    // Mel filterbank (MEL_BINS × (FFT_SIZE/2 + 1))
    float* mel_filterbank_;

    // Working buffers
    float* fft_input_;
    float* fft_output_;
    float* window_;
};

#endif // MEL_SPECTROGRAM_H
