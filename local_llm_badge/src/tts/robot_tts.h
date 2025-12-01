/*******************************************************************************
 * OAISYS Badge - Robotic TTS Module
 *
 * Uses ESP32-SAM library for 1980s-style robotic speech synthesis.
 * Outputs audio via I2S to MAX98357A speaker.
 *
 * NOTE: SAM library header has non-inline function definitions that cause
 * multiple definition errors if included in multiple translation units.
 * We use forward declarations here and include sam_arduino.h only in .cpp
 ******************************************************************************/

#pragma once

#include <Arduino.h>

// Forward declarations (SAM library included only in .cpp to avoid linker errors)
class SAM;
class SAMI2SOutput;

//==============================================================================
// Robot TTS Wrapper
//==============================================================================
class RobotTTS {
public:
    // Voice presets for different characters
    enum Voice {
        ROBOT,          // Ultra-robotic (custom settings)
        LITTLE_ROBOT,   // SAM's LittleRobot preset
        ALIEN,          // SAM's ExtraTerrestrial preset
        ELF,            // SAM's Elf preset
        SAM_DEFAULT     // SAM's default voice
    };

    RobotTTS();
    ~RobotTTS();

    // Initialize TTS with I2S output
    // Returns false if initialization fails
    bool begin();
    void end();

    // Check if initialized
    bool isReady() const { return _ready; }

    // Speak text (blocking)
    // Returns true if speech completed successfully
    bool speak(const char* text);

    // Speak phonemes directly (for precise control)
    bool speakPhonemes(const char* phonemes);

    // Voice configuration
    void setVoice(Voice voice);
    void setSpeed(uint8_t speed);      // 0-255 (default: 72)
    void setPitch(uint8_t pitch);      // 0-255 (default: 64)
    void setMouth(uint8_t mouth);      // 0-255 (default: 128)
    void setThroat(uint8_t throat);    // 0-255 (default: 128)

    // Volume control (0-100)
    void setVolume(uint8_t percent);

    // Sing mode for musical speech
    void setSingMode(bool enable);

private:
    SAM* _sam;
    SAMI2SOutput* _output;
    bool _ready;
};
