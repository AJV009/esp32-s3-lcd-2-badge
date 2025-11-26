// mel_spectrogram.cpp - Mel-spectrogram implementation with ESP-DSP

#include "mel_spectrogram.h"
#include <esp_dsp.h>
#include <math.h>

MelSpectrogram::MelSpectrogram()
    : initialized_(false), sample_rate_(16000),
      mel_filterbank_(nullptr), fft_input_(nullptr),
      fft_output_(nullptr), window_(nullptr) {
}

MelSpectrogram::~MelSpectrogram() {
    end();
}

bool MelSpectrogram::begin(int sample_rate) {
    sample_rate_ = sample_rate;

    // Allocate mel filterbank (64 bins × 257 frequency bins)
    int num_freq_bins = FFT_SIZE / 2 + 1;
    mel_filterbank_ = (float*)ps_malloc(MEL_BINS * num_freq_bins * sizeof(float));
    if (!mel_filterbank_) return false;

    // Allocate working buffers
    fft_input_ = (float*)ps_malloc(FFT_SIZE * sizeof(float));
    fft_output_ = (float*)ps_malloc(FFT_SIZE * 2 * sizeof(float));  // Complex output
    window_ = (float*)ps_malloc(FFT_SIZE * sizeof(float));

    if (!fft_input_ || !fft_output_ || !window_) {
        end();
        return false;
    }

    // Initialize ESP-DSP FFT
    esp_err_t ret = dsps_fft2r_init_fc32(NULL, FFT_SIZE);
    if (ret != ESP_OK) {
        end();
        return false;
    }

    // Create Hann window
    for (int i = 0; i < FFT_SIZE; i++) {
        window_[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_SIZE - 1)));
    }

    // Initialize mel filterbank
    initMelFilterbank();

    initialized_ = true;
    return true;
}

void MelSpectrogram::initMelFilterbank() {
    // YAMNet frequency range: 125-7500 Hz
    float min_freq = 125.0f;
    float max_freq = 7500.0f;

    float min_mel = hzToMel(min_freq);
    float max_mel = hzToMel(max_freq);

    int num_freq_bins = FFT_SIZE / 2 + 1;

    // Create mel filterbank centers
    float* mel_centers = (float*)malloc((MEL_BINS + 2) * sizeof(float));
    for (int i = 0; i < MEL_BINS + 2; i++) {
        float mel = min_mel + (max_mel - min_mel) * i / (MEL_BINS + 1);
        mel_centers[i] = melToHz(mel);
    }

    // Build triangular filters
    float freq_resolution = (float)sample_rate_ / FFT_SIZE;

    for (int m = 0; m < MEL_BINS; m++) {
        float left = mel_centers[m];
        float center = mel_centers[m + 1];
        float right = mel_centers[m + 2];

        for (int k = 0; k < num_freq_bins; k++) {
            float freq = k * freq_resolution;
            float weight = 0.0f;

            if (freq >= left && freq <= center) {
                weight = (freq - left) / (center - left);
            } else if (freq > center && freq <= right) {
                weight = (right - freq) / (right - center);
            }

            mel_filterbank_[m * num_freq_bins + k] = weight;
        }
    }

    free(mel_centers);
}

bool MelSpectrogram::compute(int16_t* audio, int num_samples, float* mel_features) {
    if (!initialized_) return false;

    // Generate MEL_FRAMES (96) frames with HOP_LENGTH (160 samples) hop
    for (int frame = 0; frame < MEL_FRAMES; frame++) {
        int start_idx = frame * HOP_LENGTH;

        // Check bounds
        if (start_idx + FFT_SIZE > num_samples) {
            // Zero-pad if needed
            for (int i = 0; i < MEL_BINS; i++) {
                mel_features[frame * MEL_BINS + i] = -80.0f;  // Log(0) approximation
            }
            continue;
        }

        // Compute power spectrum for this frame
        float power_spectrum[FFT_SIZE / 2 + 1];
        computeFFTFrame(audio, start_idx, power_spectrum);

        // Apply mel filterbank
        float mel_output[MEL_BINS];
        applyMelFilterbank(power_spectrum, mel_output);

        // Store in column-major format (frames × bins)
        for (int i = 0; i < MEL_BINS; i++) {
            mel_features[frame * MEL_BINS + i] = mel_output[i];
        }
    }

    return true;
}

void MelSpectrogram::computeFFTFrame(int16_t* audio, int start_idx, float* power_spectrum) {
    // Apply window and convert to float
    for (int i = 0; i < FFT_SIZE; i++) {
        fft_input_[i] = (float)audio[start_idx + i] * window_[i] / 32768.0f;
    }

    // Prepare complex input for FFT (real part only)
    for (int i = 0; i < FFT_SIZE; i++) {
        fft_output_[i * 2] = fft_input_[i];      // Real
        fft_output_[i * 2 + 1] = 0.0f;           // Imaginary
    }

    // Compute FFT using ESP-DSP
    dsps_fft2r_fc32(fft_output_, FFT_SIZE);
    dsps_bit_rev_fc32(fft_output_, FFT_SIZE);

    // Compute power spectrum: |X[k]|^2
    for (int k = 0; k < FFT_SIZE / 2 + 1; k++) {
        float real = fft_output_[k * 2];
        float imag = fft_output_[k * 2 + 1];
        power_spectrum[k] = real * real + imag * imag;
    }
}

void MelSpectrogram::applyMelFilterbank(float* power_spectrum, float* mel_output) {
    int num_freq_bins = FFT_SIZE / 2 + 1;

    for (int m = 0; m < MEL_BINS; m++) {
        float sum = 0.0f;

        for (int k = 0; k < num_freq_bins; k++) {
            sum += mel_filterbank_[m * num_freq_bins + k] * power_spectrum[k];
        }

        // Apply log transform (with small epsilon to avoid log(0))
        mel_output[m] = log10f(sum + 1e-10f);
    }
}

float MelSpectrogram::hzToMel(float hz) {
    return 2595.0f * log10f(1.0f + hz / 700.0f);
}

float MelSpectrogram::melToHz(float mel) {
    return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

void MelSpectrogram::end() {
    if (mel_filterbank_) free(mel_filterbank_);
    if (fft_input_) free(fft_input_);
    if (fft_output_) free(fft_output_);
    if (window_) free(window_);

    mel_filterbank_ = nullptr;
    fft_input_ = nullptr;
    fft_output_ = nullptr;
    window_ = nullptr;
    initialized_ = false;
}
