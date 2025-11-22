/*
 * INMP441 Dual Microphone SD Card Recording Test
 * With PSRAM Buffering (No SD writes during recording)
 *
 * Records 30 seconds of stereo audio from two INMP441 microphones
 * to PSRAM first, then writes to SD card after recording completes.
 *
 * Hardware Wiring:
 * ================
 * ESP32-S3-LCD-2          INMP441 #1 (LEFT)      INMP441 #2 (RIGHT)
 * ────────────────        ─────────────────      ──────────────────
 * 3.3V ───────────┬────── VDD                    VDD
 *                 │
 * GND ────────────┼────── GND ──────────┬─────── GND
 *                 │                     │
 * GPIO 2 (BCK) ───┼────── SCK ──────────┼─────── SCK
 * GPIO 4 (WS) ────┼────── WS ───────────┼─────── WS
 * GPIO 18 (DIN) ──┴────── SD ───────────┴─────── SD
 *                         │                      │
 *                         L/R → GND              L/R → 3.3V
 *                           (LEFT)                 (RIGHT)
 *
 * Behavior:
 * =========
 * 1. On boot, allocates PSRAM buffer (~1.92 MB)
 * 2. Initializes I2S in stereo mode
 * 3. Records 30 seconds of audio to PSRAM (no SD interference!)
 * 4. Mounts SD card
 * 5. Writes complete recording to SD card
 * 6. Prints completion message
 * 7. Enters idle loop
 *
 * Output File:
 * ============
 * Format: WAV (PCM)
 * Sample Rate: 16000 Hz
 * Channels: 2 (Stereo)
 * Bit Depth: 16-bit
 * Duration: 30 seconds
 * Size: ~1.92 MB
 * Location: SD card root (/recording.wav)
 */

#include <driver/i2s.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

// I2S Pin Configuration
#define I2S_BCK_PIN   2     // Bit Clock
#define I2S_WS_PIN    4     // Word Select
#define I2S_DIN_PIN   18    // Data In

// SD Card SPI Pin Configuration (shared with LCD)
#define SD_CS    41    // Chip Select
#define SD_MOSI  38    // Master Out Slave In
#define SD_MISO  40    // Master In Slave Out
#define SD_SCK   39    // Serial Clock

// Recording Configuration
#define I2S_PORT        I2S_NUM_0
#define SAMPLE_RATE     16000
#define CHANNELS        2
#define BITS_PER_SAMPLE 16
#define RECORD_DURATION 30     // seconds
#define BUFFER_SIZE     2048   // samples (int16_t stereo pairs)

// Calculate total samples
const uint32_t TOTAL_SAMPLES = SAMPLE_RATE * CHANNELS * RECORD_DURATION;
const uint32_t WAV_DATA_SIZE = TOTAL_SAMPLES * (BITS_PER_SAMPLE / 8);
const uint32_t WAV_FILE_SIZE = WAV_DATA_SIZE + 36;  // 44 byte header - 8

// LED for visual feedback
#define LED_PIN 1  // Backlight pin

// Buffers
int32_t i2s_buffer[BUFFER_SIZE];      // I2S read buffer (32-bit samples)
int16_t* psram_buffer = nullptr;      // PSRAM buffer for entire recording

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // LED on during setup

  Serial.println("\n========================================");
  Serial.println("INMP441 Dual Mic SD Recording Test");
  Serial.println("(PSRAM Buffered - No SD interference)");
  Serial.println("========================================\n");

  // Check PSRAM availability
  Serial.printf("Total PSRAM: %u bytes\n", ESP.getPsramSize());
  Serial.printf("Free PSRAM: %u bytes\n", ESP.getFreePsram());

  // Allocate PSRAM buffer for entire recording
  Serial.printf("\nAllocating PSRAM buffer (%u bytes)... ", WAV_DATA_SIZE);
  psram_buffer = (int16_t*)ps_malloc(WAV_DATA_SIZE);
  if (!psram_buffer) {
    Serial.println("FAILED");
    Serial.println("ERROR: Cannot allocate PSRAM buffer");
    Serial.println("Need OPI PSRAM enabled in Arduino IDE settings");
    while(1) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(200);
    }
  }
  Serial.println("OK");
  Serial.printf("Free PSRAM after allocation: %u bytes\n\n", ESP.getFreePsram());

  // Initialize I2S
  Serial.print("Initializing I2S... ");
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // INMP441 outputs 24-bit in 32-bit words
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  // Stereo
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_DIN_PIN
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("FAILED (err=%d)\n", err);
    while(1) delay(1000);
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("FAILED (err=%d)\n", err);
    while(1) delay(1000);
  }
  Serial.println("OK");

  // Record to PSRAM (no SD interference!)
  Serial.printf("\nRecording %d seconds to PSRAM...\n", RECORD_DURATION);
  Serial.printf("Total samples: %u\n\n", TOTAL_SAMPLES);

  digitalWrite(LED_PIN, LOW);  // LED off before recording
  delay(500);

  bool record_success = recordToPSRAM();

  if (!record_success) {
    Serial.println("\n========================================");
    Serial.println("Recording FAILED!");
    Serial.println("========================================");
    while(1) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(500);
    }
  }

  Serial.println("\n========================================");
  Serial.println("Recording to PSRAM complete!");
  Serial.println("========================================\n");

  // Now write to SD card
  Serial.print("Initializing SD card... ");
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("FAILED");
    Serial.println("ERROR: Cannot mount SD card");
    Serial.println("Recording is in PSRAM but cannot save to SD");
    while(1) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(200);
    }
  }
  Serial.println("OK");

  Serial.print("Writing to SD card (/recording.wav)... ");
  bool write_success = writePSRAMToSD();

  if (write_success) {
    Serial.println("OK\n");
    Serial.println("========================================");
    Serial.println("SUCCESS!");
    Serial.println("========================================");
    Serial.println("\nFile saved to SD card as /recording.wav");
    Serial.println("You can remove the SD card and play it on your computer!");
    digitalWrite(LED_PIN, HIGH);  // LED on = success
  } else {
    Serial.println("FAILED\n");
    Serial.println("========================================");
    Serial.println("SD Write FAILED!");
    Serial.println("========================================");
    while(1) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(500);
    }
  }
}

void loop() {
  // Idle - blink LED slowly if failed
  if (digitalRead(LED_PIN) == LOW) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(500);
  } else {
    delay(1000);
  }
}

bool recordToPSRAM() {
  uint32_t samples_written = 0;
  uint32_t last_percent = 0;
  unsigned long start_time = millis();

  while (samples_written < TOTAL_SAMPLES) {
    // Blink LED during recording
    if ((millis() / 200) % 2 == 0) {
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(LED_PIN, LOW);
    }

    // Read I2S data
    size_t bytes_read = 0;
    esp_err_t err = i2s_read(I2S_PORT, i2s_buffer, BUFFER_SIZE * sizeof(int32_t),
                             &bytes_read, portMAX_DELAY);

    if (err != ESP_OK) {
      Serial.printf("\nERROR: I2S read failed (err=%d)\n", err);
      return false;
    }

    // Convert 32-bit to 16-bit and write directly to PSRAM buffer
    int samples_read = bytes_read / sizeof(int32_t);
    for (int i = 0; i < samples_read && samples_written < TOTAL_SAMPLES; i++) {
      // INMP441 outputs 24-bit data in upper bits of 32-bit word
      // Shift right by 16 to get 16-bit output (keeping MSBs)
      psram_buffer[samples_written++] = (int16_t)(i2s_buffer[i] >> 16);
    }

    // Print progress
    uint32_t percent = (samples_written * 100) / TOTAL_SAMPLES;
    if (percent != last_percent && percent % 10 == 0) {
      unsigned long elapsed = millis() - start_time;
      Serial.printf("Progress: %u%% (%u / %u samples, %lu ms)\n",
                    percent, samples_written, TOTAL_SAMPLES, elapsed);
      last_percent = percent;
    }
  }

  unsigned long total_time = millis() - start_time;
  Serial.printf("\nRecording finished: %u samples in %lu ms\n", samples_written, total_time);

  return true;
}

bool writePSRAMToSD() {
  // Open file for writing
  File file = SD.open("/recording.wav", FILE_WRITE);
  if (!file) {
    return false;
  }

  // Write WAV header
  writeWavHeader(file, WAV_DATA_SIZE);

  // Write entire PSRAM buffer to SD card
  size_t bytes_written = file.write((uint8_t*)psram_buffer, WAV_DATA_SIZE);
  file.close();

  return (bytes_written == WAV_DATA_SIZE);
}

void writeWavHeader(File &file, uint32_t data_size) {
  // RIFF header
  file.write((uint8_t*)"RIFF", 4);
  uint32_t file_size = data_size + 36;
  writeLittleEndian(file, file_size, 4);
  file.write((uint8_t*)"WAVE", 4);

  // fmt chunk
  file.write((uint8_t*)"fmt ", 4);
  writeLittleEndian(file, 16, 4);                    // Chunk size
  writeLittleEndian(file, 1, 2);                     // Audio format (1 = PCM)
  writeLittleEndian(file, CHANNELS, 2);              // Number of channels
  writeLittleEndian(file, SAMPLE_RATE, 4);           // Sample rate
  writeLittleEndian(file, SAMPLE_RATE * CHANNELS * (BITS_PER_SAMPLE / 8), 4); // Byte rate
  writeLittleEndian(file, CHANNELS * (BITS_PER_SAMPLE / 8), 2); // Block align
  writeLittleEndian(file, BITS_PER_SAMPLE, 2);       // Bits per sample

  // data chunk
  file.write((uint8_t*)"data", 4);
  writeLittleEndian(file, data_size, 4);
}

void writeLittleEndian(File &file, uint32_t value, int bytes) {
  for (int i = 0; i < bytes; i++) {
    file.write((uint8_t)(value & 0xFF));
    value >>= 8;
  }
}
