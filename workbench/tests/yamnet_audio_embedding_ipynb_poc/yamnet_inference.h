// yamnet_inference.h - YAMNet-1024 TensorFlow Lite inference
// Loads model from SD card and runs inference with dual-core optimization

#ifndef YAMNET_INFERENCE_H
#define YAMNET_INFERENCE_H

#include <Arduino.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// TensorFlow Lite for Microcontrollers
#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

// YAMNet-1024 specifications
#define EMBEDDING_DIM 1024    // YAMNet-1024 embedding dimension
#define MEL_BINS 64           // Input: 64 mel bins
#define MEL_FRAMES 96         // Input: 96 frames

// TensorFlow Lite memory
#define TENSOR_ARENA_SIZE (400 * 1024)  // 400KB tensor arena

class YamNetInference {
public:
    YamNetInference();
    ~YamNetInference();

    // Load model from SD card and initialize TFLite
    bool begin(const char* model_path);

    // Run inference on mel-spectrogram features
    // Input: mel_features[MEL_BINS * MEL_FRAMES]
    // Output: embeddings[EMBEDDING_DIM]
    bool infer(float* mel_features, float* embeddings);

    // Cleanup
    void end();

private:
    // Load model file from SD card to PSRAM
    bool loadModelFromSD(const char* model_path);

    // Initialize TensorFlow Lite interpreter
    bool initInterpreter();

    bool initialized_;

    // Model data (in PSRAM)
    uint8_t* model_data_;
    size_t model_size_;

    // TensorFlow Lite components
    const tflite::Model* model_;
    tflite::MicroInterpreter* interpreter_;
    tflite::MicroMutableOpResolver<10>* resolver_;  // Adjust op count as needed
    uint8_t* tensor_arena_;

    // Input/output tensors
    TfLiteTensor* input_tensor_;
    TfLiteTensor* output_tensor_;

    // Dual-core task handle
    TaskHandle_t inference_task_handle_;
    SemaphoreHandle_t inference_complete_;

    // Task parameters
    struct InferenceTaskParams {
        YamNetInference* instance;
        float* mel_features;
        bool success;
    };
    InferenceTaskParams task_params_;

    // Dual-core inference task
    static void inferenceTask(void* params);
};

#endif // YAMNET_INFERENCE_H
