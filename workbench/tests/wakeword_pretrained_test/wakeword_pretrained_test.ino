/*
 * microWakeWord - Hey Daisy
 *
 * Wake word detection using custom trained microWakeWord TFLite model.
 * Uses EdgeNeuron library with microfrontend for audio preprocessing.
 *
 * Configuration loaded from JSON manifest file on SD card.
 *
 * Based on: https://github.com/kahrendt/microWakeWord
 *
 * Audio: 16kHz, 16-bit mono
 * Features: 40 bins, 30ms window, 10ms stride
 * Detection: Sliding window probability averaging
 */

#include <ArduinoJson.h>

// Include EdgeNeuron first to trigger Arduino library build
#include <EdgeNeuron.h>

// Additional TFLite components for streaming model support
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"

// Microfrontend for audio feature extraction
#include "tensorflow/lite/experimental/microfrontend/lib/frontend.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"

#include <driver/i2s.h>
#include <SD.h>
#include <SPI.h>

//=============================================================================
// FIXED CONFIGURATION
//=============================================================================

// Paths on SD card
#define CONFIG_PATH     "/models/hey_daisy.json"
#define MODEL_PATH      "/models/hey_daisy.tflite"

// Hardware pins (INMP441 microphone)
#define MIC_BCK  2
#define MIC_WS   4
#define MIC_DIN  18
#define SD_CS    41
#define LED_PIN  1

// Audio config
#define SAMPLE_RATE     16000
#define AUDIO_CHUNK_MS  10
#define AUDIO_CHUNK_SAMPLES (SAMPLE_RATE * AUDIO_CHUNK_MS / 1000)

// Feature config (matches microWakeWord training)
#define FEATURE_WINDOW_MS   30
#define FEATURE_STRIDE_MS   10
#define FEATURE_BINS        40
#define FEATURE_FRAMES      3
#define FEATURE_STRIDE_SAMPLES (SAMPLE_RATE * FEATURE_STRIDE_MS / 1000)

// Memory config
#define TENSOR_ARENA_SIZE   500000
#define VAR_ARENA_SIZE      50000
#define MAX_RESOURCE_VARS   100

// Debug
#define DEBUG_FEATURES      1

//=============================================================================
// TUNABLE CONFIGURATION (loaded from JSON)
//=============================================================================

struct WakeWordConfig {
  char wake_word[32];
  float probability_cutoff;
  int sliding_window_size;
  int min_high_frames;
  float min_frame_prob;
  int cooldown_ms;
};

// Default values (overridden by JSON if present)
static WakeWordConfig config = {
  .wake_word = "Hey Daisy",
  .probability_cutoff = 0.5f,
  .sliding_window_size = 10,
  .min_high_frames = 6,
  .min_frame_prob = 0.5f,
  .cooldown_ms = 1500
};

//=============================================================================
// GLOBALS
//=============================================================================

// TFLite globals
static uint8_t* tensor_arena = nullptr;
static uint8_t* var_arena = nullptr;
static uint8_t* model_data = nullptr;
static uint32_t model_size = 0;

static tflite::AllOpsResolver resolver;
static tflite::MicroAllocator* var_allocator = nullptr;
static tflite::MicroResourceVariables* resource_vars = nullptr;
static tflite::MicroInterpreter* interpreter = nullptr;
static TfLiteTensor* input_tensor = nullptr;
static TfLiteTensor* output_tensor = nullptr;

// Audio frontend
static FrontendState frontend_state;

// Audio buffers
#define AUDIO_BUFFER_SIZE 1024
static int32_t i2s_buffer[512];
static int16_t audio_buffer[AUDIO_BUFFER_SIZE];
static int audio_buffer_pos = 0;

// Feature buffer for 3 frames
static uint16_t feature_buffer[FEATURE_FRAMES * FEATURE_BINS];
static int feature_frame_count = 0;

// Sliding window for probability averaging (max size)
#define MAX_SLIDING_WINDOW 20
static float probability_window[MAX_SLIDING_WINDOW];
static int window_pos = 0;
static bool window_filled = false;

// Cooldown to prevent multiple triggers
static unsigned long last_detection_ms = 0;

//=============================================================================
// FORWARD DECLARATIONS
//=============================================================================

bool initSD();
bool loadConfig(const char* path);
bool loadModelFromSD(const char* path);
bool initMicrophone();
bool initFrontend();
bool initInterpreter();
bool captureAudio();
float runInference();
bool detectWakeword(float probability);
void errorBlink();

//=============================================================================
// SETUP
//=============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("\n=== microWakeWord - Hey Daisy ===\n");

  // Allocate arenas in PSRAM
  tensor_arena = (uint8_t*)ps_malloc(TENSOR_ARENA_SIZE);
  var_arena = (uint8_t*)ps_malloc(VAR_ARENA_SIZE);
  if (!tensor_arena || !var_arena) {
    Serial.println("ERROR: Arena allocation failed");
    errorBlink();
  }

  // Initialize SD card
  if (!initSD()) {
    Serial.println("ERROR: SD card init failed");
    errorBlink();
  }
  Serial.println("SD Card: OK");

  // Load configuration from JSON
  if (!loadConfig(CONFIG_PATH)) {
    Serial.println("WARNING: Config load failed, using defaults");
  }

  // Print loaded config
  Serial.println("\nConfiguration:");
  Serial.printf("  Wake word: \"%s\"\n", config.wake_word);
  Serial.printf("  Probability cutoff: %.2f\n", config.probability_cutoff);
  Serial.printf("  Sliding window size: %d\n", config.sliding_window_size);
  Serial.printf("  Min high frames: %d\n", config.min_high_frames);
  Serial.printf("  Min frame prob: %.2f\n", config.min_frame_prob);
  Serial.printf("  Cooldown: %d ms\n", config.cooldown_ms);
  Serial.println();

  // Load model from SD
  if (!loadModelFromSD(MODEL_PATH)) {
    Serial.println("ERROR: Model load failed");
    Serial.printf("Copy model file to SD:%s\n", MODEL_PATH);
    errorBlink();
  }
  Serial.printf("Model loaded: %u bytes\n", model_size);

  // Initialize TFLite interpreter
  if (!initInterpreter()) {
    Serial.println("ERROR: TFLite init failed");
    errorBlink();
  }
  Serial.println("TFLite: OK");
  Serial.printf("  Arena used: %d bytes\n", interpreter->arena_used_bytes());

  // Print tensor info
  Serial.printf("  Input: dims=[");
  for (int d = 0; d < input_tensor->dims->size; d++) {
    Serial.printf("%d%s", input_tensor->dims->data[d],
                  d < input_tensor->dims->size - 1 ? "," : "");
  }
  Serial.printf("], type=%d\n", input_tensor->type);

  Serial.printf("  Output: dims=[");
  for (int d = 0; d < output_tensor->dims->size; d++) {
    Serial.printf("%d%s", output_tensor->dims->data[d],
                  d < output_tensor->dims->size - 1 ? "," : "");
  }
  Serial.printf("], type=%d\n", output_tensor->type);

  // Print quantization parameters
  Serial.printf("  Input quant: scale=%.6f, zero_point=%d\n",
                input_tensor->params.scale, input_tensor->params.zero_point);
  Serial.printf("  Output quant: scale=%.6f, zero_point=%d\n",
                output_tensor->params.scale, output_tensor->params.zero_point);

  // Initialize microphone
  if (!initMicrophone()) {
    Serial.println("ERROR: Microphone init failed");
    errorBlink();
  }
  Serial.println("Microphone: OK");

  // Initialize microfrontend
  if (!initFrontend()) {
    Serial.println("ERROR: Frontend init failed");
    errorBlink();
  }
  Serial.println("Frontend: OK");

  // Initialize probability window
  for (int i = 0; i < MAX_SLIDING_WINDOW; i++) {
    probability_window[i] = 0.0f;
  }

  Serial.printf("\nListening for \"%s\"...\n", config.wake_word);
  Serial.println("================================\n");
}

//=============================================================================
// MAIN LOOP
//=============================================================================

void loop() {
  if (!captureAudio()) {
    return;
  }

  while (audio_buffer_pos >= FEATURE_STRIDE_SAMPLES) {
    size_t num_samples_read;
    FrontendOutput frontend_output = FrontendProcessSamples(
        &frontend_state, audio_buffer, audio_buffer_pos, &num_samples_read);

    if (num_samples_read == 0) {
      break;
    }

    if (num_samples_read < audio_buffer_pos) {
      memmove(audio_buffer, audio_buffer + num_samples_read,
              (audio_buffer_pos - num_samples_read) * sizeof(int16_t));
    }
    audio_buffer_pos -= num_samples_read;

    if (frontend_output.size == FEATURE_BINS) {
      if (feature_frame_count >= FEATURE_FRAMES) {
        memmove(feature_buffer, feature_buffer + FEATURE_BINS,
                (FEATURE_FRAMES - 1) * FEATURE_BINS * sizeof(uint16_t));
        feature_frame_count = FEATURE_FRAMES - 1;
      }

      memcpy(feature_buffer + feature_frame_count * FEATURE_BINS,
             frontend_output.values, FEATURE_BINS * sizeof(uint16_t));
      feature_frame_count++;

      if (feature_frame_count >= FEATURE_FRAMES) {
#if DEBUG_FEATURES
        static int debug_count = 0;
        if (++debug_count >= 30) {
          debug_count = 0;
          uint32_t energy = 0;
          uint16_t max_feat = 0;
          for (int i = 0; i < FEATURE_FRAMES * FEATURE_BINS; i++) {
            energy += feature_buffer[i];
            if (feature_buffer[i] > max_feat) max_feat = feature_buffer[i];
          }
          Serial.printf("Features: max=%u, sum=%lu\n", max_feat, energy);
        }
#endif
        float prob = runInference();

        if (detectWakeword(prob)) {
          Serial.printf("\n>>> %s DETECTED! <<<\n\n", config.wake_word);
          digitalWrite(LED_PIN, HIGH);
          delay(500);
          digitalWrite(LED_PIN, LOW);

          for (int i = 0; i < config.sliding_window_size; i++) {
            probability_window[i] = 0.0f;
          }
          window_filled = false;
        }
      }
    }
  }
}

//=============================================================================
// CONFIG LOADING
//=============================================================================

bool loadConfig(const char* path) {
  File f = SD.open(path);
  if (!f) {
    Serial.printf("Cannot open config: %s\n", path);
    return false;
  }

  // Parse JSON
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();

  if (error) {
    Serial.printf("JSON parse error: %s\n", error.c_str());
    return false;
  }

  // Load wake word name
  const char* wake_word = doc["wake_word"] | "Hey Daisy";
  strncpy(config.wake_word, wake_word, sizeof(config.wake_word) - 1);

  // Load detection parameters from "micro" object
  JsonObject micro = doc["micro"];
  if (micro) {
    config.probability_cutoff = micro["probability_cutoff"] | 0.5f;
    config.sliding_window_size = micro["sliding_window_average_size"] | 10;
    config.min_high_frames = micro["min_high_frames"] | 6;
    config.min_frame_prob = micro["min_frame_prob"] | 0.5f;
    config.cooldown_ms = micro["cooldown_ms"] | 1500;
  }

  // Clamp sliding window size to max
  if (config.sliding_window_size > MAX_SLIDING_WINDOW) {
    config.sliding_window_size = MAX_SLIDING_WINDOW;
  }

  Serial.printf("Config loaded: %s\n", path);
  return true;
}

//=============================================================================
// MODEL & INTERPRETER
//=============================================================================

bool initInterpreter() {
  const tflite::Model* model = tflite::GetModel(model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.printf("Model version mismatch: %d vs %d\n",
                  model->version(), TFLITE_SCHEMA_VERSION);
    return false;
  }

  var_allocator = tflite::MicroAllocator::Create(var_arena, VAR_ARENA_SIZE);
  if (!var_allocator) {
    Serial.println("Failed to create var allocator");
    return false;
  }

  resource_vars = tflite::MicroResourceVariables::Create(var_allocator, MAX_RESOURCE_VARS);
  if (!resource_vars) {
    Serial.println("Failed to create resource variables");
    return false;
  }

  static tflite::MicroInterpreter static_interpreter(
      model, resolver, tensor_arena, TENSOR_ARENA_SIZE, resource_vars);
  interpreter = &static_interpreter;

  if (interpreter->AllocateTensors() != kTfLiteOk) {
    Serial.println("AllocateTensors failed");
    return false;
  }

  input_tensor = interpreter->input(0);
  output_tensor = interpreter->output(0);

  if (!input_tensor || !output_tensor) {
    Serial.println("Failed to get tensors");
    return false;
  }

  return true;
}

bool loadModelFromSD(const char* path) {
  File f = SD.open(path);
  if (!f) {
    Serial.printf("Cannot open: %s\n", path);
    return false;
  }

  model_size = f.size();
  model_data = (uint8_t*)ps_malloc(model_size);
  if (!model_data) {
    Serial.println("Model allocation failed");
    f.close();
    return false;
  }

  f.read(model_data, model_size);
  f.close();
  return true;
}

//=============================================================================
// AUDIO
//=============================================================================

bool initMicrophone() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false
  };

  i2s_pin_config_t pins = {
    .bck_io_num = MIC_BCK,
    .ws_io_num = MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_DIN
  };

  return (i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL) == ESP_OK &&
          i2s_set_pin(I2S_NUM_0, &pins) == ESP_OK);
}

bool initFrontend() {
  FrontendConfig frontend_config;
  frontend_config.window.size_ms = FEATURE_WINDOW_MS;
  frontend_config.window.step_size_ms = FEATURE_STRIDE_MS;
  frontend_config.filterbank.num_channels = FEATURE_BINS;
  frontend_config.filterbank.lower_band_limit = 125.0f;
  frontend_config.filterbank.upper_band_limit = 7500.0f;
  frontend_config.noise_reduction.smoothing_bits = 10;
  frontend_config.noise_reduction.even_smoothing = 0.025f;
  frontend_config.noise_reduction.odd_smoothing = 0.06f;
  frontend_config.noise_reduction.min_signal_remaining = 0.05f;
  frontend_config.pcan_gain_control.enable_pcan = 1;
  frontend_config.pcan_gain_control.strength = 0.95f;
  frontend_config.pcan_gain_control.offset = 80.0f;
  frontend_config.pcan_gain_control.gain_bits = 21;
  frontend_config.log_scale.enable_log = 1;
  frontend_config.log_scale.scale_shift = 6;

  return FrontendPopulateState(&frontend_config, &frontend_state, SAMPLE_RATE);
}

bool initSD() {
  SPI.begin(39, 40, 38, SD_CS);
  return SD.begin(SD_CS);
}

bool captureAudio() {
  int space_available = AUDIO_BUFFER_SIZE - audio_buffer_pos;
  if (space_available < 256) {
    return true;
  }

  int samples_to_read = min(256, space_available);
  size_t bytes_read;
  if (i2s_read(I2S_NUM_0, i2s_buffer, samples_to_read * 4, &bytes_read, 10) != ESP_OK) {
    return false;
  }

  int samples = bytes_read / 4;

  static int audio_debug = 0;
  int32_t max_sample = 0;

  for (int i = 0; i < samples; i++) {
    int32_t sample = i2s_buffer[i] >> 14;
    audio_buffer[audio_buffer_pos++] = constrain(sample, -32768, 32767);
    if (abs(sample) > max_sample) max_sample = abs(sample);
  }

#if DEBUG_FEATURES
  if (++audio_debug >= 300) {
    audio_debug = 0;
    Serial.printf("Audio: amp=%ld, buf=%d\n", max_sample, audio_buffer_pos);
  }
#endif

  return true;
}

//=============================================================================
// INFERENCE
//=============================================================================

float runInference() {
  int total_features = FEATURE_FRAMES * FEATURE_BINS;

  static bool printed_params = false;
  if (!printed_params) {
    printed_params = true;
    Serial.printf("\n=== INFERENCE DEBUG ===\n");
    Serial.printf("Input type=%d, scale=%.6f, zp=%d\n",
      input_tensor->type, input_tensor->params.scale, input_tensor->params.zero_point);
    Serial.printf("Output type=%d, scale=%.6f, zp=%d\n",
      output_tensor->type, output_tensor->params.scale, output_tensor->params.zero_point);
    Serial.printf("Input bytes=%d, Output bytes=%d\n",
      input_tensor->bytes, output_tensor->bytes);
  }

  if (input_tensor->type == kTfLiteInt8) {
    int8_t* input_data = input_tensor->data.int8;
    for (int i = 0; i < total_features; i++) {
      int32_t scaled = ((int32_t)feature_buffer[i] * 256 + 333) / 666 - 128;
      input_data[i] = constrain(scaled, -128, 127);
    }

    static int input_debug = 0;
    if (++input_debug == 50) {
      Serial.printf("Input[0-5]: %d,%d,%d,%d,%d,%d\n",
        input_data[0], input_data[1], input_data[2],
        input_data[3], input_data[4], input_data[5]);
    }
  } else {
    float* input_data = input_tensor->data.f;
    for (int i = 0; i < total_features; i++) {
      input_data[i] = feature_buffer[i] / 25.6f;
    }
  }

  if (interpreter->Invoke() != kTfLiteOk) {
    Serial.println("Invoke failed");
    return 0.0f;
  }

  float probability;
  static int out_debug = 0;

  if (output_tensor->type == kTfLiteUInt8) {
    uint8_t output_value = output_tensor->data.uint8[0];
    probability = (output_value - output_tensor->params.zero_point) *
                  output_tensor->params.scale;
    if (++out_debug == 50) {
      Serial.printf("Raw output uint8=%u, prob=%.4f\n", output_value, probability);
    }
  } else if (output_tensor->type == kTfLiteInt8) {
    int8_t output_value = output_tensor->data.int8[0];
    probability = (output_value - output_tensor->params.zero_point) *
                  output_tensor->params.scale;
    if (++out_debug == 50) {
      Serial.printf("Raw output int8=%d, prob=%.4f\n", output_value, probability);
    }
  } else {
    probability = output_tensor->data.f[0];
    if (++out_debug == 50) {
      Serial.printf("Raw output float=%.4f\n", probability);
    }
  }

  return probability;
}

//=============================================================================
// DETECTION
//=============================================================================

bool detectWakeword(float probability) {
  probability_window[window_pos] = probability;
  window_pos = (window_pos + 1) % config.sliding_window_size;

  if (window_pos == 0) {
    window_filled = true;
  }

  if (!window_filled) {
    return false;
  }

  if (millis() - last_detection_ms < (unsigned long)config.cooldown_ms) {
    return false;
  }

  float avg = 0.0f;
  int high_frames = 0;
  for (int i = 0; i < config.sliding_window_size; i++) {
    avg += probability_window[i];
    if (probability_window[i] >= config.min_frame_prob) {
      high_frames++;
    }
  }
  avg /= config.sliding_window_size;

  static int log_count = 0;
  if (probability > 0.01f || ++log_count >= 100) {
    Serial.printf("prob: %.4f, avg: %.4f, high: %d/%d\n",
                  probability, avg, high_frames, config.sliding_window_size);
    log_count = 0;
  }

  if (avg >= config.probability_cutoff && high_frames >= config.min_high_frames) {
    last_detection_ms = millis();
    return true;
  }
  return false;
}

//=============================================================================
// UTILITY
//=============================================================================

void errorBlink() {
  while (1) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(200);
  }
}
