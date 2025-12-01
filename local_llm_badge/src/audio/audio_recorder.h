/*******************************************************************************
 * OAISYS Badge - Audio Recorder Module
 * Fixed duration recording for speech input after wake word
 ******************************************************************************/

#pragma once

#include <Arduino.h>

//==============================================================================
// Recording constants
//==============================================================================
#define RECORDING_SAMPLE_RATE   16000
#define RECORDING_MAX_SECONDS   5
#define RECORDING_BUFFER_SIZE   (RECORDING_SAMPLE_RATE * RECORDING_MAX_SECONDS)

//==============================================================================
// AudioRecorder - Handles timed audio recording to PSRAM buffer
//==============================================================================
class AudioRecorder {
public:
    AudioRecorder();
    ~AudioRecorder();

    // Lifecycle
    bool begin();  // Allocates PSRAM buffer
    void end();    // Frees buffer

    // Recording control
    void startRecording(float durationSec = 3.0f);
    void stopRecording();
    bool isRecording() const { return _recording; }

    // Call in loop while recording - captures audio
    // Returns true when recording completes
    bool update();

    // Get recorded audio
    const int16_t* getBuffer() const { return _buffer; }
    size_t getSampleCount() const { return _sampleCount; }
    float getDurationSec() const { return _sampleCount / (float)RECORDING_SAMPLE_RATE; }

    // Get recording progress (0.0 - 1.0)
    float getProgress() const;

    // Check if buffer is valid
    bool hasRecording() const { return _sampleCount > 0; }

    // Save recording to SD card as WAV file (for debugging)
    // Returns true if saved successfully
    bool saveToSD(const char* filepath);

private:
    int16_t* _buffer;
    size_t _targetSamples;
    size_t _sampleCount;
    bool _recording;
    bool _initialized;

    // I2S buffer for capture
    int32_t* _i2sBuffer;

    // Internal helpers
    bool _captureChunk();
    void _warmUpMic();  // Discard first 2s of audio (mic stabilization)
};
