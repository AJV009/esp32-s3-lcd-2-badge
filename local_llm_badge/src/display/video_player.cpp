/*******************************************************************************
 * OAISYS Badge - Video Player Implementation
 ******************************************************************************/

#include "video_player.h"
#include "../../config.h"

#include <SD.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>

// Include MjpegClass inline to avoid separate header management
#define READ_BUFFER_SIZE 1024

class MjpegClass {
public:
    bool setup(Stream *input, uint8_t *mjpeg_buf, JPEG_DRAW_CALLBACK *pfnDraw,
               bool useBigEndian, int x, int y, int widthLimit, int heightLimit) {
        _input = input;
        _mjpeg_buf = mjpeg_buf;
        _pfnDraw = pfnDraw;
        _useBigEndian = useBigEndian;
        _x = x;
        _y = y;
        _widthLimit = widthLimit;
        _heightLimit = heightLimit;
        _inputindex = 0;

        if (!_read_buf) {
            _read_buf = (uint8_t *)malloc(READ_BUFFER_SIZE);
        }
        return _read_buf != nullptr;
    }

    bool readMjpegBuf() {
        if (_inputindex == 0) {
            _buf_read = _input->readBytes(_read_buf, READ_BUFFER_SIZE);
            _inputindex += _buf_read;
        }
        _mjpeg_buf_offset = 0;
        int i = 0;
        bool found_FFD8 = false;

        while ((_buf_read > 0) && (!found_FFD8)) {
            i = 0;
            while ((i < _buf_read) && (!found_FFD8)) {
                if ((_read_buf[i] == 0xFF) && (_read_buf[i + 1] == 0xD8)) {
                    found_FFD8 = true;
                }
                ++i;
            }
            if (found_FFD8) {
                --i;
            } else {
                _buf_read = _input->readBytes(_read_buf, READ_BUFFER_SIZE);
            }
        }

        uint8_t *_p = _read_buf + i;
        _buf_read -= i;
        bool found_FFD9 = false;

        if (_buf_read > 0) {
            i = 3;
            while ((_buf_read > 0) && (!found_FFD9)) {
                if ((_mjpeg_buf_offset > 0) && (_mjpeg_buf[_mjpeg_buf_offset - 1] == 0xFF) && (_p[0] == 0xD9)) {
                    found_FFD9 = true;
                } else {
                    while ((i < _buf_read) && (!found_FFD9)) {
                        if ((_p[i] == 0xFF) && (_p[i + 1] == 0xD9)) {
                            found_FFD9 = true;
                            ++i;
                        }
                        ++i;
                    }
                }

                memcpy(_mjpeg_buf + _mjpeg_buf_offset, _p, i);
                _mjpeg_buf_offset += i;
                size_t o = _buf_read - i;

                if (o > 0) {
                    memcpy(_read_buf, _p + i, o);
                    _buf_read = _input->readBytes(_read_buf + o, READ_BUFFER_SIZE - o);
                    _p = _read_buf;
                    _inputindex += _buf_read;
                    _buf_read += o;
                } else {
                    _buf_read = _input->readBytes(_read_buf, READ_BUFFER_SIZE);
                    _p = _read_buf;
                    _inputindex += _buf_read;
                }
                i = 0;
            }
            return found_FFD9;
        }
        return false;
    }

    bool drawJpg() {
        _remain = _mjpeg_buf_offset;

        if (_jpeg.openRAM(_mjpeg_buf, _remain, _pfnDraw) != 1) return false;

        if (_scale == -1) {
            int iMaxMCUs;
            int w = _jpeg.getWidth();
            int h = _jpeg.getHeight();
            float ratio = (float)h / _heightLimit;

            if (ratio <= 1) {
                _scale = 0;
                iMaxMCUs = _widthLimit / 16;
            } else if (ratio <= 2) {
                _scale = JPEG_SCALE_HALF;
                iMaxMCUs = _widthLimit / 8;
                w /= 2;
                h /= 2;
            } else if (ratio <= 4) {
                _scale = JPEG_SCALE_QUARTER;
                iMaxMCUs = _widthLimit / 4;
                w /= 4;
                h /= 4;
            } else {
                _scale = JPEG_SCALE_EIGHTH;
                iMaxMCUs = _widthLimit / 2;
                w /= 8;
                h /= 8;
            }

            _jpeg.setMaxOutputSize(iMaxMCUs);
            _x = (w > _widthLimit) ? 0 : ((_widthLimit - w) / 2);
            _y = (_heightLimit - h) / 2;
        }

        if (_useBigEndian) {
            _jpeg.setPixelType(RGB565_BIG_ENDIAN);
        }

        if (_jpeg.decode(_x, _y, _scale) != 1) {
            _jpeg.close();
            return false;
        }

        _jpeg.close();
        return true;
    }

    void reset() {
        _inputindex = 0;
        _buf_read = 0;
        _mjpeg_buf_offset = 0;
    }

private:
    Stream *_input;
    uint8_t *_mjpeg_buf;
    JPEG_DRAW_CALLBACK *_pfnDraw;
    bool _useBigEndian;
    int _x, _y, _widthLimit, _heightLimit;
    uint8_t *_read_buf = nullptr;
    int32_t _mjpeg_buf_offset = 0;
    JPEGDEC _jpeg;
    int _scale = -1;
    int32_t _inputindex = 0;
    int32_t _buf_read;
    int32_t _remain = 0;
};

//==============================================================================
// MemoryStream Implementation
//==============================================================================

MemoryStream::MemoryStream(uint8_t* buffer, size_t size)
    : _buffer(buffer), _size(size), _position(0) {}

int MemoryStream::available() {
    return _size - _position;
}

int MemoryStream::read() {
    return (_position < _size) ? _buffer[_position++] : -1;
}

int MemoryStream::peek() {
    return (_position < _size) ? _buffer[_position] : -1;
}

size_t MemoryStream::readBytes(char* buffer, size_t length) {
    size_t n = min(length, _size - _position);
    memcpy(buffer, _buffer + _position, n);
    _position += n;
    return n;
}

void MemoryStream::reset() {
    _position = 0;
}

//==============================================================================
// VideoPlayer Implementation
//==============================================================================

VideoPlayer* VideoPlayer::_instance = nullptr;

VideoPlayer::VideoPlayer()
    : _display(nullptr)
    , _rotation(1)
    , _videoBuf(nullptr)
    , _videoSize(0)
    , _stream(nullptr)
    , _decodeBuf(nullptr)
    , _decoder(nullptr)
    , _paused(false)
    , _powered(true)
    , _initialized(false)
    , _textVisible(false)
{
    _instance = this;
}

VideoPlayer::~VideoPlayer() {
    end();
}

int VideoPlayer::_drawCallback(JPEGDRAW* pDraw) {
    if (_instance && _instance->_display) {
        _instance->_display->draw16bitBeRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
    }
    return 1;
}

void VideoPlayer::_initDisplay() {
    // Use FSPI bus explicitly (same as factory demo) to avoid conflicts with SD card
    Arduino_DataBus* bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, LCD_MISO, FSPI, true);
    _display = new Arduino_ST7789(bus, -1, _rotation, true, LCD_WIDTH, LCD_HEIGHT);

    if (!_display->begin()) {
        return;
    }

    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);
    _display->fillScreen(BLACK);
}

bool VideoPlayer::_loadVideo(const char* path) {
    File f;

    // Initialize SPI bus (FSPI) and SD card - same bus as display
    static SPIClass sdSpi(FSPI);
    sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, sdSpi)) {
        Serial.println("SD card init failed!");
        return false;
    }

    // Use provided path or default VIDEO_PATH
    const char* videoFile = path ? path : VIDEO_PATH;
    f = SD.open(videoFile, FILE_READ);

    if (!f) {
        return false;
    }

    _videoSize = f.size();
    _videoBuf = (uint8_t*)ps_malloc(_videoSize);
    if (!_videoBuf) {
        f.close();
        return false;
    }

    if (f.read(_videoBuf, _videoSize) != _videoSize) {
        free(_videoBuf);
        _videoBuf = nullptr;
        f.close();
        return false;
    }
    f.close();

    // Create stream and decoder
    _stream = new MemoryStream(_videoBuf, _videoSize);
    _decodeBuf = (uint8_t*)malloc(LCD_WIDTH * LCD_HEIGHT / 2);
    if (!_decodeBuf) {
        return false;
    }

    _decoder = new MjpegClass();
    MjpegClass* mjpeg = (MjpegClass*)_decoder;

    // Note: With rotation=1, effective display is 320x240 (landscape)
    // MjpegClass uses these for scaling and centering calculations
    return mjpeg->setup(_stream, _decodeBuf, _drawCallback, true, 0, 0, LCD_HEIGHT, LCD_WIDTH);
}

bool VideoPlayer::begin(const char* videoPath) {
    if (_initialized) return true;

    _initDisplay();
    if (!_display) return false;

    if (!_loadVideo(videoPath)) {
        return false;
    }

    _initialized = true;
    return true;
}

void VideoPlayer::end() {
    if (_decoder) {
        delete (MjpegClass*)_decoder;
        _decoder = nullptr;
    }
    if (_stream) {
        delete _stream;
        _stream = nullptr;
    }
    if (_decodeBuf) {
        free(_decodeBuf);
        _decodeBuf = nullptr;
    }
    if (_videoBuf) {
        free(_videoBuf);
        _videoBuf = nullptr;
    }
    _initialized = false;
}

void VideoPlayer::unloadVideo() {
    // Free video buffer and decoder to make room for LLM (~1.2MB)
    // Display remains initialized for text overlay
    size_t freedSize = _videoSize;

    if (_decoder) {
        delete (MjpegClass*)_decoder;
        _decoder = nullptr;
    }
    if (_stream) {
        delete _stream;
        _stream = nullptr;
    }
    if (_decodeBuf) {
        free(_decodeBuf);
        _decodeBuf = nullptr;
    }
    if (_videoBuf) {
        free(_videoBuf);
        _videoBuf = nullptr;
        _videoSize = 0;
    }
    Serial.printf("VideoPlayer: Unloaded video (~%.1fKB freed)\n", freedSize / 1024.0f);
}

bool VideoPlayer::reloadVideo() {
    // Reload video from SD card
    if (_videoBuf) {
        return true;  // Already loaded
    }

    if (!_loadVideo(VIDEO_PATH)) {
        Serial.println("VideoPlayer: Failed to reload video");
        return false;
    }

    Serial.printf("VideoPlayer: Reloaded video (%.1fKB)\n", _videoSize / 1024.0f);
    return true;
}

void VideoPlayer::play() {
    if (!_powered || _paused || _textVisible || !_initialized) {
        static unsigned long lastDebug = 0;
        if (millis() - lastDebug > 2000) {
            lastDebug = millis();
            Serial.printf("play() skip: pwr=%d pause=%d txt=%d init=%d\n",
                          _powered, _paused, _textVisible, _initialized);
        }
        return;
    }

    MjpegClass* mjpeg = (MjpegClass*)_decoder;
    if (mjpeg->readMjpegBuf()) {
        mjpeg->drawJpg();
    } else {
        // Loop video
        _stream->reset();
        mjpeg->reset();
    }
}

void VideoPlayer::pause() {
    _paused = true;
}

void VideoPlayer::resume() {
    Serial.printf("resume() called: was pwr=%d pause=%d\n", _powered, _paused);
    _paused = false;
    _powered = true;  // Ensure powered on when resuming
}

void VideoPlayer::togglePause() {
    _paused = !_paused;
}

void VideoPlayer::powerOff() {
    Serial.println("!!! powerOff() called !!!");
    if (!_powered) return;
    _powered = false;
    _paused = true;
    if (_display) {
        _display->fillScreen(BLACK);
    }
    digitalWrite(LCD_BL, LOW);
}

void VideoPlayer::powerOn() {
    if (_powered) return;
    _powered = true;
    _paused = false;
    digitalWrite(LCD_BL, HIGH);
}

void VideoPlayer::togglePower() {
    if (_powered) {
        powerOff();
    } else {
        powerOn();
    }
}

void VideoPlayer::setRotation(uint8_t rotation) {
    _rotation = rotation;
    if (_display) {
        _display->setRotation(rotation);
    }
}

void VideoPlayer::_drawTextBox() {
    if (!_display || _currentText.length() == 0) return;

    // Get actual display dimensions (accounts for rotation)
    int dispWidth = _display->width();
    int dispHeight = _display->height();

    // Text box dimensions
    const int boxHeight = 80;
    const int boxMargin = 5;
    const int textPadding = 8;
    const int boxY = dispHeight - boxHeight - boxMargin;

    // Draw semi-transparent background
    _display->fillRect(boxMargin, boxY, dispWidth - 2 * boxMargin, boxHeight, TEXT_BG_COLOR);

    // Draw border
    _display->drawRect(boxMargin, boxY, dispWidth - 2 * boxMargin, boxHeight, TEXT_BORDER_COLOR);
    _display->drawRect(boxMargin + 1, boxY + 1, dispWidth - 2 * boxMargin - 2, boxHeight - 2, TEXT_BORDER_COLOR);

    // Draw text
    _display->setTextColor(TEXT_COLOR);
    _display->setTextSize(2);
    _display->setTextWrap(true);

    // Calculate text position
    int textX = boxMargin + textPadding;
    int textY = boxY + textPadding;

    _display->setCursor(textX, textY);

    // Word wrap manually for better control
    String remaining = _currentText;
    int lineWidth = dispWidth - 2 * boxMargin - 2 * textPadding;
    int charWidth = 12;  // Approximate width per char at size 2
    int charsPerLine = lineWidth / charWidth;
    int lineHeight = 20;
    int maxLines = (boxHeight - 2 * textPadding) / lineHeight;

    int line = 0;
    while (remaining.length() > 0 && line < maxLines) {
        String lineToPrint;
        if (remaining.length() <= charsPerLine) {
            lineToPrint = remaining;
            remaining = "";
        } else {
            // Find last space before limit
            int breakPoint = charsPerLine;
            while (breakPoint > 0 && remaining[breakPoint] != ' ') {
                breakPoint--;
            }
            if (breakPoint == 0) breakPoint = charsPerLine;

            lineToPrint = remaining.substring(0, breakPoint);
            remaining = remaining.substring(breakPoint);
            remaining.trim();
        }

        _display->setCursor(textX, textY + line * lineHeight);
        _display->print(lineToPrint);
        line++;
    }
}

void VideoPlayer::showText(const char* text) {
    _currentText = text;
    _textVisible = true;
    _paused = true;

    // Clear area and draw text box
    if (_display) {
        _display->fillScreen(BLACK);
        _drawTextBox();
    }
}

void VideoPlayer::appendText(const char* chunk) {
    _currentText += chunk;

    // Redraw text box with updated text
    if (_textVisible && _display) {
        _drawTextBox();
    }
}

void VideoPlayer::clearText() {
    _currentText = "";
    if (_textVisible && _display) {
        _drawTextBox();
    }
}

void VideoPlayer::hideText() {
    _textVisible = false;
    _currentText = "";
    _paused = false;
    // Video will resume on next play() call
}

void VideoPlayer::fillScreen(uint16_t color) {
    if (_display) {
        _display->fillScreen(color);
    }
}

void VideoPlayer::refreshDisplay() {
    Serial.println("refreshDisplay() called");
    if (_display) {
        // Don't call begin() again - it hangs on second call
        // Just ensure backlight is on and try a simple draw operation
        digitalWrite(LCD_BL, HIGH);
        // Force a write operation to re-sync SPI state
        _display->startWrite();
        _display->endWrite();
        Serial.println("Display refresh done");
    }
}
