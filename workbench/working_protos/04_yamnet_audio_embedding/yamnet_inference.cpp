// yamnet_inference.cpp - YAMNet-1024 inference implementation

#include "yamnet_inference.h"

YamNetInference::YamNetInference()
    : initialized_(false), model_data_(nullptr), model_size_(0),
      model_(nullptr), interpreter_(nullptr), resolver_(nullptr),
      tensor_arena_(nullptr), input_tensor_(nullptr), output_tensor_(nullptr),
      inference_task_handle_(nullptr), inference_complete_(nullptr) {
}

YamNetInference::~YamNetInference() {
    end();
}

bool YamNetInference::begin(const char* model_path) {
    // Load model from SD card
    if (!loadModelFromSD(model_path)) {
        Serial.println("ERROR: Failed to load model from SD");
        return false;
    }

    Serial.printf("Model loaded: %u bytes\n", model_size_);

    // Initialize TensorFlow Lite interpreter
    if (!initInterpreter()) {
        Serial.println("ERROR: Failed to initialize TFLite interpreter");
        return false;
    }

    Serial.println("TFLite interpreter initialized");

    // Create semaphore for dual-core sync
    inference_complete_ = xSemaphoreCreateBinary();
    if (!inference_complete_) {
        Serial.println("ERROR: Failed to create semaphore");
        return false;
    }

    initialized_ = true;
    return true;
}

bool YamNetInference::loadModelFromSD(const char* model_path) {
    // Open model file
    File model_file = SD.open(model_path, FILE_READ);
    if (!model_file) {
        Serial.printf("ERROR: Cannot open %s\n", model_path);
        return false;
    }

    model_size_ = model_file.size();
    Serial.printf("Model file size: %u bytes\n", model_size_);

    // Allocate in PSRAM
    model_data_ = (uint8_t*)ps_malloc(model_size_);
    if (!model_data_) {
        Serial.println("ERROR: Failed to allocate model buffer");
        model_file.close();
        return false;
    }

    // Read model into PSRAM
    size_t bytes_read = model_file.read(model_data_, model_size_);
    model_file.close();

    if (bytes_read != model_size_) {
        Serial.printf("ERROR: Read %u bytes, expected %u\n", bytes_read, model_size_);
        free(model_data_);
        model_data_ = nullptr;
        return false;
    }

    return true;
}

bool YamNetInference::initInterpreter() {
    // Get model from buffer
    model_ = tflite::GetModel(model_data_);
    if (model_->version() != TFLITE_SCHEMA_VERSION) {
        Serial.printf("ERROR: Model schema version %d != %d\n",
                      model_->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    // Allocate tensor arena in PSRAM
    tensor_arena_ = (uint8_t*)ps_malloc(TENSOR_ARENA_SIZE);
    if (!tensor_arena_) {
        Serial.println("ERROR: Failed to allocate tensor arena");
        return false;
    }

    Serial.printf("Tensor arena: %d KB\n", TENSOR_ARENA_SIZE / 1024);

    // Create op resolver and add required ops
    resolver_ = new tflite::MicroMutableOpResolver<10>();
    if (!resolver_) {
        return false;
    }

    // Add ops used by YAMNet (adjust as needed based on model)
    resolver_->AddConv2D();
    resolver_->AddDepthwiseConv2D();
    resolver_->AddReshape();
    resolver_->AddSoftmax();
    resolver_->AddFullyConnected();
    resolver_->AddMean();
    resolver_->AddQuantize();
    resolver_->AddDequantize();

    // Create interpreter
    interpreter_ = new tflite::MicroInterpreter(
        model_, *resolver_, tensor_arena_, TENSOR_ARENA_SIZE);

    if (!interpreter_) {
        return false;
    }

    // Allocate tensors
    TfLiteStatus allocate_status = interpreter_->AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        Serial.println("ERROR: AllocateTensors() failed");
        return false;
    }

    // Get input and output tensors
    input_tensor_ = interpreter_->input(0);
    output_tensor_ = interpreter_->output(0);

    if (!input_tensor_ || !output_tensor_) {
        Serial.println("ERROR: Failed to get input/output tensors");
        return false;
    }

    // Verify input shape
    Serial.printf("Input tensor: dims=%d, shape=[", input_tensor_->dims->size);
    for (int i = 0; i < input_tensor_->dims->size; i++) {
        Serial.printf("%d", input_tensor_->dims->data[i]);
        if (i < input_tensor_->dims->size - 1) Serial.print(", ");
    }
    Serial.println("]");

    // Verify output shape
    Serial.printf("Output tensor: dims=%d, shape=[", output_tensor_->dims->size);
    for (int i = 0; i < output_tensor_->dims->size; i++) {
        Serial.printf("%d", output_tensor_->dims->data[i]);
        if (i < output_tensor_->dims->size - 1) Serial.print(", ");
    }
    Serial.println("]");

    return true;
}

bool YamNetInference::infer(float* mel_features, float* embeddings) {
    if (!initialized_) {
        return false;
    }

    // Copy mel-spectrogram to input tensor
    // Expected shape: [1, 96, 64, 1] or [1, 64, 96, 1] depending on model
    float* input_data = input_tensor_->data.f;

    // Copy features (assuming row-major: frames × bins)
    for (int frame = 0; frame < MEL_FRAMES; frame++) {
        for (int bin = 0; bin < MEL_BINS; bin++) {
            // Transpose if needed: (frame, bin) -> input layout
            input_data[frame * MEL_BINS + bin] = mel_features[frame * MEL_BINS + bin];
        }
    }

    // Run inference on Core 1 (dual-core optimization)
    task_params_.instance = this;
    task_params_.mel_features = mel_features;
    task_params_.success = false;

    // Create inference task on Core 1
    xTaskCreatePinnedToCore(
        inferenceTask,
        "yamnet_infer",
        8192,  // Stack size
        &task_params_,
        1,     // Priority
        &inference_task_handle_,
        1      // Core 1 (main sketch runs on Core 0)
    );

    // Wait for inference to complete
    xSemaphoreTake(inference_complete_, portMAX_DELAY);

    if (!task_params_.success) {
        return false;
    }

    // Extract embeddings from output tensor
    // YAMNet-1024: output is the embedding layer (before classification)
    // Assuming output tensor contains embeddings directly
    float* output_data = output_tensor_->data.f;

    // Copy embeddings
    int output_size = output_tensor_->dims->data[output_tensor_->dims->size - 1];
    int embedding_count = (output_size < EMBEDDING_DIM) ? output_size : EMBEDDING_DIM;

    for (int i = 0; i < embedding_count; i++) {
        embeddings[i] = output_data[i];
    }

    // Fill remaining with zeros if output < EMBEDDING_DIM
    for (int i = embedding_count; i < EMBEDDING_DIM; i++) {
        embeddings[i] = 0.0f;
    }

    return true;
}

void YamNetInference::inferenceTask(void* params) {
    InferenceTaskParams* task_params = (InferenceTaskParams*)params;
    YamNetInference* instance = task_params->instance;

    // Run TFLite inference
    TfLiteStatus invoke_status = instance->interpreter_->Invoke();

    if (invoke_status == kTfLiteOk) {
        task_params->success = true;
    } else {
        Serial.println("ERROR: Invoke() failed");
        task_params->success = false;
    }

    // Signal completion
    xSemaphoreGive(instance->inference_complete_);

    // Delete task
    vTaskDelete(NULL);
}

void YamNetInference::end() {
    if (interpreter_) {
        delete interpreter_;
        interpreter_ = nullptr;
    }

    if (resolver_) {
        delete resolver_;
        resolver_ = nullptr;
    }

    if (tensor_arena_) {
        free(tensor_arena_);
        tensor_arena_ = nullptr;
    }

    if (model_data_) {
        free(model_data_);
        model_data_ = nullptr;
    }

    if (inference_complete_) {
        vSemaphoreDelete(inference_complete_);
        inference_complete_ = nullptr;
    }

    initialized_ = false;
}
