/*******************************************************************************
 * MJPEG Parser and Decoder Class
 *
 * This class parses MJPEG format files and decodes individual JPEG frames
 * using the ESP32-S3 hardware JPEG decoder.
 *
 * Based on Arduino_GFX MJPEG examples
 * Adapted for ESP32-S3-LCD-2 Event Badge
 ******************************************************************************/

#pragma once

#if defined(ESP32)
#include <FS.h>
#include <SD.h>
#include <ESP32_JPEG_Library.h>

#define READ_BATCH_SIZE 1024  // Read 1KB at a time from SD card

class MjpegClass {
public:
  /*
   * Initialize MJPEG player
   *
   * @param path Path to MJPEG file on SD card
   * @param mjpeg_buf Buffer for reading compressed JPEG data
   * @param output_buf Buffer for decoded RGB565 frame data
   * @param output_buf_size Size of output buffer in bytes
   * @param useBigEndian Use big endian format for RGB565 output
   * @return true if successful, false otherwise
   */
  bool setup(const char *path, uint8_t *mjpeg_buf,
             uint16_t *output_buf, size_t output_buf_size,
             bool useBigEndian) {

    // Open MJPEG file
    _mjpeg_file = SD.open(path, FILE_READ);
    if (!_mjpeg_file) {
      Serial.print("ERROR: Failed to open file: ");
      Serial.println(path);
      return false;
    }

    // Store buffer pointers
    _mjpeg_buf = mjpeg_buf;
    _output_buf = (uint8_t *)output_buf;
    _output_buf_size = output_buf_size;
    _useBigEndian = useBigEndian;
    _read = 0;

    Serial.print("Opened MJPEG file: ");
    Serial.println(path);
    Serial.print("File size: ");
    Serial.print(_mjpeg_file.size());
    Serial.println(" bytes");

    return true;
  }

  /*
   * Read next JPEG frame from MJPEG file
   *
   * Searches for JPEG SOI marker (FF D8) and EOI marker (FF D9)
   * to extract a complete JPEG frame
   *
   * @return true if frame found, false if end of file
   */
  bool readMjpegBuf() {
    if (!_mjpeg_file) {
      return false;
    }

    // Handle remaining data from previous read
    if (_read == 0) {
      // Buffer is empty, read fresh data
      _read = _mjpeg_file.read(_mjpeg_buf, READ_BATCH_SIZE);
      _p = _mjpeg_buf;
    } else {
      // Move remaining data to start of buffer
      memmove(_mjpeg_buf, _p, _read);
      _p = _mjpeg_buf;
    }

    // Search for JPEG Start of Image marker (FF D8)
    bool found_FFD8 = false;
    while ((_read > 0) && (!found_FFD8)) {
      while ((_read > 1) && (!found_FFD8)) {
        --_read;
        if ((*_p++ == 0xFF) && (*_p == 0xD8)) {  // SOI marker
          found_FFD8 = true;
        }
      }

      if (!found_FFD8) {
        // Keep searching in new data
        if (*_p == 0xFF) {
          // Might be start of SOI, keep FF byte
          _mjpeg_buf[0] = 0xFF;
          _read = _mjpeg_file.read(_mjpeg_buf + 1, READ_BATCH_SIZE) + 1;
        } else {
          // Read new batch
          _read = _mjpeg_file.read(_mjpeg_buf, READ_BATCH_SIZE);
        }
        _p = _mjpeg_buf;
      }
    }

    if (!found_FFD8) {
      // No more frames found
      return false;
    }

    // Rewind pointer to include FF D8
    --_p;
    ++_read;

    // Move SOI to start of buffer if needed
    if (_p > _mjpeg_buf) {
      memmove(_mjpeg_buf, _p, _read);
      _p = _mjpeg_buf;
    }

    // Skip past SOI marker
    _p += 2;
    _read -= 2;

    // Ensure we have data to search
    if (_read == 0) {
      _read = _mjpeg_file.read(_p, READ_BATCH_SIZE);
    }

    // Search for JPEG End of Image marker (FF D9)
    bool found_FFD9 = false;
    while ((_read > 0) && (!found_FFD9)) {
      while ((_read > 1) && (!found_FFD9)) {
        --_read;
        if ((*_p++ == 0xFF) && (*_p == 0xD9)) {  // EOI marker
          found_FFD9 = true;
        }
      }

      if (!found_FFD9) {
        // Read more data
        _read += _mjpeg_file.read(_p + _read, READ_BATCH_SIZE);
      }
    }

    if (found_FFD9) {
      // Complete JPEG frame found
      ++_p;
      --_read;
      return true;
    }

    // Incomplete frame
    return false;
  }

  /*
   * Decode current JPEG frame to RGB565
   *
   * Uses ESP32-S3 hardware JPEG decoder for fast decoding
   *
   * @return true if successful, false otherwise
   */
  bool decodeJpg() {
    // Configure JPEG decoder
    jpeg_dec_config_t config = {
      .output_type = _useBigEndian ? JPEG_RAW_TYPE_RGB565_BE : JPEG_RAW_TYPE_RGB565_LE,
      .rotate = JPEG_ROTATE_0D,
    };

    // Open decoder
    _jpeg_dec = jpeg_dec_open(&config);
    if (!_jpeg_dec) {
      Serial.println("ERROR: Failed to open JPEG decoder");
      return false;
    }

    // Allocate I/O callback structure
    _jpeg_io = (jpeg_dec_io_t *)calloc(1, sizeof(jpeg_dec_io_t));
    if (!_jpeg_io) {
      Serial.println("ERROR: Failed to allocate JPEG I/O structure");
      jpeg_dec_close(_jpeg_dec);
      return false;
    }

    // Allocate header info structure
    _out_info = (jpeg_dec_header_info_t *)calloc(1, sizeof(jpeg_dec_header_info_t));
    if (!_out_info) {
      Serial.println("ERROR: Failed to allocate JPEG header structure");
      free(_jpeg_io);
      jpeg_dec_close(_jpeg_dec);
      return false;
    }

    // Set input buffer (JPEG data)
    _jpeg_io->inbuf = _mjpeg_buf;
    _jpeg_io->inbuf_len = _p - _mjpeg_buf;

    // Parse JPEG header to get dimensions
    esp_err_t ret = jpeg_dec_parse_header(_jpeg_dec, _jpeg_io, _out_info);
    if (ret != ESP_OK) {
      Serial.print("ERROR: JPEG header parse failed: ");
      Serial.println(ret);
      free(_out_info);
      free(_jpeg_io);
      jpeg_dec_close(_jpeg_dec);
      return false;
    }

    // Store frame dimensions
    _w = _out_info->width;
    _h = _out_info->height;

    // Check if output buffer is large enough
    if ((_w * _h * 2) > _output_buf_size) {
      Serial.print("ERROR: Frame too large for buffer! ");
      Serial.print(_w);
      Serial.print("x");
      Serial.print(_h);
      Serial.print(" needs ");
      Serial.print(_w * _h * 2);
      Serial.print(" bytes, have ");
      Serial.println(_output_buf_size);
      free(_out_info);
      free(_jpeg_io);
      jpeg_dec_close(_jpeg_dec);
      return false;
    }

    // Set output buffer
    _jpeg_io->outbuf = _output_buf;

    // Decode JPEG to RGB565
    ret = jpeg_dec_process(_jpeg_dec, _jpeg_io);
    if (ret != ESP_OK) {
      Serial.print("ERROR: JPEG decode failed: ");
      Serial.println(ret);
      free(_out_info);
      free(_jpeg_io);
      jpeg_dec_close(_jpeg_dec);
      return false;
    }

    // Cleanup
    free(_out_info);
    free(_jpeg_io);
    jpeg_dec_close(_jpeg_dec);

    return true;
  }

  /*
   * Get width of decoded frame
   */
  int16_t getWidth() {
    return _w;
  }

  /*
   * Get height of decoded frame
   */
  int16_t getHeight() {
    return _h;
  }

  /*
   * Close MJPEG file
   */
  void close() {
    if (_mjpeg_file) {
      _mjpeg_file.close();
    }
  }

private:
  File _mjpeg_file;              // SD card file handle
  uint8_t *_mjpeg_buf;           // Buffer for compressed JPEG data
  uint8_t *_output_buf;          // Buffer for decoded RGB565 data
  size_t _output_buf_size;       // Size of output buffer
  bool _useBigEndian;            // RGB565 byte order

  // JPEG decoder handles
  jpeg_dec_handle_t *_jpeg_dec;
  jpeg_dec_io_t *_jpeg_io;
  jpeg_dec_header_info_t *_out_info;

  // Frame dimensions
  int16_t _w = 0;
  int16_t _h = 0;

  // Parsing state
  uint8_t *_p;                   // Current read position
  int32_t _read;                 // Bytes remaining in buffer
};

#endif // defined(ESP32)
