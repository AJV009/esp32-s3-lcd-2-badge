/*******************************************************************************
 * ESP32-S3-LCD-2 Event Badge - MJPEG Video Player
 *
 * This sketch plays an MJPEG video file from SD card in an infinite loop
 * on the ESP32-S3-LCD-2 display.
 *
 * Hardware: ESP32-S3-LCD-2 by Waveshare
 * Display: ST7789 2.0" IPS LCD (240x320)
 * Video: output.mjpeg (320x240)
 *
 * Required Libraries:
 * - Arduino_GFX_Library (should be installed)
 * - ESP32_JPEG (included in ESP32 Arduino Core >= 3.0)
 *
 * Setup:
 * 1. Copy output.mjpeg to SD card root directory
 * 2. Insert SD card into ESP32-S3-LCD-2
 * 3. Upload this sketch
 *
 * Created for OAISYS25 Badge Project
 ******************************************************************************/

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Arduino_GFX_Library.h>

#include "MjpegClass.h"

/*******************************************************************************
 * Pin Definitions - ESP32-S3-LCD-2 Board
 ******************************************************************************/
// Display pins (SPI)
#define LCD_SCLK      39
#define LCD_MOSI      38
#define LCD_MISO      40
#define LCD_CS        45
#define LCD_DC        42
#define LCD_RST       -1  // No reset pin
#define LCD_BL        1   // Backlight

// SD Card pins (shares SPI with display)
#define SD_CS         41
#define SD_SCLK       39  // Shared with LCD
#define SD_MOSI       38  // Shared with LCD
#define SD_MISO       40  // Shared with LCD

// Display settings
#define LCD_WIDTH     240
#define LCD_HEIGHT    320
#define LCD_ROTATION  1   // Landscape mode (makes it 320x240)

/*******************************************************************************
 * MJPEG Configuration
 ******************************************************************************/
#define MJPEG_FILENAME    "/output.mjpeg"
#define MJPEG_BUFFER_SIZE (320 * 240 * 2 / 10)  // Buffer for compressed JPEG data
#define OUTPUT_BUFFER_SIZE (320 * 240 * 2)      // Buffer for decoded RGB565 frame

/*******************************************************************************
 * Global Objects
 ******************************************************************************/
// SPI bus for both display and SD card
SPIClass spi_bus = SPIClass(FSPI);

// Display setup
Arduino_DataBus *bus = new Arduino_ESP32SPI(
  LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, LCD_MISO, FSPI, true);

Arduino_GFX *gfx = new Arduino_ST7789(
  bus, LCD_RST, LCD_ROTATION, true /* IPS */, LCD_WIDTH, LCD_HEIGHT);

// MJPEG player
static MjpegClass mjpeg;

// Buffers
uint8_t *mjpeg_buf = nullptr;
uint16_t *output_buf = nullptr;

/*******************************************************************************
 * Setup Function
 ******************************************************************************/
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000); // Wait for serial or timeout

  Serial.println();
  Serial.println("===========================================");
  Serial.println("  ESP32-S3-LCD-2 Event Badge MJPEG Player");
  Serial.println("===========================================");
  Serial.println();

  // Initialize display
  Serial.print("Initializing display... ");
  if (!gfx->begin()) {
    Serial.println("FAILED!");
    Serial.println("ERROR: Display initialization failed!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("OK");

  // Clear screen and show startup message
  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(10, 100);
  gfx->println("Event Badge");
  gfx->setCursor(10, 130);
  gfx->println("Loading...");

  // Turn on backlight
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  Serial.println("Display ready (320x240 landscape mode)");

  // Initialize SPI for SD card
  Serial.print("Initializing SD card... ");
  spi_bus.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS, spi_bus, 40000000)) {
    Serial.println("FAILED!");
    Serial.println("ERROR: SD Card Mount Failed!");
    Serial.println("Please check:");
    Serial.println("  - SD card is inserted");
    Serial.println("  - SD card is formatted (FAT32)");
    Serial.println("  - output.mjpeg is in root directory");

    gfx->fillScreen(RED);
    gfx->setCursor(10, 100);
    gfx->setTextColor(WHITE);
    gfx->println("SD Card Error!");
    gfx->setCursor(10, 130);
    gfx->println("Check card");

    while (1) {
      delay(1000);
    }
  }
  Serial.println("OK");

  // Check if MJPEG file exists
  Serial.print("Looking for ");
  Serial.print(MJPEG_FILENAME);
  Serial.print("... ");

  if (!SD.exists(MJPEG_FILENAME)) {
    Serial.println("NOT FOUND!");
    Serial.println("ERROR: output.mjpeg not found on SD card!");

    gfx->fillScreen(RED);
    gfx->setCursor(10, 90);
    gfx->setTextColor(WHITE);
    gfx->println("File Not Found!");
    gfx->setCursor(10, 120);
    gfx->setTextSize(1);
    gfx->println("Please copy output.mjpeg");
    gfx->setCursor(10, 140);
    gfx->println("to SD card root directory");

    while (1) {
      delay(1000);
    }
  }

  File mjpegFile = SD.open(MJPEG_FILENAME);
  size_t fileSize = mjpegFile.size();
  mjpegFile.close();
  Serial.println("FOUND");
  Serial.print("File size: ");
  Serial.print(fileSize);
  Serial.println(" bytes");

  // Allocate buffers
  Serial.print("Allocating buffers... ");

  // Use aligned allocation for better performance with SIMD
  mjpeg_buf = (uint8_t *)heap_caps_aligned_alloc(16, MJPEG_BUFFER_SIZE, MALLOC_CAP_8BIT);
  if (!mjpeg_buf) {
    Serial.println("FAILED!");
    Serial.println("ERROR: Failed to allocate MJPEG buffer!");
    gfx->fillScreen(RED);
    gfx->setCursor(10, 100);
    gfx->println("Memory Error!");
    while (1) delay(1000);
  }

  output_buf = (uint16_t *)heap_caps_aligned_alloc(16, OUTPUT_BUFFER_SIZE, MALLOC_CAP_8BIT);
  if (!output_buf) {
    Serial.println("FAILED!");
    Serial.println("ERROR: Failed to allocate output buffer!");
    free(mjpeg_buf);
    gfx->fillScreen(RED);
    gfx->setCursor(10, 100);
    gfx->println("Memory Error!");
    while (1) delay(1000);
  }

  Serial.println("OK");
  Serial.print("MJPEG buffer: ");
  Serial.print(MJPEG_BUFFER_SIZE);
  Serial.println(" bytes");
  Serial.print("Output buffer: ");
  Serial.print(OUTPUT_BUFFER_SIZE);
  Serial.println(" bytes");

  // Setup MJPEG player
  Serial.print("Initializing MJPEG player... ");
  if (!mjpeg.setup(MJPEG_FILENAME, mjpeg_buf, output_buf, OUTPUT_BUFFER_SIZE, true)) {
    Serial.println("FAILED!");
    Serial.println("ERROR: MJPEG setup failed!");
    gfx->fillScreen(RED);
    gfx->setCursor(10, 100);
    gfx->println("Player Error!");
    while (1) delay(1000);
  }
  Serial.println("OK");

  // All ready!
  Serial.println();
  Serial.println("===========================================");
  Serial.println("  System ready! Starting playback...");
  Serial.println("===========================================");
  Serial.println();

  delay(1000);  // Brief pause before starting
  gfx->fillScreen(BLACK);
}

/*******************************************************************************
 * Main Loop - Plays video continuously
 ******************************************************************************/
void loop() {
  static unsigned long loop_count = 0;
  static unsigned long frame_count = 0;
  static unsigned long loop_start_ms = 0;

  loop_start_ms = millis();
  loop_count++;

  Serial.print("Loop #");
  Serial.println(loop_count);

  // Play through entire video
  while (mjpeg.readMjpegBuf()) {
    // Decode JPEG frame
    if (mjpeg.decodeJpg()) {
      // Get decoded frame dimensions
      int16_t w = mjpeg.getWidth();
      int16_t h = mjpeg.getHeight();

      // Center on screen (should be perfect match: 320x240)
      int16_t x = (gfx->width() - w) / 2;
      int16_t y = (gfx->height() - h) / 2;

      // Display frame (using Big Endian RGB565)
      gfx->draw16bitBeRGBBitmap(x, y, output_buf, w, h);

      frame_count++;
    }

    // Small delay to control frame rate (optional)
    // delay(1);  // Uncomment if playback is too fast
  }

  // Video finished, loop back to start
  unsigned long loop_duration = millis() - loop_start_ms;

  Serial.print("  Frames displayed: ");
  Serial.println(frame_count);
  Serial.print("  Loop duration: ");
  Serial.print(loop_duration);
  Serial.println(" ms");
  Serial.print("  Average FPS: ");
  Serial.println(1000.0 * frame_count / loop_duration, 1);
  Serial.println("  Restarting...");
  Serial.println();

  // Close and reopen file for next loop
  mjpeg.close();

  // Small delay before restarting (optional)
  delay(100);

  // Restart MJPEG player
  if (!mjpeg.setup(MJPEG_FILENAME, mjpeg_buf, output_buf, OUTPUT_BUFFER_SIZE, true)) {
    Serial.println("ERROR: Failed to restart MJPEG player!");
    gfx->fillScreen(RED);
    gfx->setCursor(10, 100);
    gfx->setTextColor(WHITE);
    gfx->println("Restart Error!");
    while (1) delay(1000);
  }

  frame_count = 0;  // Reset frame counter for next loop
}
