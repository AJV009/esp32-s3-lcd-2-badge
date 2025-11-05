/*******************************************************************************
 * MJPEG Parser and Decoder Class
 *
 * This class parses MJPEG format files and decodes individual JPEG frames
 * using the JPEGDEC software decoder.
 *
 * Based on Arduino_GFX MJPEG examples
 * Adapted for ESP32-S3-LCD-2 Event Badge
 ******************************************************************************/

#pragma once

#include <FS.h>
#include <SD.h>
#include <JPEGDEC.h>

#define READ_BUFFER_SIZE 1024

class MjpegClass {
public:
  /*
   * Initialize MJPEG player
   *
   * @param input File stream from SD card
   * @param mjpeg_buf Buffer for reading compressed JPEG data
   * @param pfnDraw Callback function for drawing decoded pixels
   * @param useBigEndian Use big endian format for RGB565 output
   * @param x X position on screen
   * @param y Y position on screen
   * @param widthLimit Maximum width
   * @param heightLimit Maximum height
   * @return true if successful, false otherwise
   */
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

    if (!_read_buf) {
      Serial.println("ERROR: Failed to allocate read buffer");
      return false;
    }

    return true;
  }

  /*
   * Read next JPEG frame from MJPEG stream
   *
   * Searches for JPEG SOI marker (FF D8) and EOI marker (FF D9)
   * to extract a complete JPEG frame
   *
   * @return true if frame found, false if end of file
   */
  bool readMjpegBuf() {
    if (_inputindex == 0) {
      _buf_read = _input->readBytes(_read_buf, READ_BUFFER_SIZE);
      _inputindex += _buf_read;
    }
    _mjpeg_buf_offset = 0;
    int i = 0;
    bool found_FFD8 = false;

    // Search for JPEG Start of Image marker (FF D8)
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

    // Copy JPEG data and search for End of Image marker (FF D9)
    if (_buf_read > 0) {
      i = 3;
      while ((_buf_read > 0) && (!found_FFD9)) {
        if ((_mjpeg_buf_offset > 0) && (_mjpeg_buf[_mjpeg_buf_offset - 1] == 0xFF) &&
            (_p[0] == 0xD9)) {
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

      if (found_FFD9) {
        return true;
      }
    }

    return false;
  }

  /*
   * Decode and draw current JPEG frame
   *
   * Uses JPEGDEC library for software decoding
   *
   * @return true if successful, false otherwise
   */
  bool drawJpg() {
    _remain = _mjpeg_buf_offset;

    if (_jpeg.openRAM(_mjpeg_buf, _remain, _pfnDraw) != 1) {
      Serial.println("ERROR: Failed to open JPEG");
      return false;
    }

    if (_scale == -1) {
      // Calculate scale to fit screen
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
      Serial.println("ERROR: JPEG decode failed");
      _jpeg.close();
      return false;
    }

    _jpeg.close();
    return true;
  }

  /*
   * Get width of decoded frame
   */
  int16_t getWidth() {
    return _jpeg.getWidth();
  }

  /*
   * Get height of decoded frame
   */
  int16_t getHeight() {
    return _jpeg.getHeight();
  }

private:
  Stream *_input;
  uint8_t *_mjpeg_buf;
  JPEG_DRAW_CALLBACK *_pfnDraw;
  bool _useBigEndian;
  int _x;
  int _y;
  int _widthLimit;
  int _heightLimit;

  uint8_t *_read_buf = nullptr;
  int32_t _mjpeg_buf_offset = 0;

  JPEGDEC _jpeg;
  int _scale = -1;

  int32_t _inputindex = 0;
  int32_t _buf_read;
  int32_t _remain = 0;
};
