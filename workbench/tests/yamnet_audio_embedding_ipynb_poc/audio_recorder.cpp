// audio_recorder.cpp - I2S audio recording implementation

#include "audio_recorder.h"

AudioRecorder::AudioRecorder() : initialized_(false), sample_rate_(16000) {
}

AudioRecorder::~AudioRecorder() {
    end();
}

bool AudioRecorder::begin(int sample_rate) {
    sample_rate_ = sample_rate;

    // I2S configuration for INMP441 microphones
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = sample_rate_,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  // Stereo
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = MIC_BCK_PIN,
        .ws_io_num = MIC_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = MIC_DIN_PIN
    };

    // Install and configure I2S driver
    if (i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL) != ESP_OK) {
        return false;
    }

    if (i2s_set_pin(I2S_PORT, &pin_config) != ESP_OK) {
        i2s_driver_uninstall(I2S_PORT);
        return false;
    }

    initialized_ = true;
    return true;
}

bool AudioRecorder::record(int16_t* buffer, int num_samples) {
    if (!initialized_) {
        return false;
    }

    int samples_written = 0;

    // Read from I2S and downmix stereo to mono
    while (samples_written < num_samples) {
        size_t bytes_read = 0;

        if (i2s_read(I2S_PORT, i2s_buffer_,
                     I2S_BUFFER_SIZE * sizeof(int32_t),
                     &bytes_read, portMAX_DELAY) != ESP_OK) {
            return false;
        }

        int samples_read = bytes_read / sizeof(int32_t);

        // Convert and downmix: stereo (L,R,L,R...) -> mono
        for (int i = 0; i < samples_read && samples_written < num_samples; i += 2) {
            // Extract left and right channels (32-bit to 16-bit)
            int16_t left = (int16_t)(i2s_buffer_[i] >> 16);
            int16_t right = (int16_t)(i2s_buffer_[i + 1] >> 16);

            // Simple average downmix to mono
            buffer[samples_written++] = ((int32_t)left + (int32_t)right) / 2;
        }
    }

    return true;
}

void AudioRecorder::end() {
    if (initialized_) {
        i2s_driver_uninstall(I2S_PORT);
        initialized_ = false;
    }
}
