/*
 * INMP441 Dual Microphone to Mono Recording Test
 * ALL THREE ALGORITHMS - Compare Them All!
 *
 * Records 30 seconds of stereo audio ONCE, then creates THREE mono files
 * using different downmix algorithms so you can compare quality.
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
 * Downmixing Algorithms (ALL applied automatically):
 * ==================================================
 * 1. SIMPLE_AVERAGE:  Mono = (L + R) / 2
 *    → /recording_average.wav
 *
 * 2. ADAPTIVE_MIX:    Mono = weighted average based on signal strength
 *    → /recording_adaptive.wav
 *
 * 3. HYBRID_MIX:      Adaptive weighting with 30% minimum per channel
 *    → /recording_hybrid.wav
 *    Best of both: Adaptive's clarity + Average's width!
 *
 * Behavior:
 * =========
 * 1. Records 30 seconds of stereo to PSRAM (once!)
 * 2. Downmixes using SIMPLE_AVERAGE → saves recording_average.wav
 * 3. Downmixes using ADAPTIVE_MIX → saves recording_adaptive.wav
 * 4. Downmixes using HYBRID_MIX → saves recording_hybrid.wav
 * 5. LED on = success
 *
 * Output Files (all on SD card):
 * ==============================
 * /recording_average.wav   - Standard (L+R)/2 - Wide, full sound
 * /recording_adaptive.wav  - Intelligent weighted - Loud & clear
 * /recording_hybrid.wav    - Best of both - Clarity + Width!
 *
 * Each file: 16kHz, Mono, 16-bit, ~960 KB
 */

#include <driver/i2s.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

// I2S Pin Configuration
#define I2S_BCK_PIN   2     // Bit Clock
#define I2S_WS_PIN    4     // Word Select
#define I2S_DIN_PIN   18    // Data In

// SD Card SPI Pin Configuration
#define SD_CS    41
#define SD_MOSI  38
#define SD_MISO  40
#define SD_SCK   39

// Recording Configuration
#define I2S_PORT        I2S_NUM_0
#define SAMPLE_RATE     16000
#define BITS_PER_SAMPLE 16
#define RECORD_DURATION 30
#define BUFFER_SIZE     2048

// Calculate sizes
const uint32_t STEREO_SAMPLES = SAMPLE_RATE * 2 * RECORD_DURATION;  // L+R interleaved
const uint32_t MONO_SAMPLES = SAMPLE_RATE * RECORD_DURATION;        // Mono only
const uint32_t MONO_DATA_SIZE = MONO_SAMPLES * (BITS_PER_SAMPLE / 8);

// LED for visual feedback
#define LED_PIN 1

// Buffers
int32_t i2s_buffer[BUFFER_SIZE];      // I2S read buffer (32-bit samples)
int16_t* stereo_buffer = nullptr;     // PSRAM: stereo recording (L,R,L,R,...)
int16_t* mono_buffer = nullptr;       // PSRAM: mono downmix result (reused for all 3 algos)

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  Serial.println("\n========================================");
  Serial.println("INMP441 Dual Mic to Mono Recording");
  Serial.println("ALL THREE ALGORITHMS - Compare!");
  Serial.println("========================================\n");

  Serial.println("Will create 3 files:");
  Serial.println("  1. /recording_average.wav  - Standard (L+R)/2");
  Serial.println("  2. /recording_adaptive.wav - Intelligent (loud & clear)");
  Serial.println("  3. /recording_hybrid.wav   - Best of both!");
  Serial.println();

  // Check PSRAM
  Serial.printf("Total PSRAM: %u bytes\n", ESP.getPsramSize());
  Serial.printf("Free PSRAM: %u bytes\n", ESP.getFreePsram());

  // Allocate stereo buffer
  uint32_t stereo_bytes = STEREO_SAMPLES * sizeof(int16_t);
  Serial.printf("\nAllocating stereo buffer (%u bytes)... ", stereo_bytes);
  stereo_buffer = (int16_t*)ps_malloc(stereo_bytes);
  if (!stereo_buffer) {
    Serial.println("FAILED");
    error_blink();
  }
  Serial.println("OK");

  // Allocate mono buffer
  Serial.printf("Allocating mono buffer (%u bytes)... ", MONO_DATA_SIZE);
  mono_buffer = (int16_t*)ps_malloc(MONO_DATA_SIZE);
  if (!mono_buffer) {
    Serial.println("FAILED");
    error_blink();
  }
  Serial.println("OK");
  Serial.printf("Free PSRAM after allocation: %u bytes\n\n", ESP.getFreePsram());

  // Initialize I2S
  Serial.print("Initializing I2S... ");
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
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

  if (i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL) != ESP_OK ||
      i2s_set_pin(I2S_PORT, &pin_config) != ESP_OK) {
    Serial.println("FAILED");
    error_blink();
  }
  Serial.println("OK");

  // Record stereo to PSRAM
  Serial.printf("\nRecording %d seconds (stereo)...\n\n", RECORD_DURATION);
  digitalWrite(LED_PIN, LOW);
  delay(500);

  if (!recordStereoToPSRAM()) {
    Serial.println("\nRecording FAILED!");
    error_blink();
  }

  Serial.println("\n========================================");
  Serial.println("Stereo recording complete!");
  Serial.println("========================================\n");

  // Initialize SD card
  Serial.print("Initializing SD card... ");
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("FAILED");
    error_blink();
  }
  Serial.println("OK\n");

  // Create all three downmix versions
  Serial.println("Creating downmixed mono files...\n");

  // 1. SIMPLE_AVERAGE
  Serial.print("1/3: Applying SIMPLE_AVERAGE... ");
  downmixSimpleAverage();
  Serial.print("Writing /recording_average.wav... ");
  if (!writeMonoToSD("/recording_average.wav")) {
    Serial.println("FAILED");
    error_blink();
  }
  Serial.println("OK");

  // 2. ADAPTIVE_MIX
  Serial.print("2/3: Applying ADAPTIVE_MIX... ");
  downmixAdaptive();
  Serial.print("Writing /recording_adaptive.wav... ");
  if (!writeMonoToSD("/recording_adaptive.wav")) {
    Serial.println("FAILED");
    error_blink();
  }
  Serial.println("OK");

  // 3. HYBRID_MIX
  Serial.print("3/3: Applying HYBRID_MIX... ");
  downmixHybrid();
  Serial.print("Writing /recording_hybrid.wav... ");
  if (!writeMonoToSD("/recording_hybrid.wav")) {
    Serial.println("FAILED");
    error_blink();
  }
  Serial.println("OK\n");

  Serial.println("========================================");
  Serial.println("SUCCESS! All 3 files created!");
  Serial.println("========================================");
  Serial.println("\nFiles on SD card:");
  Serial.println("  /recording_average.wav  - Wide, full sound");
  Serial.println("  /recording_adaptive.wav - Loud & clear");
  Serial.println("  /recording_hybrid.wav   - Clarity + Width!");
  Serial.println("\nRemove SD card and compare on your computer!");

  digitalWrite(LED_PIN, HIGH);
}

void loop() {
  delay(1000);
}

bool recordStereoToPSRAM() {
  uint32_t samples_written = 0;
  uint32_t last_percent = 0;
  unsigned long start_time = millis();

  while (samples_written < STEREO_SAMPLES) {
    // Blink LED
    if ((millis() / 200) % 2 == 0) {
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(LED_PIN, LOW);
    }

    // Read I2S
    size_t bytes_read = 0;
    if (i2s_read(I2S_PORT, i2s_buffer, BUFFER_SIZE * sizeof(int32_t),
                 &bytes_read, portMAX_DELAY) != ESP_OK) {
      Serial.println("\nI2S read error");
      return false;
    }

    // Convert and store
    int samples_read = bytes_read / sizeof(int32_t);
    for (int i = 0; i < samples_read && samples_written < STEREO_SAMPLES; i++) {
      stereo_buffer[samples_written++] = (int16_t)(i2s_buffer[i] >> 16);
    }

    // Progress
    uint32_t percent = (samples_written * 100) / STEREO_SAMPLES;
    if (percent != last_percent && percent % 10 == 0) {
      Serial.printf("Progress: %u%% (%u / %u samples)\n",
                    percent, samples_written, STEREO_SAMPLES);
      last_percent = percent;
    }
  }

  Serial.printf("\nRecorded: %u samples in %lu ms\n", samples_written, millis() - start_time);
  return true;
}

void downmixSimpleAverage() {
  for (uint32_t i = 0; i < MONO_SAMPLES; i++) {
    int16_t left = stereo_buffer[i * 2];
    int16_t right = stereo_buffer[i * 2 + 1];

    // Standard (L + R) / 2
    mono_buffer[i] = ((int32_t)left + (int32_t)right) / 2;
  }
}

void downmixHybrid() {
  float left_energy = 0.0f;
  float right_energy = 0.0f;
  const float ENERGY_DECAY = 0.95f;
  const float MIN_WEIGHT = 0.30f;  // Minimum 30% per channel (max 70%)
  const float MAX_WEIGHT = 0.70f;

  for (uint32_t i = 0; i < MONO_SAMPLES; i++) {
    int16_t left = stereo_buffer[i * 2];
    int16_t right = stereo_buffer[i * 2 + 1];

    // Track energy of each channel
    left_energy = left_energy * ENERGY_DECAY + abs(left) * (1.0f - ENERGY_DECAY);
    right_energy = right_energy * ENERGY_DECAY + abs(right) * (1.0f - ENERGY_DECAY);

    // Calculate raw weights
    float total_energy = left_energy + right_energy;
    float left_weight = (total_energy > 0) ? (left_energy / total_energy) : 0.5f;

    // Constrain to 30%-70% range (this maintains "width" while keeping clarity)
    if (left_weight < MIN_WEIGHT) left_weight = MIN_WEIGHT;
    if (left_weight > MAX_WEIGHT) left_weight = MAX_WEIGHT;

    float right_weight = 1.0f - left_weight;

    // Weighted mix
    mono_buffer[i] = (int16_t)(left * left_weight + right * right_weight);
  }
}

void downmixAdaptive() {
  float left_energy = 0.0f;
  float right_energy = 0.0f;
  const float ENERGY_DECAY = 0.95f;

  for (uint32_t i = 0; i < MONO_SAMPLES; i++) {
    int16_t left = stereo_buffer[i * 2];
    int16_t right = stereo_buffer[i * 2 + 1];

    // Track energy of each channel
    left_energy = left_energy * ENERGY_DECAY + abs(left) * (1.0f - ENERGY_DECAY);
    right_energy = right_energy * ENERGY_DECAY + abs(right) * (1.0f - ENERGY_DECAY);

    // Calculate weights (favor channel with more energy)
    float total_energy = left_energy + right_energy;
    float left_weight = (total_energy > 0) ? (left_energy / total_energy) : 0.5f;
    float right_weight = 1.0f - left_weight;

    // Weighted mix
    mono_buffer[i] = (int16_t)(left * left_weight + right * right_weight);
  }
}

bool writeMonoToSD(const char* filename) {
  File file = SD.open(filename, FILE_WRITE);
  if (!file) return false;

  // Write WAV header (MONO)
  writeWavHeader(file, MONO_DATA_SIZE, 1);  // 1 channel = mono

  // Write mono buffer
  size_t written = file.write((uint8_t*)mono_buffer, MONO_DATA_SIZE);
  file.close();

  return (written == MONO_DATA_SIZE);
}

void writeWavHeader(File &file, uint32_t data_size, uint16_t channels) {
  // RIFF header
  file.write((uint8_t*)"RIFF", 4);
  uint32_t file_size = data_size + 36;
  writeLittleEndian(file, file_size, 4);
  file.write((uint8_t*)"WAVE", 4);

  // fmt chunk
  file.write((uint8_t*)"fmt ", 4);
  writeLittleEndian(file, 16, 4);                    // Chunk size
  writeLittleEndian(file, 1, 2);                     // PCM
  writeLittleEndian(file, channels, 2);              // Channels (1=mono, 2=stereo)
  writeLittleEndian(file, SAMPLE_RATE, 4);           // Sample rate
  writeLittleEndian(file, SAMPLE_RATE * channels * (BITS_PER_SAMPLE / 8), 4); // Byte rate
  writeLittleEndian(file, channels * (BITS_PER_SAMPLE / 8), 2); // Block align
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

void error_blink() {
  while(1) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(200);
  }
}
