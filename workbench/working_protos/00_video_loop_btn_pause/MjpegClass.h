/*******************************************************************************
 * MJPEG Parser and Decoder
 * Parses MJPEG format and decodes JPEG frames using JPEGDEC library
 ******************************************************************************/

#pragma once
#include <FS.h>
#include <JPEGDEC.h>

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

  // Read next JPEG frame from MJPEG stream (searches for FF D8/FF D9 markers)
  bool readMjpegBuf() {
    if (_inputindex == 0) {
      _buf_read = _input->readBytes(_read_buf, READ_BUFFER_SIZE);
      _inputindex += _buf_read;
    }
    _mjpeg_buf_offset = 0;
    int i = 0;
    bool found_FFD8 = false;

    // Search for JPEG Start of Image (FF D8)
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

    // Copy JPEG data and search for End of Image (FF D9)
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

  // Decode and draw current JPEG frame
  bool drawJpg() {
    _remain = _mjpeg_buf_offset;

    if (_jpeg.openRAM(_mjpeg_buf, _remain, _pfnDraw) != 1) return false;

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
      _jpeg.close();
      return false;
    }

    _jpeg.close();
    return true;
  }

  // Reset decoder state for looping
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
