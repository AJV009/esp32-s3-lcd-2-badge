/*
 * MAX98357A Speaker Test
 *
 * Tests the MAX98357A I2S audio amplifier by playing white noise.
 * Useful for verifying speaker wiring and volume levels.
 *
 * Wiring:
 *   MAX98357A:
 *     VIN  -> 5V (or 3.3V)
 *     GND  -> GND
 *     BCLK -> GPIO 6
 *     LRC  -> GPIO 7
 *     DIN  -> GPIO 8
 *     GAIN -> Float (9dB), GND (12dB), or VDD (15dB)
 *     SD   -> VIN (always on) or GPIO for control
 *
 * Expected Output:
 *   White noise playing from speaker at full volume
 */

#include <driver/i2s.h>

// Pins verified from ESP32-S3-Touch-LCD-2 schematic PinOut
#define I2S_BCK_PIN   6     // Bit Clock (BCLK on MAX98357A)
#define I2S_WS_PIN    7     // Word Select (LRC on MAX98357A)
#define I2S_DOUT_PIN  8     // Data Out (DIN on MAX98357A)
// Note: These are camera pins (safe if no camera)

#define I2S_PORT      I2S_NUM_1  // Use I2S1 (different from mic on I2S0)
#define SAMPLE_RATE   44100
#define BUFFER_SIZE   512

int16_t samples[BUFFER_SIZE];

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== MAX98357A Speaker Test ===");
  Serial.printf("I2S Pins: BCK=%d, WS=%d, DOUT=%d\n", I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);
  Serial.println("(Using I2S1 for speaker output)\n");
  Serial.println("Configuring I2S...\n");

  // I2S configuration for MAX98357A
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = I2S_DOUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("ERROR: i2s_driver_install failed: %d\n", err);
    while(1) delay(1000);
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("ERROR: i2s_set_pin failed: %d\n", err);
    while(1) delay(1000);
  }

  // Set volume to maximum (no attenuation)
  i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);

  Serial.println("I2S initialized successfully!");
  Serial.println("Playing white noise at full volume...\n");
  Serial.println("WARNING: This will be LOUD! Reduce volume if needed.\n");

  delay(1000);
}

void loop() {
  size_t bytes_written = 0;

  // Generate white noise samples at full volume
  for (int i = 0; i < BUFFER_SIZE; i++) {
    // Generate random 16-bit samples (full range: -32768 to 32767)
    samples[i] = random(-32768, 32767);
  }

  // Write samples to I2S (blocking)
  i2s_write(I2S_PORT, samples, BUFFER_SIZE * sizeof(int16_t), &bytes_written, portMAX_DELAY);

  // Print status every ~1 second
  static unsigned long last_print = 0;
  if (millis() - last_print > 1000) {
    Serial.printf("Playing... %d bytes written\n", bytes_written);
    last_print = millis();
  }
}
