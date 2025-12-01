/*******************************************************************************
 * OAISYS Badge - Audio Recorder Implementation
 ******************************************************************************/

#include "audio_recorder.h"
#include "../../config.h"

#include <driver/i2s.h>
#include <SD.h>

AudioRecorder::AudioRecorder()
    : _buffer(nullptr)
    , _targetSamples(0)
    , _sampleCount(0)
    , _recording(false)
    , _initialized(false)
    , _i2sBuffer(nullptr)
{
}

AudioRecorder::~AudioRecorder() {
    end();
}

bool AudioRecorder::begin() {
    if (_initialized) return true;

    // Allocate recording buffer in PSRAM
    _buffer = (int16_t*)ps_malloc(RECORDING_BUFFER_SIZE * sizeof(int16_t));
    if (!_buffer) {
        Serial.println("AudioRecorder: Buffer alloc failed");
        return false;
    }

    // Allocate I2S read buffer
    _i2sBuffer = (int32_t*)malloc(512 * sizeof(int32_t));
    if (!_i2sBuffer) {
        free(_buffer);
        _buffer = nullptr;
        return false;
    }

    // Note: I2S is initialized by MicStream, we share the same I2S port
    // This recorder should be used AFTER MicStream.end() is called

    _initialized = true;
    _sampleCount = 0;
    return true;
}

void AudioRecorder::end() {
    stopRecording();

    if (_buffer) {
        free(_buffer);
        _buffer = nullptr;
    }
    if (_i2sBuffer) {
        free(_i2sBuffer);
        _i2sBuffer = nullptr;
    }

    _initialized = false;
}

void AudioRecorder::startRecording(float durationSec) {
    if (!_initialized || _recording) return;

    // Clamp duration
    if (durationSec > RECORDING_MAX_SECONDS) {
        durationSec = RECORDING_MAX_SECONDS;
    }

    _targetSamples = (size_t)(durationSec * RECORDING_SAMPLE_RATE);
    _sampleCount = 0;
    _recording = true;

    // Initialize I2S for recording
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = RECORDING_SAMPLE_RATE,
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
        _recording = false;
        return;
    }
    i2s_set_pin(I2S_NUM_0, &pins);

    // INMP441 microphone needs warm-up time after I2S init
    // Discard first 2 seconds of audio (junk/noise from mic stabilization)
    Serial.println("AudioRecorder: Mic warm-up (discarding 2s)...");
    _warmUpMic();

    Serial.printf("AudioRecorder: Started %.1fs recording\n", durationSec);
}

void AudioRecorder::stopRecording() {
    if (_recording) {
        i2s_driver_uninstall(I2S_NUM_0);
        _recording = false;
        Serial.printf("AudioRecorder: Stopped, %d samples (%.2fs)\n",
                      _sampleCount, getDurationSec());
    }
}

bool AudioRecorder::_captureChunk() {
    size_t remaining = _targetSamples - _sampleCount;
    if (remaining == 0) return true;  // Done

    int samples_to_read = min((size_t)256, remaining);
    size_t bytes_read;

    if (i2s_read(I2S_NUM_0, _i2sBuffer, samples_to_read * 4, &bytes_read, 10) != ESP_OK) {
        return false;
    }

    int samples = bytes_read / 4;

    for (int i = 0; i < samples && _sampleCount < _targetSamples; i++) {
        // INMP441 sends 24-bit in 32-bit frame
        int32_t sample = _i2sBuffer[i] >> 14;
        _buffer[_sampleCount++] = constrain(sample, -32768, 32767);
    }

    return true;
}

void AudioRecorder::_warmUpMic() {
    // INMP441 needs ~100-200ms minimum to stabilize, but for best results
    // we discard 2 full seconds of audio data
    // At 16kHz, that's 32000 samples = 128KB of data to discard

    const int WARMUP_SAMPLES = RECORDING_SAMPLE_RATE * 2;  // 2 seconds
    int samplesDiscarded = 0;
    size_t bytes_read;

    unsigned long startTime = millis();

    while (samplesDiscarded < WARMUP_SAMPLES) {
        // Read and discard
        if (i2s_read(I2S_NUM_0, _i2sBuffer, 512 * 4, &bytes_read, 10) == ESP_OK) {
            samplesDiscarded += bytes_read / 4;
        }
    }

    Serial.printf("AudioRecorder: Warm-up done (%d ms, %d samples discarded)\n",
                  (int)(millis() - startTime), samplesDiscarded);
}

bool AudioRecorder::update() {
    if (!_recording) return false;

    _captureChunk();

    // Check if recording complete
    if (_sampleCount >= _targetSamples) {
        stopRecording();
        return true;  // Recording complete
    }

    return false;  // Still recording
}

float AudioRecorder::getProgress() const {
    if (_targetSamples == 0) return 0.0f;
    return (float)_sampleCount / _targetSamples;
}

bool AudioRecorder::saveToSD(const char* filepath) {
    if (!_buffer || _sampleCount == 0) {
        Serial.println("AudioRecorder: No recording to save");
        return false;
    }

    File file = SD.open(filepath, FILE_WRITE);
    if (!file) {
        Serial.printf("AudioRecorder: Failed to create %s\n", filepath);
        return false;
    }

    // Write WAV header
    uint32_t dataSize = _sampleCount * sizeof(int16_t);
    uint32_t fileSize = 36 + dataSize;

    // RIFF header
    file.write((const uint8_t*)"RIFF", 4);
    file.write((uint8_t*)&fileSize, 4);
    file.write((const uint8_t*)"WAVE", 4);

    // fmt chunk
    file.write((const uint8_t*)"fmt ", 4);
    uint32_t fmtSize = 16;
    file.write((uint8_t*)&fmtSize, 4);
    uint16_t audioFormat = 1;  // PCM
    file.write((uint8_t*)&audioFormat, 2);
    uint16_t numChannels = 1;  // Mono
    file.write((uint8_t*)&numChannels, 2);
    uint32_t sampleRate = RECORDING_SAMPLE_RATE;
    file.write((uint8_t*)&sampleRate, 4);
    uint32_t byteRate = sampleRate * numChannels * sizeof(int16_t);
    file.write((uint8_t*)&byteRate, 4);
    uint16_t blockAlign = numChannels * sizeof(int16_t);
    file.write((uint8_t*)&blockAlign, 2);
    uint16_t bitsPerSample = 16;
    file.write((uint8_t*)&bitsPerSample, 2);

    // data chunk
    file.write((const uint8_t*)"data", 4);
    file.write((uint8_t*)&dataSize, 4);
    file.write((uint8_t*)_buffer, dataSize);

    file.close();

    Serial.printf("AudioRecorder: Saved %s (%d samples, %.2fs)\n",
                  filepath, _sampleCount, getDurationSec());
    return true;
}
