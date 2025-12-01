/*******************************************************************************
 * OAISYS Badge - Microphone Stream Implementation
 ******************************************************************************/

#include "mic_stream.h"
#include "../../config.h"

#include <driver/i2s.h>

// Include EdgeNeuron first to trigger Arduino library path resolution
#include <EdgeNeuron.h>

// Microfrontend includes (from EdgeNeuron library)
#include "tensorflow/lite/experimental/microfrontend/lib/frontend.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"

// Audio constants
#define SAMPLE_RATE         16000
#define FEATURE_STRIDE_SAMPLES (SAMPLE_RATE * FEATURE_STRIDE_MS / 1000)  // 160 samples

MicStream::MicStream()
    : _audioBuffer(nullptr)
    , _audioBufferPos(0)
    , _featureFrameCount(0)
    , _i2sBuffer(nullptr)
    , _frontendState(nullptr)
    , _running(false)
    , _amplitude(0)
{
    memset(_featureBuffer, 0, sizeof(_featureBuffer));
}

MicStream::~MicStream() {
    end();
}

bool MicStream::begin() {
    if (_running) return true;

    // Allocate buffers
    _audioBuffer = (int16_t*)malloc(AUDIO_BUFFER_SIZE * sizeof(int16_t));
    _i2sBuffer = (int32_t*)malloc(512 * sizeof(int32_t));
    _frontendState = (FrontendState*)malloc(sizeof(FrontendState));

    if (!_audioBuffer || !_i2sBuffer || !_frontendState) {
        end();
        return false;
    }

    _audioBufferPos = 0;
    _featureFrameCount = 0;

    if (!_initI2S()) {
        end();
        return false;
    }

    if (!_initFrontend()) {
        end();
        return false;
    }

    _running = true;
    return true;
}

void MicStream::end() {
    if (_running) {
        i2s_driver_uninstall(I2S_NUM_0);
    }

    if (_audioBuffer) {
        free(_audioBuffer);
        _audioBuffer = nullptr;
    }
    if (_i2sBuffer) {
        free(_i2sBuffer);
        _i2sBuffer = nullptr;
    }
    if (_frontendState) {
        FrontendFreeStateContents(_frontendState);
        free(_frontendState);
        _frontendState = nullptr;
    }

    _running = false;
}

bool MicStream::_initI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pins = {
        .bck_io_num = MIC_BCK,
        .ws_io_num = MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = MIC_DIN
    };

    if (i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL) != ESP_OK) {
        return false;
    }

    if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
        i2s_driver_uninstall(I2S_NUM_0);
        return false;
    }

    // Brief mic warm-up (500ms) to let INMP441 stabilize
    // Full 2s warm-up happens after wake word detection, before recording
    Serial.println("MicStream: Quick mic warm-up (500ms)...");
    const int QUICK_WARMUP_SAMPLES = SAMPLE_RATE / 2;  // 500ms at 16kHz
    int samplesDiscarded = 0;
    size_t bytes_read;
    while (samplesDiscarded < QUICK_WARMUP_SAMPLES) {
        if (i2s_read(I2S_NUM_0, _i2sBuffer, 512 * 4, &bytes_read, 10) == ESP_OK) {
            samplesDiscarded += bytes_read / 4;
        }
    }
    Serial.println("MicStream: Warm-up done");

    return true;
}

void MicStream::_warmUpMic() {
    const int WARMUP_SAMPLES = SAMPLE_RATE * 2;  // 2 seconds at 16kHz
    int samplesDiscarded = 0;
    size_t bytes_read;

    unsigned long startTime = millis();

    while (samplesDiscarded < WARMUP_SAMPLES) {
        if (i2s_read(I2S_NUM_0, _i2sBuffer, 512 * 4, &bytes_read, 10) == ESP_OK) {
            samplesDiscarded += bytes_read / 4;
        }
    }

    Serial.printf("MicStream: Warm-up done (%d ms)\n", (int)(millis() - startTime));
}

bool MicStream::_initFrontend() {
    FrontendConfig frontend_config;
    frontend_config.window.size_ms = FEATURE_WINDOW_MS;
    frontend_config.window.step_size_ms = FEATURE_STRIDE_MS;
    frontend_config.filterbank.num_channels = FEATURE_BINS;
    frontend_config.filterbank.lower_band_limit = 125.0f;
    frontend_config.filterbank.upper_band_limit = 7500.0f;
    frontend_config.noise_reduction.smoothing_bits = 10;
    frontend_config.noise_reduction.even_smoothing = 0.025f;
    frontend_config.noise_reduction.odd_smoothing = 0.06f;
    frontend_config.noise_reduction.min_signal_remaining = 0.05f;
    frontend_config.pcan_gain_control.enable_pcan = 1;
    frontend_config.pcan_gain_control.strength = 0.95f;
    frontend_config.pcan_gain_control.offset = 80.0f;
    frontend_config.pcan_gain_control.gain_bits = 21;
    frontend_config.log_scale.enable_log = 1;
    frontend_config.log_scale.scale_shift = 6;

    return FrontendPopulateState(&frontend_config, _frontendState, SAMPLE_RATE);
}

bool MicStream::_captureAudio() {
    int space_available = AUDIO_BUFFER_SIZE - _audioBufferPos;
    if (space_available < 256) {
        return true;  // Buffer full, will process
    }

    int samples_to_read = min(256, space_available);
    size_t bytes_read;
    if (i2s_read(I2S_NUM_0, _i2sBuffer, samples_to_read * 4, &bytes_read, 10) != ESP_OK) {
        return false;
    }

    int samples = bytes_read / 4;
    int32_t max_sample = 0;

    for (int i = 0; i < samples; i++) {
        // INMP441 sends 24-bit in 32-bit frame, shift down
        int32_t sample = _i2sBuffer[i] >> 14;
        _audioBuffer[_audioBufferPos++] = constrain(sample, -32768, 32767);
        if (abs(sample) > max_sample) max_sample = abs(sample);
    }

    _amplitude = max_sample;
    return true;
}

bool MicStream::update() {
    if (!_running) return false;

    if (!_captureAudio()) {
        return false;
    }

    bool newFrame = false;

    // Process audio through frontend when we have enough samples
    while (_audioBufferPos >= FEATURE_STRIDE_SAMPLES) {
        size_t num_samples_read;
        FrontendOutput frontend_output = FrontendProcessSamples(
            _frontendState, _audioBuffer, _audioBufferPos, &num_samples_read);

        if (num_samples_read == 0) {
            break;
        }

        // Shift remaining samples to start of buffer
        if (num_samples_read < (size_t)_audioBufferPos) {
            memmove(_audioBuffer, _audioBuffer + num_samples_read,
                    (_audioBufferPos - num_samples_read) * sizeof(int16_t));
        }
        _audioBufferPos -= num_samples_read;

        // If we got features, add to feature buffer
        if (frontend_output.size == FEATURE_BINS) {
            // Shift feature frames if buffer is full
            if (_featureFrameCount >= FEATURE_FRAMES) {
                memmove(_featureBuffer, _featureBuffer + FEATURE_BINS,
                        (FEATURE_FRAMES - 1) * FEATURE_BINS * sizeof(uint16_t));
                _featureFrameCount = FEATURE_FRAMES - 1;
            }

            // Add new frame
            memcpy(_featureBuffer + _featureFrameCount * FEATURE_BINS,
                   frontend_output.values, FEATURE_BINS * sizeof(uint16_t));
            _featureFrameCount++;

            // Signal that we have a complete feature set
            if (_featureFrameCount >= FEATURE_FRAMES) {
                newFrame = true;
            }
        }
    }

    return newFrame;
}

int MicStream::getNewSamples(int16_t* buffer, size_t maxSamples) {
    // This provides raw audio for recording purposes
    // For now, return what's in buffer (caller should copy before next update)
    int count = min((size_t)_audioBufferPos, maxSamples);
    if (count > 0 && buffer) {
        memcpy(buffer, _audioBuffer, count * sizeof(int16_t));
    }
    return count;
}
