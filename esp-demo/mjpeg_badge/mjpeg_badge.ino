/*******************************************************************************
 * ESP32-S3-LCD-2 MJPEG Video Player - Flash Storage Version
 *
 * Loads video from flash memory (FFat) into PSRAM, then plays in loop.
 * NO SD CARD NEEDED!
 *
 * Setup:
 * 1. Upload this sketch
 * 2. Upload data folder with output.mjpeg using upload script
 ******************************************************************************/

#include <Arduino.h>
#include <FFat.h>
#include <Arduino_GFX_Library.h>
#include "MjpegClass.h"

// Pin Definitions
#define LCD_CS        45
#define LCD_DC        42
#define LCD_RST       -1
#define LCD_BL        1

#define LCD_WIDTH     240
#define LCD_HEIGHT    320
#define LCD_ROTATION  1  // Landscape = 320x240

#define MJPEG_FILENAME "/output.mjpeg"
#define MJPEG_BUFFER_SIZE (320 * 240 * 2 / 4)

// Global variables
uint8_t *mjpeg_file_buffer = NULL;
size_t mjpeg_file_size = 0;
uint8_t *mjpeg_buf = nullptr;

// Display objects
Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, 39, 38, 40);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, LCD_ROTATION, true, LCD_WIDTH, LCD_HEIGHT);

MjpegClass mjpeg;

// Memory stream for reading from PSRAM
class MemoryStream : public Stream {
private:
  uint8_t *buffer;
  size_t size;
  size_t position;
public:
  MemoryStream(uint8_t *buf, size_t sz) : buffer(buf), size(sz), position(0) {}
  int available() override { return size - position; }
  int read() override { return (position < size) ? buffer[position++] : -1; }
  int peek() override { return (position < size) ? buffer[position] : -1; }
  size_t readBytes(char *buf, size_t len) override {
    size_t to_read = min(len, size - position);
    memcpy(buf, buffer + position, to_read);
    position += to_read;
    return to_read;
  }
  void reset() { position = 0; }
  size_t write(uint8_t) override { return 0; }
  void flush() override {}
};

MemoryStream *memStream = nullptr;

// JPEG draw callback
static int jpegDrawCallback(JPEGDRAW *pDraw) {
  gfx->draw16bitBeRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
  return 1;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==========================================");
  Serial.println("ESP32-S3-LCD-2 MJPEG Player (Flash)");
  Serial.println("==========================================\n");

  // Step 1: Initialize display
  Serial.print("1. Init display... ");
  if (!gfx->begin()) {
    Serial.println("FAILED!");
    while(1) delay(1000);
  }
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(20, 80);
  gfx->println("ESP32-S3");
  gfx->setCursor(20, 110);
  gfx->println("Video Badge");
  gfx->setCursor(20, 150);
  gfx->setTextSize(1);
  gfx->println("Loading from flash...");
  Serial.println("OK");

  // Step 2: Mount flash filesystem
  Serial.print("2. Mount flash (FFat)... ");
  if (!FFat.begin(false, "", 1)) {  // maxOpenFiles = 1 to save RAM
    Serial.println("FAILED!");
    gfx->fillScreen(RED);
    gfx->setCursor(20, 100);
    gfx->setTextSize(2);
    gfx->println("Flash Error!");
    while(1) delay(1000);
  }
  Serial.println("OK");

  // Step 3: Check flash info
  Serial.printf("   Flash total: %d KB\n", FFat.totalBytes() / 1024);
  Serial.printf("   Flash used: %d KB\n", FFat.usedBytes() / 1024);

  // Step 4: Open video file from flash
  Serial.print("3. Open video file... ");
  File file = FFat.open(MJPEG_FILENAME, "r");
  if (!file) {
    Serial.println("FAILED!");
    Serial.println("   File not found in flash!");
    Serial.println("   Please upload output.mjpeg using 'Upload Filesystem Image'");
    gfx->fillScreen(RED);
    gfx->setCursor(10, 90);
    gfx->setTextSize(2);
    gfx->println("File Error!");
    gfx->setCursor(10, 120);
    gfx->setTextSize(1);
    gfx->println("Upload data folder");
    gfx->println("with output.mjpeg");
    while(1) delay(1000);
  }
  mjpeg_file_size = file.size();
  Serial.printf("OK (%d bytes = %d KB)\n", mjpeg_file_size, mjpeg_file_size/1024);

  // Step 5: Allocate PSRAM
  Serial.print("4. Allocate PSRAM... ");
  mjpeg_file_buffer = (uint8_t*)ps_malloc(mjpeg_file_size);
  if (!mjpeg_file_buffer) {
    Serial.println("FAILED! Not enough PSRAM");
    gfx->fillScreen(RED);
    gfx->setCursor(20, 100);
    gfx->setTextSize(2);
    gfx->println("Memory Error!");
    file.close();
    while(1) delay(1000);
  }
  Serial.printf("OK (%d KB allocated)\n", mjpeg_file_size/1024);

  // Step 6: Load file to PSRAM
  Serial.print("5. Load to PSRAM... ");
  gfx->setCursor(20, 180);
  gfx->println("Reading from flash...");

  size_t bytes_read = file.read(mjpeg_file_buffer, mjpeg_file_size);
  file.close();

  if (bytes_read != mjpeg_file_size) {
    Serial.printf("FAILED! Read %d/%d bytes\n", bytes_read, mjpeg_file_size);
    free(mjpeg_file_buffer);
    while(1) delay(1000);
  }
  Serial.println("OK");

  Serial.println("\n========================================");
  Serial.println("SUCCESS! Video loaded from flash");
  Serial.println("========================================\n");

  // Step 7: Setup MJPEG player
  Serial.print("6. Setup player... ");
  memStream = new MemoryStream(mjpeg_file_buffer, mjpeg_file_size);
  mjpeg_buf = (uint8_t*)malloc(MJPEG_BUFFER_SIZE);

  if (!mjpeg_buf || !mjpeg.setup(memStream, mjpeg_buf, jpegDrawCallback, true, 0, 0, 320, 240)) {
    Serial.println("FAILED!");
    while(1) delay(1000);
  }
  Serial.println("OK");

  Serial.println("\nStarting playback...\n");
  gfx->fillScreen(BLACK);
}

void loop() {
  if (mjpeg.readMjpegBuf()) {
    mjpeg.drawJpg();
  } else {
    // Loop back to start
    memStream->reset();
  }
}
