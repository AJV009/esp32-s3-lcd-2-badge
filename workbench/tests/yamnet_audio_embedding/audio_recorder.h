// audio_recorder.h - I2S audio recording from INMP441 microphones
// Records mono audio (downmixed from stereo) at 16kHz

#ifndef AUDIO_RECORDER_H
#define AUDIO_RECORDER_H

#include <Arduino.h>
#include <driver/i2s.h>

// Microphone pins (I2S0) - same as your working setup
#define MIC_BCK_PIN    2
#define MIC_WS_PIN     4
#define MIC_DIN_PIN    18

// I2S configuration
#define I2S_PORT       I2S_NUM_0
#define I2S_BUFFER_SIZE 2048

class AudioRecorder {
public:
    AudioRecorder();
    ~AudioRecorder();

    // Initialize I2S microphone
    bool begin(int sample_rate);

    // Record audio samples (blocking)
    bool record(int16_t* buffer, int num_samples);

    // Stop and cleanup
    void end();

private:
    int sample_rate_;
    int32_t i2s_buffer_[I2S_BUFFER_SIZE];
    bool initialized_;
};

#endif // AUDIO_RECORDER_H
