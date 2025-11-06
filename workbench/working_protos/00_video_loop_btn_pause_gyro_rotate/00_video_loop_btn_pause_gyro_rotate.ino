/*******************************************************************************
 * ESP32-S3-LCD-2 MJPEG Video Player with Gyro-based Auto-Rotation
 * Plays MJPEG video from flash memory in infinite loop
 * BOOT button (pin 0):
 *   - Click: Toggle pause/play
 *   - Long press: Power off/on (blank screen + backlight off)
 * Auto-rotation: Screen rotates 180° when device orientation changes
 ******************************************************************************/

#include <FFat.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <OneButton.h>
#include <FastIMU.h>
#include "MjpegClass.h"

// Hardware configuration
#define LCD_CS 45
#define LCD_DC 42
#define LCD_BL 1
#define BTN_BOOT 0

// IMU configuration
#define IMU_ADDRESS 0x6B
#define I2C_SDA 48
#define I2C_SCL 47

// IMU objects
QMI8658 IMU;
calData calib = {0};
AccelData accelData;

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

// Orientation manager with debounced rotation detection
class OrientationManager {
  const float THRESHOLD = 0.5;           // Hysteresis threshold (±0.5g)
  const unsigned long DEBOUNCE_MS = 1000; // 1 second stability required
  const unsigned long POLL_INTERVAL_MS = 50; // Poll at 20Hz

  uint8_t currentRotation;    // Stable rotation: 1 (USB right) or 3 (USB left)
  uint8_t pendingRotation;    // Candidate rotation during debounce (0 = none)
  unsigned long debounceStartTime;
  unsigned long lastPollTime;
  bool rotationJustChanged;   // One-shot flag for change detection
  bool imuAvailable;

public:
  OrientationManager() : currentRotation(1), pendingRotation(0),
                         debounceStartTime(0), lastPollTime(0),
                         rotationJustChanged(false), imuAvailable(true) {}

  void setImuAvailable(bool available) {
    imuAvailable = available;
  }

  void update() {
    if (!imuAvailable) return;

    unsigned long now = millis();
    if (now - lastPollTime < POLL_INTERVAL_MS) return;
    lastPollTime = now;

    // Read accelerometer
    IMU.update();
    IMU.getAccel(&accelData);
    float accelY = accelData.accelY;

    // Determine desired rotation based on accelY
    uint8_t desiredRotation = 0;
    if (accelY > THRESHOLD) {
      desiredRotation = 1;  // USB on right (normal)
    } else if (accelY < -THRESHOLD) {
      desiredRotation = 3;  // USB on left (180° flip)
    } else {
      desiredRotation = currentRotation; // Hysteresis zone - keep current
    }

    // State machine logic
    if (desiredRotation == currentRotation) {
      // Orientation matches current stable state
      pendingRotation = 0; // Cancel any pending transition
    } else if (pendingRotation == 0) {
      // Start new transition
      pendingRotation = desiredRotation;
      debounceStartTime = now;
    } else if (pendingRotation == desiredRotation) {
      // Transition in progress, check if debounce time elapsed
      if (now - debounceStartTime >= DEBOUNCE_MS) {
        // Commit rotation change
        currentRotation = pendingRotation;
        pendingRotation = 0;
        rotationJustChanged = true;
      }
    } else {
      // Desired rotation changed during debounce - restart
      pendingRotation = desiredRotation;
      debounceStartTime = now;
    }
  }

  uint8_t getRotation() {
    return currentRotation;
  }

  bool hasChanged() {
    if (rotationJustChanged) {
      rotationJustChanged = false; // Clear one-shot flag
      return true;
    }
    return false;
  }
};

// Video player class
class VideoPlayer {
  Arduino_GFX *display;
  MjpegClass decoder;
  MemoryStream *stream;
  uint8_t *videoBuf, *decodeBuf;
  bool paused;
  bool powered;

  static VideoPlayer *instance;
  static int drawCallback(JPEGDRAW *d) {
    instance->display->draw16bitBeRGBBitmap(d->x, d->y, d->pPixels, d->iWidth, d->iHeight);
    return 1;
  }

public:
  VideoPlayer() : display(nullptr), stream(nullptr), videoBuf(nullptr), decodeBuf(nullptr), paused(false), powered(true) {
    instance = this;
  }

  void togglePause() {
    paused = !paused;
  }

  void powerOff() {
    if (!powered) return;
    powered = false;
    paused = true;
    display->fillScreen(BLACK);
    digitalWrite(LCD_BL, LOW);
  }

  void powerOn() {
    if (powered) return;
    powered = true;
    paused = false;
    digitalWrite(LCD_BL, HIGH);
  }

  void togglePower() {
    if (powered) {
      powerOff();
    } else {
      powerOn();
    }
  }

  void setRotation(uint8_t rotation) {
    if (!display) return;
    display->setRotation(rotation);
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
    if (!powered || paused) return;

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
OrientationManager orientationMgr;

// Button setup
OneButton button(BTN_BOOT, true);

void onButtonClick() {
  player.togglePause();
}

void onButtonLongPressStart() {
  player.togglePower();
}

void setup() {
  // Initialize IMU
  Wire.begin(I2C_SDA, I2C_SCL);
  int imuErr = IMU.init(calib, IMU_ADDRESS);
  if (imuErr != 0) {
    orientationMgr.setImuAvailable(false);
  }
  // Note: If IMU fails, we continue anyway - rotation feature will be disabled
  // but video playback still works

  if (!player.begin()) {
    pinMode(LED_BUILTIN, OUTPUT);
    while(1) { digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); delay(500); }
  }

  button.attachClick(onButtonClick);
  button.attachLongPressStart(onButtonLongPressStart);
}

void loop() {
  orientationMgr.update();

  // Handle rotation changes
  if (orientationMgr.hasChanged()) {
    player.setRotation(orientationMgr.getRotation());
  }

  button.tick();
  player.play();
}
