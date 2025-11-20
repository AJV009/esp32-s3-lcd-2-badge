/*
 * INMP441 Dual Microphone Pin Test
 *
 * This sketch reads stereo I2S data from two INMP441 microphones
 * and displays both channel levels to verify hardware connectivity.
 *
 * Wiring (adjust pins below if needed):
 *   Both INMP441s:
 *     VDD  -> 3.3V
 *     GND  -> GND
 *     SCK  -> GPIO 18
 *     WS   -> GPIO 19
 *     SD   -> GPIO 20
 *
 *   INMP441 #1 (LEFT):  L/R -> GND
 *   INMP441 #2 (RIGHT): L/R -> 3.3V
 *
 * Expected Output:
 *   You should see different peak values for LEFT and RIGHT channels
 *   when you tap/blow on each microphone separately.
 */

#include <driver/i2s.h>

// === CORRECTED PINS (verified from PinOut) ===
#define I2S_BCK_PIN   2     // Bit Clock (SCK on INMP441)
#define I2S_WS_PIN    4     // Word Select (WS on INMP441)
#define I2S_DIN_PIN   18    // Data In (SD on INMP441)
// ==============================================
// Note: IO2 and IO4 are camera pins (safe if no camera attached)
// IO18 is free and not assigned to any peripheral

#define I2S_PORT      I2S_NUM_0
#define SAMPLE_RATE   16000
#define BUFFER_SIZE   512

int32_t samples[BUFFER_SIZE];

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== INMP441 Dual Mic Pin Test ===");
  Serial.printf("I2S Pins: BCK=%d, WS=%d, DIN=%d\n", I2S_BCK_PIN, I2S_WS_PIN, I2S_DIN_PIN);
  Serial.println("Configuring I2S...\n");

  // I2S configuration for dual INMP441 (stereo)
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
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
    Serial.printf("ERROR: i2s_driver_install failed: %d\n", err);
    while(1) delay(1000);
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("ERROR: i2s_set_pin failed: %d\n", err);
    while(1) delay(1000);
  }

  Serial.println("I2S initialized successfully!");
  Serial.println("Reading audio data...\n");
  Serial.println("Tap or blow on each microphone to test.");
  Serial.println("You should see different peak values for LEFT vs RIGHT.\n");
}

void loop() {
  size_t bytes_read = 0;

  // Read stereo samples (L and R interleaved)
  i2s_read(I2S_PORT, samples, BUFFER_SIZE * sizeof(int32_t), &bytes_read, portMAX_DELAY);

  int samples_read = bytes_read / sizeof(int32_t);

  // Calculate peak levels for left and right channels
  int32_t left_peak = 0;
  int32_t right_peak = 0;

  for (int i = 0; i < samples_read; i += 2) {
    // Samples are 32-bit, but INMP441 outputs 24-bit data in upper bits
    int32_t left = samples[i] >> 8;      // Left channel (even indices)
    int32_t right = samples[i+1] >> 8;   // Right channel (odd indices)

    if (abs(left) > left_peak) left_peak = abs(left);
    if (abs(right) > right_peak) right_peak = abs(right);
  }

  // Only print when sound is detected on either channel (above noise floor)
  if (left_peak > 1000 || right_peak > 1000) {
    Serial.print("LEFT:  ");
    printBar(left_peak, 8388607);  // 24-bit max value
    Serial.printf(" (%7d)  |  ", left_peak);

    Serial.print("RIGHT: ");
    printBar(right_peak, 8388607);
    Serial.printf(" (%7d)\n", right_peak);
  }

  delay(100);  // Update ~10 times per second
}

void printBar(int32_t value, int32_t max_val) {
  int bars = map(value, 0, max_val, 0, 40);
  for (int i = 0; i < bars; i++) Serial.print("█");
  for (int i = bars; i < 40; i++) Serial.print(" ");
}
