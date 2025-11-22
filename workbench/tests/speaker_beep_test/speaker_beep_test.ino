/*
 * MAX98357A Speaker Beep Test
 *
 * Simple test that plays beeps and boops to verify speaker hardware.
 * Plays a sequence of tones at different frequencies.
 *
 * Hardware Wiring:
 * ================
 * MAX98357A → ESP32-S3-LCD-2
 * ─────────────────────────
 * VIN   → 5V
 * GND   → GND
 * BCLK  → GPIO 6
 * LRC   → GPIO 7
 * DIN   → GPIO 8
 * GAIN  → 5V (for maximum volume) or Float (for 9dB)
 * SD    → VIN (always on)
 *
 * Speakers:
 * ─────────
 * SPK+ ───┬──── Speaker 1 (+)
 *         └──── Speaker 2 (+)
 *
 * SPK- ───┬──── Speaker 1 (-)
 *         └──── Speaker 2 (-)
 *
 * Expected Output:
 * ================
 * You should hear a sequence of beeps cycling through:
 * - 440 Hz (A note - musical A)
 * - 523 Hz (C note)
 * - 659 Hz (E note)
 * - 784 Hz (G note)
 * Then a rising sweep from 200 Hz to 2000 Hz
 *
 * If you hear this clearly, your speaker hardware is working!
 */

#include <driver/i2s.h>
#include <math.h>

// Speaker pins (I2S1)
#define SPK_BCK_PIN    6
#define SPK_WS_PIN     7
#define SPK_DOUT_PIN   8

// Audio config
#define I2S_PORT       I2S_NUM_1
#define SAMPLE_RATE    16000
#define BUFFER_SIZE    512

// LED for feedback
#define LED_PIN 1

int16_t audio_buffer[BUFFER_SIZE];

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  Serial.println("\n========================================");
  Serial.println("MAX98357A Speaker Beep Test");
  Serial.println("========================================\n");
  Serial.printf("Pins: BCK=%d, WS=%d, DOUT=%d\n\n", SPK_BCK_PIN, SPK_WS_PIN, SPK_DOUT_PIN);

  // Initialize I2S
  Serial.print("Initializing I2S... ");
  i2s_config_t i2s_config = {
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

  i2s_pin_config_t pin_config = {
    .bck_io_num = SPK_BCK_PIN,
    .ws_io_num = SPK_WS_PIN,
    .data_out_num = SPK_DOUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  if (i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL) != ESP_OK ||
      i2s_set_pin(I2S_PORT, &pin_config) != ESP_OK) {
    Serial.println("FAILED");
    Serial.println("ERROR: I2S initialization failed!");
    while(1) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(200);
    }
  }
  Serial.println("OK\n");

  Serial.println("========================================");
  Serial.println("Playing test tones...");
  Serial.println("========================================\n");
  Serial.println("You should hear:");
  Serial.println("  1. Four musical notes (A, C, E, G)");
  Serial.println("  2. A rising sweep (200-2000 Hz)");
  Serial.println("  3. Pattern repeats forever\n");

  delay(500);
}

void loop() {
  // Play sequence of musical notes
  playTone(440, 500);  // A note
  delay(100);

  playTone(523, 500);  // C note
  delay(100);

  playTone(659, 500);  // E note
  delay(100);

  playTone(784, 500);  // G note
  delay(100);

  // Rising sweep
  Serial.println("Playing sweep 200-2000 Hz...");
  for (int freq = 200; freq <= 2000; freq += 50) {
    playTone(freq, 50);
  }

  delay(1000);
  Serial.println("Repeating test sequence...\n");
}

void playTone(float frequency, int duration_ms) {
  Serial.printf("Playing %d Hz for %d ms\n", (int)frequency, duration_ms);

  unsigned long start = millis();
  int samples_sent = 0;

  while (millis() - start < duration_ms) {
    // Generate sine wave samples
    for (int i = 0; i < BUFFER_SIZE; i++) {
      float t = (float)(samples_sent + i) / SAMPLE_RATE;
      float sample = sin(2.0 * PI * frequency * t);

      // Convert to 16-bit with 50% volume (to prevent clipping)
      audio_buffer[i] = (int16_t)(sample * 16000);
    }

    // Send to speaker
    size_t bytes_written;
    i2s_write(I2S_PORT, audio_buffer, BUFFER_SIZE * sizeof(int16_t), &bytes_written, portMAX_DELAY);

    samples_sent += BUFFER_SIZE;

    // Blink LED while playing
    if ((millis() / 100) % 2 == 0) {
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(LED_PIN, LOW);
    }
  }
}
