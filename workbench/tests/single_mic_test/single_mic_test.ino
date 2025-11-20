/*
 * INMP441 Single Microphone Test
 *
 * Simple test sketch for validating one INMP441 microphone.
 * Reads mono I2S data and displays audio level.
 *
 * Wiring:
 *   INMP441:
 *     VDD  -> 3.3V
 *     GND  -> GND
 *     SCK  -> GPIO 2
 *     WS   -> GPIO 4
 *     SD   -> GPIO 18
 *     L/R  -> GND (for LEFT channel, or 3.3V for RIGHT channel)
 *
 * Expected Output:
 *   Single bar graph showing audio levels when you tap/blow on mic
 */

#include <driver/i2s.h>

// Pins verified from ESP32-S3-Touch-LCD-2 schematic PinOut
#define I2S_BCK_PIN   2     // Bit Clock (SCK on INMP441)
#define I2S_WS_PIN    4     // Word Select (WS on INMP441)
#define I2S_DIN_PIN   18    // Data In (SD on INMP441)
// Note: IO2/IO4 are camera pins (safe if no camera), IO18 is free

#define I2S_PORT      I2S_NUM_0
#define SAMPLE_RATE   16000
#define BUFFER_SIZE   512

int32_t samples[BUFFER_SIZE];

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== INMP441 Single Mic Test ===");
  Serial.printf("I2S Pins: BCK=%d, WS=%d, DIN=%d\n", I2S_BCK_PIN, I2S_WS_PIN, I2S_DIN_PIN);
  Serial.println("(Using GPIO from PinOut: IO2, IO4, IO18)\n");
  Serial.println("Configuring I2S...\n");

  // I2S configuration for single INMP441
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,  // Single mic on LEFT channel
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
  Serial.println("Tap or blow on the microphone to test.\n");
}

void loop() {
  size_t bytes_read = 0;

  // Read samples
  i2s_read(I2S_PORT, samples, BUFFER_SIZE * sizeof(int32_t), &bytes_read, portMAX_DELAY);

  int samples_read = bytes_read / sizeof(int32_t);

  // Calculate peak level
  int32_t peak = 0;

  for (int i = 0; i < samples_read; i++) {
    // INMP441 outputs 24-bit data in upper bits of 32-bit word
    int32_t sample = samples[i] >> 8;

    if (abs(sample) > peak) {
      peak = abs(sample);
    }
  }

  // Only print when sound is detected (above noise floor)
  if (peak > 1000) {
    Serial.print("LEVEL: ");
    printBar(peak, 8388607);  // 24-bit max value
    Serial.printf(" (%7d)\n", peak);
  }

  delay(100);  // Update ~10 times per second
}

void printBar(int32_t value, int32_t max_val) {
  int bars = map(value, 0, max_val, 0, 50);
  bars = constrain(bars, 0, 50);

  for (int i = 0; i < bars; i++) Serial.print("█");
  for (int i = bars; i < 50; i++) Serial.print(" ");
}
