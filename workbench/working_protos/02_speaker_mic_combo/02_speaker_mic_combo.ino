/*
 * Record, Downmix, and Playback Loop
 *
 * Records 30 seconds of stereo audio from dual INMP441 microphones,
 * downmixes to mono using HYBRID algorithm, then loops playback
 * through MAX98357A speaker.
 *
 * Hardware:
 * =========
 * INMP441 Microphones (I2S0 RX):
 *   Mic #1 (LEFT):  L/R -> GND
 *   Mic #2 (RIGHT): L/R -> 3.3V
 *   Both share: BCK=GPIO2, WS=GPIO4, DIN=GPIO18
 *
 * MAX98357A Speaker (I2S1 TX):
 *   VIN  -> 5V (or 3.3V)
 *   GND  -> GND
 *   BCLK -> GPIO 6
 *   LRC  -> GPIO 7
 *   DIN  -> GPIO 8
 *   GAIN -> Float (9dB), GND (12dB), or VDD (15dB)
 *
 * Behavior:
 * =========
 * 1. Records 30 seconds stereo to PSRAM
 * 2. Downmixes to mono using HYBRID algorithm
 * 3. Loops playback forever
 *
 * LED Feedback:
 * =============
 * - Blinking during recording
 * - Solid ON during playback
 */

#include <driver/i2s.h>

// Microphone pins (I2S0)
#define MIC_BCK_PIN    2
#define MIC_WS_PIN     4
#define MIC_DIN_PIN    18

// Speaker pins (I2S1)
#define SPK_BCK_PIN    6
#define SPK_WS_PIN     7
#define SPK_DOUT_PIN   8

// Recording configuration
#define SAMPLE_RATE       16000
#define BITS_PER_SAMPLE   16
#define RECORD_DURATION   30
#define BUFFER_SIZE       2048

// Calculate sizes
const uint32_t STEREO_SAMPLES = SAMPLE_RATE * 2 * RECORD_DURATION;
const uint32_t MONO_SAMPLES = SAMPLE_RATE * RECORD_DURATION;
const uint32_t MONO_DATA_SIZE = MONO_SAMPLES * (BITS_PER_SAMPLE / 8);

// LED
#define LED_PIN 1

// Buffers in PSRAM
int32_t i2s_buffer[BUFFER_SIZE];
int16_t* stereo_buffer = nullptr;
int16_t* mono_buffer = nullptr;

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  Serial.println("\n========================================");
  Serial.println("Record -> Downmix -> Playback Loop");
  Serial.println("========================================\n");

  // Check PSRAM
  Serial.printf("Total PSRAM: %u bytes\n", ESP.getPsramSize());
  Serial.printf("Free PSRAM: %u bytes\n\n", ESP.getFreePsram());

  // Allocate stereo buffer
  uint32_t stereo_bytes = STEREO_SAMPLES * sizeof(int16_t);
  Serial.printf("Allocating stereo buffer (%u bytes)... ", stereo_bytes);
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
  Serial.println("OK\n");

  // Initialize I2S0 for microphones (RX)
  Serial.print("Initializing I2S0 (microphones)... ");
  i2s_config_t mic_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t mic_pins = {
    .bck_io_num = MIC_BCK_PIN,
    .ws_io_num = MIC_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_DIN_PIN
  };

  if (i2s_driver_install(I2S_NUM_0, &mic_config, 0, NULL) != ESP_OK ||
      i2s_set_pin(I2S_NUM_0, &mic_pins) != ESP_OK) {
    Serial.println("FAILED");
    error_blink();
  }
  Serial.println("OK");

  // Initialize I2S1 for speaker (TX)
  Serial.print("Initializing I2S1 (speaker)... ");
  i2s_config_t spk_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,  // Mono
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t spk_pins = {
    .bck_io_num = SPK_BCK_PIN,
    .ws_io_num = SPK_WS_PIN,
    .data_out_num = SPK_DOUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  if (i2s_driver_install(I2S_NUM_1, &spk_config, 0, NULL) != ESP_OK ||
      i2s_set_pin(I2S_NUM_1, &spk_pins) != ESP_OK) {
    Serial.println("FAILED");
    error_blink();
  }
  Serial.println("OK\n");

  // Record stereo
  Serial.printf("Recording %d seconds (stereo)...\n\n", RECORD_DURATION);
  digitalWrite(LED_PIN, LOW);
  delay(500);

  if (!recordStereoToPSRAM()) {
    Serial.println("\nRecording FAILED!");
    error_blink();
  }

  Serial.println("\n========================================");
  Serial.println("Recording complete!");
  Serial.println("========================================\n");

  // Downmix to mono using HYBRID
  Serial.print("Applying HYBRID downmix... ");
  downmixHybrid();
  Serial.println("OK");

  // Analyze and normalize
  normalizeAudio();
  Serial.println();

  Serial.println("========================================");
  Serial.println("Starting playback loop...");
  Serial.println("========================================\n");

  digitalWrite(LED_PIN, HIGH);
}

void loop() {
  playMonoBuffer();
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
    if (i2s_read(I2S_NUM_0, i2s_buffer, BUFFER_SIZE * sizeof(int32_t),
                 &bytes_read, portMAX_DELAY) != ESP_OK) {
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

void downmixHybrid() {
  float left_energy = 0.0f;
  float right_energy = 0.0f;
  const float ENERGY_DECAY = 0.95f;
  const float MIN_WEIGHT = 0.30f;  // Minimum 30% per channel
  const float MAX_WEIGHT = 0.70f;  // Maximum 70% per channel
  const float GAIN_BOOST = 4.0f;   // 4x volume boost (compensates for 9dB hardware gain)

  for (uint32_t i = 0; i < MONO_SAMPLES; i++) {
    int16_t left = stereo_buffer[i * 2];
    int16_t right = stereo_buffer[i * 2 + 1];

    // Track energy of each channel
    left_energy = left_energy * ENERGY_DECAY + abs(left) * (1.0f - ENERGY_DECAY);
    right_energy = right_energy * ENERGY_DECAY + abs(right) * (1.0f - ENERGY_DECAY);

    // Calculate raw weights
    float total_energy = left_energy + right_energy;
    float left_weight = (total_energy > 0) ? (left_energy / total_energy) : 0.5f;

    // Constrain to 30%-70% range
    if (left_weight < MIN_WEIGHT) left_weight = MIN_WEIGHT;
    if (left_weight > MAX_WEIGHT) left_weight = MAX_WEIGHT;

    float right_weight = 1.0f - left_weight;

    // Weighted mix with gain boost and clipping protection
    int32_t mixed = (int32_t)(left * left_weight + right * right_weight) * GAIN_BOOST;
    if (mixed > 32767) mixed = 32767;
    if (mixed < -32768) mixed = -32768;
    mono_buffer[i] = (int16_t)mixed;
  }
}

void normalizeAudio() {
  // Find peak level in recording
  int16_t peak = 0;
  for (uint32_t i = 0; i < MONO_SAMPLES; i++) {
    int16_t abs_val = abs(mono_buffer[i]);
    if (abs_val > peak) peak = abs_val;
  }

  Serial.printf("Peak level: %d / 32767 (%.1f%%)\n", peak, (peak * 100.0f) / 32767.0f);

  // Calculate normalization gain to maximize volume
  if (peak > 0) {
    float norm_gain = 32000.0f / peak;  // Leave small headroom (32000 instead of 32767)

    // Limit max boost to 8x to avoid amplifying noise too much
    if (norm_gain > 8.0f) norm_gain = 8.0f;

    Serial.printf("Applying normalization: %.2fx gain\n", norm_gain);

    // Apply normalization
    for (uint32_t i = 0; i < MONO_SAMPLES; i++) {
      int32_t normalized = (int32_t)(mono_buffer[i] * norm_gain);
      if (normalized > 32767) normalized = 32767;
      if (normalized < -32768) normalized = -32768;
      mono_buffer[i] = (int16_t)normalized;
    }
  } else {
    Serial.println("WARNING: Recording is silent (peak = 0)!");
  }
}

void playMonoBuffer() {
  size_t bytes_written = 0;
  static unsigned long last_print = 0;

  // Play entire mono buffer
  for (uint32_t i = 0; i < MONO_SAMPLES; i += BUFFER_SIZE) {
    uint32_t samples_to_write = min((uint32_t)BUFFER_SIZE, MONO_SAMPLES - i);

    i2s_write(I2S_NUM_1, &mono_buffer[i], samples_to_write * sizeof(int16_t),
              &bytes_written, portMAX_DELAY);

    // Print status every ~2 seconds
    if (millis() - last_print > 2000) {
      Serial.printf("Playing... sample %u / %u\n", i, MONO_SAMPLES);
      last_print = millis();
    }
  }

  Serial.println("Playback complete, looping...\n");
}

void error_blink() {
  while(1) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(200);
  }
}
