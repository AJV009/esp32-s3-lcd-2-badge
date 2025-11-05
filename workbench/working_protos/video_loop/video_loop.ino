/*******************************************************************************
 * ESP32-S3-LCD-2 MJPEG Video Player
 * Plays MJPEG video from flash memory in infinite loop
 ******************************************************************************/

#include <FFat.h>
#include <Arduino_GFX_Library.h>
#include "MjpegClass.h"

// Hardware configuration
#define LCD_CS 45
#define LCD_DC 42
#define LCD_BL 1

// Memory stream for PSRAM playback
class MemoryStream : public Stream {
  uint8_t *buf;
  size_t sz, pos;
public:
  MemoryStream(uint8_t *b, size_t s) : buf(b), sz(s), pos(0) {}
  int available() override { return sz - pos; }
  int read() override { return (pos < sz) ? buf[pos++] : -1; }
  int peek() override { return (pos < sz) ? buf[pos] : -1; }
  size_t readBytes(char *b, size_t len) override {
    size_t n = min(len, sz - pos);
    memcpy(b, buf + pos, n);
    pos += n;
    return n;
  }
  void reset() { pos = 0; }
  size_t write(uint8_t) override { return 0; }
  void flush() override {}
};

// Video player class
class VideoPlayer {
  Arduino_GFX *display;
  MjpegClass decoder;
  MemoryStream *stream;
  uint8_t *videoBuf, *decodeBuf;

  static VideoPlayer *instance;
  static int drawCallback(JPEGDRAW *d) {
    instance->display->draw16bitBeRGBBitmap(d->x, d->y, d->pPixels, d->iWidth, d->iHeight);
    return 1;
  }

public:
  VideoPlayer() : display(nullptr), stream(nullptr), videoBuf(nullptr), decodeBuf(nullptr) {
    instance = this;
  }

  bool begin() {
    // Init display
    Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, 39, 38, 40);
    display = new Arduino_ST7789(bus, -1, 1, true, 240, 320);
    if (!display->begin()) return false;
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);
    display->fillScreen(BLACK);

    // Mount flash and load video
    if (!FFat.begin(false, "", 1)) return false;
    File f = FFat.open("/output.mjpeg", "r");
    if (!f) return false;

    size_t sz = f.size();
    videoBuf = (uint8_t*)ps_malloc(sz);
    if (!videoBuf || f.read(videoBuf, sz) != sz) {
      f.close();
      return false;
    }
    f.close();

    // Setup decoder
    stream = new MemoryStream(videoBuf, sz);
    decodeBuf = (uint8_t*)malloc(320 * 240 / 2);
    return decodeBuf && decoder.setup(stream, decodeBuf, drawCallback, true, 0, 0, 320, 240);
  }

  void play() {
    if (decoder.readMjpegBuf()) {
      decoder.drawJpg();
    } else {
      stream->reset();
      decoder.reset();
    }
  }
};

VideoPlayer *VideoPlayer::instance = nullptr;
VideoPlayer player;

void setup() {
  if (!player.begin()) {
    pinMode(LED_BUILTIN, OUTPUT);
    while(1) { digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); delay(500); }
  }
}

void loop() {
  player.play();
}
