/*******************************************************************************
 * OAISYS Badge - Video Player Module
 * MJPEG playback with gyro rotation and text overlay support
 ******************************************************************************/

#pragma once

#include <Arduino.h>
#include <Stream.h>
#include <JPEGDEC.h>

// Forward declare to avoid circular includes
class Arduino_GFX;

//==============================================================================
// Memory Stream - Wraps PSRAM buffer as a Stream for MJPEG decoder
//==============================================================================
class MemoryStream : public Stream {
public:
    MemoryStream(uint8_t* buffer, size_t size);

    int available() override;
    int read() override;
    int peek() override;
    size_t readBytes(char* buffer, size_t length) override;
    void reset();

    // Required Stream methods (write not supported)
    size_t write(uint8_t) override { return 0; }
    void flush() override {}

private:
    uint8_t* _buffer;
    size_t _size;
    size_t _position;
};

//==============================================================================
// Video Player - Handles MJPEG playback and display
//==============================================================================
class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    // Lifecycle
    bool begin(const char* videoPath = nullptr);  // nullptr = use FFat /output.mjpeg
    void end();

    // Playback control
    void play();           // Decode and display one frame
    void pause();
    void resume();
    void togglePause();
    bool isPaused() const { return _paused; }

    // Power control
    void powerOff();       // Blank screen + backlight off
    void powerOn();        // Restore display + backlight
    void togglePower();
    bool isPowered() const { return _powered; }

    // Orientation
    void setRotation(uint8_t rotation);
    uint8_t getRotation() const { return _rotation; }

    // Text overlay (pauses video, shows text box)
    void showText(const char* text);
    void appendText(const char* chunk);  // For streaming LLM output
    void clearText();
    void hideText();                      // Resume video playback
    bool isTextVisible() const { return _textVisible; }

    // Get display for direct access if needed
    Arduino_GFX* getDisplay() { return _display; }

    // Fill screen with solid color (RGB565)
    void fillScreen(uint16_t color);

    // Refresh display after SPI bus conflicts (e.g., after SD card access)
    void refreshDisplay();

private:
    // Display
    Arduino_GFX* _display;
    uint8_t _rotation;

    // Video buffer (PSRAM)
    uint8_t* _videoBuf;
    size_t _videoSize;
    MemoryStream* _stream;

    // Decoder
    uint8_t* _decodeBuf;
    void* _decoder;  // MjpegClass* (forward declared to keep header clean)

    // State
    bool _paused;
    bool _powered;
    bool _initialized;

    // Text overlay
    bool _textVisible;
    String _currentText;

    // JPEG draw callback (needs static for C callback)
    static VideoPlayer* _instance;
    static int _drawCallback(JPEGDRAW* pDraw);

    // Internal helpers
    void _drawTextBox();
    void _initDisplay();
    bool _loadVideo(const char* path);
};
