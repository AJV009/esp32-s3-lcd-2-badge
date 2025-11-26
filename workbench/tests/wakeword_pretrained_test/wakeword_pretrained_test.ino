/*
 * microWakeWord Pretrained Test
 *
 * Tests wake word detection using microWakeWord TFLite model.
 * Uses EdgeNeuron library with microfrontend for audio preprocessing.
 *
 * Based on: https://github.com/kahrendt/microWakeWord
 *
 * Audio: 16kHz, 16-bit mono
 * Features: 40 bins, 30ms window, 10ms stride
 * Detection: Sliding window probability averaging
 */

//=============================================================================
// MODEL SELECTION - Uncomment ONE of these to select the wake word model
//=============================================================================
// #define MODEL_HEY_JARVIS    // Pretrained "Hey Jarvis" model
#define MODEL_HEY_DAISY     // Custom trained "Hey Daisy" model

//=============================================================================
// Model configuration (auto-set based on selection above)
//=============================================================================
#if defined(MODEL_HEY_JARVIS)
  #define MODEL_PATH      "/models/hey_jarvis.tflite"
  #define WAKE_WORD_NAME  "Hey Jarvis"
#elif defined(MODEL_HEY_DAISY)
  #define MODEL_PATH      "/models/hey_daisy.tflite"
  #define WAKE_WORD_NAME  "Hey Daisy"
#else
  #error "No model selected! Uncomment MODEL_HEY_JARVIS or MODEL_HEY_DAISY above."
#endif

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

// Pins (INMP441 microphone)
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
#define FEATURE_FRAMES      3       // Model expects 3 consecutive frames
#define FEATURE_STRIDE_SAMPLES (SAMPLE_RATE * FEATURE_STRIDE_MS / 1000)  // 160 samples per stride

// Detection config - V1 model uses lower threshold
// V1: probability_cutoff=0.5, sliding_window_average_size=10
// V2: probability_cutoff=0.97, sliding_window_size=5
#define PROBABILITY_CUTOFF  0.5f
#define SLIDING_WINDOW_SIZE 10
#define DEBUG_FEATURES      1      // Set to 1 to debug feature values

// Memory config - v1 models need larger arena (~90KB), v2 needs ~23KB + var arena
#define TENSOR_ARENA_SIZE   100000
#define VAR_ARENA_SIZE      10000
#define MAX_RESOURCE_VARS   20

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

// Audio buffers - need enough for frontend window (480 samples = 30ms) plus extra
#define AUDIO_BUFFER_SIZE 1024
static int32_t i2s_buffer[512];
static int16_t audio_buffer[AUDIO_BUFFER_SIZE];
static int audio_buffer_pos = 0;

// Feature buffer for 3 frames
static uint16_t feature_buffer[FEATURE_FRAMES * FEATURE_BINS];
static int feature_frame_count = 0;

// Sliding window for probability averaging
static float probability_window[SLIDING_WINDOW_SIZE];
static int window_pos = 0;
static bool window_filled = false;

// Forward declarations
bool initSD();
bool loadModelFromSD(const char* path);
bool initMicrophone();
bool initFrontend();
bool initInterpreter();
bool captureAudio();
float runInference();
bool detectWakeword(float probability);
void errorBlink();

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("\n=== microWakeWord Test ===");
  Serial.printf("Wake word: \"%s\"\n", WAKE_WORD_NAME);
  Serial.printf("Model: %s\n\n", MODEL_PATH);

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

  // Load model from SD
  if (!loadModelFromSD(MODEL_PATH)) {
    Serial.println("ERROR: Model load failed");
    Serial.printf("Copy model file to SD:%s\n", MODEL_PATH);
    errorBlink();
  }
  Serial.printf("Model loaded: %u bytes\n", model_size);

  // Initialize TFLite interpreter with resource variables
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
  for (int i = 0; i < SLIDING_WINDOW_SIZE; i++) {
    probability_window[i] = 0.0f;
  }

  Serial.printf("\nListening for \"%s\"...\n", WAKE_WORD_NAME);
  Serial.println("================================\n");
}

void loop() {
  // Capture audio
  if (!captureAudio()) {
    return;
  }

  // Keep processing audio through frontend until it stops producing features
  while (audio_buffer_pos >= FEATURE_STRIDE_SAMPLES) {
    // Feed ALL available audio to frontend (it will consume what it needs)
    size_t num_samples_read;
    FrontendOutput frontend_output = FrontendProcessSamples(
        &frontend_state, audio_buffer, audio_buffer_pos, &num_samples_read);

    // If no samples consumed, we need more audio
    if (num_samples_read == 0) {
      break;
    }

    // Shift audio buffer by consumed samples
    if (num_samples_read < audio_buffer_pos) {
      memmove(audio_buffer, audio_buffer + num_samples_read,
              (audio_buffer_pos - num_samples_read) * sizeof(int16_t));
    }
    audio_buffer_pos -= num_samples_read;

    // If we got features, add to feature buffer
    if (frontend_output.size == FEATURE_BINS) {
      // Shift existing frames left and add new one at end
      if (feature_frame_count >= FEATURE_FRAMES) {
        memmove(feature_buffer, feature_buffer + FEATURE_BINS,
                (FEATURE_FRAMES - 1) * FEATURE_BINS * sizeof(uint16_t));
        feature_frame_count = FEATURE_FRAMES - 1;
      }

      // Copy new frame
      memcpy(feature_buffer + feature_frame_count * FEATURE_BINS,
             frontend_output.values, FEATURE_BINS * sizeof(uint16_t));
      feature_frame_count++;

      // Run inference when we have 3 frames
      if (feature_frame_count >= FEATURE_FRAMES) {
#if DEBUG_FEATURES
        static int debug_count = 0;
        if (++debug_count >= 30) {
          debug_count = 0;
          // Show raw feature values and energy
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
          Serial.printf("\n>>> %s DETECTED! <<<\n\n", WAKE_WORD_NAME);
          digitalWrite(LED_PIN, HIGH);
          delay(500);
          digitalWrite(LED_PIN, LOW);

          for (int i = 0; i < SLIDING_WINDOW_SIZE; i++) {
            probability_window[i] = 0.0f;
          }
          window_filled = false;
        }
      }
    }
  }
}

bool initInterpreter() {
  // Get model
  const tflite::Model* model = tflite::GetModel(model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.printf("Model version mismatch: %d vs %d\n",
                  model->version(), TFLITE_SCHEMA_VERSION);
    return false;
  }

  // Create allocator for resource variables (streaming model state)
  var_allocator = tflite::MicroAllocator::Create(var_arena, VAR_ARENA_SIZE);
  if (!var_allocator) {
    Serial.println("Failed to create var allocator");
    return false;
  }

  // Create resource variables container
  resource_vars = tflite::MicroResourceVariables::Create(var_allocator, MAX_RESOURCE_VARS);
  if (!resource_vars) {
    Serial.println("Failed to create resource variables");
    return false;
  }

  // Create interpreter WITH resource variables support
  static tflite::MicroInterpreter static_interpreter(
      model, resolver, tensor_arena, TENSOR_ARENA_SIZE, resource_vars);
  interpreter = &static_interpreter;

  // Allocate tensors
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    Serial.println("AllocateTensors failed");
    return false;
  }

  // Get input/output tensors
  input_tensor = interpreter->input(0);
  output_tensor = interpreter->output(0);

  if (!input_tensor || !output_tensor) {
    Serial.println("Failed to get tensors");
    return false;
  }

  return true;
}

bool initMicrophone() {
  i2s_config_t config = {
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

  return (i2s_driver_install(I2S_NUM_0, &config, 0, NULL) == ESP_OK &&
          i2s_set_pin(I2S_NUM_0, &pins) == ESP_OK);
}

bool initFrontend() {
  FrontendConfig config;
  config.window.size_ms = FEATURE_WINDOW_MS;
  config.window.step_size_ms = FEATURE_STRIDE_MS;
  config.filterbank.num_channels = FEATURE_BINS;
  config.filterbank.lower_band_limit = 125.0f;
  config.filterbank.upper_band_limit = 7500.0f;
  config.noise_reduction.smoothing_bits = 10;
  config.noise_reduction.even_smoothing = 0.025f;
  config.noise_reduction.odd_smoothing = 0.06f;
  config.noise_reduction.min_signal_remaining = 0.05f;
  config.pcan_gain_control.enable_pcan = 1;
  config.pcan_gain_control.strength = 0.95f;
  config.pcan_gain_control.offset = 80.0f;
  config.pcan_gain_control.gain_bits = 21;
  config.log_scale.enable_log = 1;
  config.log_scale.scale_shift = 6;

  return FrontendPopulateState(&config, &frontend_state, SAMPLE_RATE);
}

bool initSD() {
  SPI.begin(39, 40, 38, SD_CS);  // SCK, MISO, MOSI, CS
  return SD.begin(SD_CS);
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

bool captureAudio() {
  // Only read if we have space in buffer
  int space_available = AUDIO_BUFFER_SIZE - audio_buffer_pos;
  if (space_available < 256) {
    return true;  // Buffer has data, let it be processed first
  }

  // Read smaller chunks more frequently for smoother audio
  int samples_to_read = min(256, space_available);
  size_t bytes_read;
  if (i2s_read(I2S_NUM_0, i2s_buffer, samples_to_read * 4, &bytes_read, 10) != ESP_OK) {
    return false;
  }

  int samples = bytes_read / 4;

  // Track audio levels for debug
  static int audio_debug = 0;
  int32_t max_sample = 0;

  for (int i = 0; i < samples; i++) {
    // Convert 32-bit I2S to 16-bit
    // INMP441 outputs 24-bit in MSB of 32-bit word, shift right to get 16-bit
    int32_t sample = i2s_buffer[i] >> 14;  // >>14 for moderate gain
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

float runInference() {
  // Copy 3 frames of features to input tensor
  int total_features = FEATURE_FRAMES * FEATURE_BINS;  // 3 * 40 = 120

  // Debug: print quant params once
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

    // Use ESPHome's proven formula directly
    for (int i = 0; i < total_features; i++) {
      int32_t scaled = ((int32_t)feature_buffer[i] * 256 + 333) / 666 - 128;
      input_data[i] = constrain(scaled, -128, 127);
    }

    // Debug: show first few input values once
    static int input_debug = 0;
    if (++input_debug == 50) {
      Serial.printf("Input[0-5]: %d,%d,%d,%d,%d,%d\n",
        input_data[0], input_data[1], input_data[2],
        input_data[3], input_data[4], input_data[5]);
    }
  } else {
    // Float model
    float* input_data = input_tensor->data.f;
    for (int i = 0; i < total_features; i++) {
      input_data[i] = feature_buffer[i] / 25.6f;
    }
  }

  // Run inference
  if (interpreter->Invoke() != kTfLiteOk) {
    Serial.println("Invoke failed");
    return 0.0f;
  }

  // Get output probability
  float probability;

  // Debug: show raw output once
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

bool detectWakeword(float probability) {
  // Add to sliding window
  probability_window[window_pos] = probability;
  window_pos = (window_pos + 1) % SLIDING_WINDOW_SIZE;

  if (window_pos == 0) {
    window_filled = true;
  }

  // Don't trigger until window is filled
  if (!window_filled) {
    return false;
  }

  // Calculate average probability
  float avg = 0.0f;
  for (int i = 0; i < SLIDING_WINDOW_SIZE; i++) {
    avg += probability_window[i];
  }
  avg /= SLIDING_WINDOW_SIZE;

  // Log probability (always show for debugging)
  static int log_count = 0;
  if (probability > 0.01f || ++log_count >= 100) {
    Serial.printf("prob: %.4f, avg: %.4f\n", probability, avg);
    log_count = 0;
  }

  return avg >= PROBABILITY_CUTOFF;
}

void errorBlink() {
  while (1) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(200);
  }
}
