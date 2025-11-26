// yamnet_audio_embedding.ino - YAMNet-1024 Audio Embedding on ESP32-S3
// Records 3-5 seconds of audio, extracts 1024-D embeddings, saves to SD card
// Uses dual-core optimization and SD card model streaming

#include <SD.h>
#include <SPI.h>
#include "audio_recorder.h"
#include "mel_spectrogram.h"
#include "yamnet_inference.h"
#include "embedding_writer.h"

// SD card SPI pins (shared with LCD)
#define SD_CS   41
#define SD_MOSI 38
#define SD_MISO 40
#define SD_SCK  39

// Model file on SD card
const char* MODEL_PATH = "/yamnet.tflite";
const char* OUTPUT_PATH = "/embedding.json";

// Audio configuration
const int RECORD_SECONDS = 3;  // 3-5 seconds configurable
const int SAMPLE_RATE = 16000;
const int TOTAL_SAMPLES = RECORD_SECONDS * SAMPLE_RATE;

// Global instances
AudioRecorder audio_recorder;
MelSpectrogram mel_processor;
YamNetInference yamnet;
EmbeddingWriter writer;

// Buffers (allocated in PSRAM)
int16_t* audio_buffer = nullptr;
float* mel_features = nullptr;
float* embeddings = nullptr;

bool system_ready = false;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n========================================");
    Serial.println("YAMNet-1024 Audio Embedding");
    Serial.println("ESP32-S3 Dual-Core Optimized");
    Serial.println("========================================\n");

    // Print memory info
    Serial.printf("Total heap: %d bytes\n", ESP.getHeapSize());
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Total PSRAM: %d bytes\n", ESP.getPsramSize());
    Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
    Serial.println();

    // Initialize SD card
    Serial.print("Mounting SD card... ");
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS)) {
        Serial.println("FAILED");
        error_halt();
    }
    Serial.println("OK");

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %llu MB\n\n", cardSize);

    // Allocate buffers in PSRAM
    Serial.print("Allocating audio buffer... ");
    audio_buffer = (int16_t*)ps_malloc(TOTAL_SAMPLES * sizeof(int16_t));
    if (!audio_buffer) {
        Serial.println("FAILED");
        error_halt();
    }
    Serial.println("OK");

    Serial.print("Allocating mel-spectrogram buffer... ");
    mel_features = (float*)ps_malloc(MEL_BINS * MEL_FRAMES * sizeof(float));
    if (!mel_features) {
        Serial.println("FAILED");
        error_halt();
    }
    Serial.println("OK");

    Serial.print("Allocating embeddings buffer... ");
    embeddings = (float*)ps_malloc(EMBEDDING_DIM * sizeof(float));
    if (!embeddings) {
        Serial.println("FAILED");
        error_halt();
    }
    Serial.println("OK\n");

    // Initialize audio recorder
    Serial.print("Initializing I2S microphone... ");
    if (!audio_recorder.begin(SAMPLE_RATE)) {
        Serial.println("FAILED");
        error_halt();
    }
    Serial.println("OK");

    // Initialize mel-spectrogram processor
    Serial.print("Initializing mel-spectrogram processor... ");
    if (!mel_processor.begin(SAMPLE_RATE)) {
        Serial.println("FAILED");
        error_halt();
    }
    Serial.println("OK");

    // Load YAMNet model from SD card
    Serial.println("Loading YAMNet-1024 model from SD...");
    if (!yamnet.begin(MODEL_PATH)) {
        Serial.println("FAILED");
        error_halt();
    }
    Serial.println("OK");

    Serial.printf("\nMemory after initialization:\n");
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
    Serial.println();

    system_ready = true;

    Serial.println("========================================");
    Serial.println("READY! System will auto-record on boot");
    Serial.println("========================================\n");
}

void loop() {
    if (!system_ready) {
        delay(1000);
        return;
    }

    // Run once on boot
    Serial.printf("Recording %d seconds of audio...\n", RECORD_SECONDS);
    unsigned long start_time = millis();

    if (!audio_recorder.record(audio_buffer, TOTAL_SAMPLES)) {
        Serial.println("ERROR: Recording failed!");
        error_halt();
    }

    unsigned long record_time = millis() - start_time;
    Serial.printf("Recording complete: %lu ms\n\n", record_time);

    // Generate mel-spectrogram
    Serial.println("Generating mel-spectrogram (64x96)...");
    start_time = millis();

    if (!mel_processor.compute(audio_buffer, TOTAL_SAMPLES, mel_features)) {
        Serial.println("ERROR: Mel-spectrogram failed!");
        error_halt();
    }

    unsigned long mel_time = millis() - start_time;
    Serial.printf("Mel-spectrogram complete: %lu ms\n\n", mel_time);

    // Run YAMNet inference (dual-core optimized)
    Serial.println("Running YAMNet-1024 inference...");
    Serial.println("(Using dual-core optimization)");
    start_time = millis();

    if (!yamnet.infer(mel_features, embeddings)) {
        Serial.println("ERROR: Inference failed!");
        error_halt();
    }

    unsigned long infer_time = millis() - start_time;
    Serial.printf("Inference complete: %lu ms\n\n", infer_time);

    // Print first few embedding values
    Serial.println("Embeddings (first 10 values):");
    for (int i = 0; i < 10; i++) {
        Serial.printf("  [%d]: %.6f\n", i, embeddings[i]);
    }
    Serial.println();

    // Save to SD card as JSON
    Serial.printf("Writing embeddings to %s...\n", OUTPUT_PATH);
    start_time = millis();

    if (!writer.writeJSON(OUTPUT_PATH, embeddings, EMBEDDING_DIM)) {
        Serial.println("ERROR: Write failed!");
        error_halt();
    }

    unsigned long write_time = millis() - start_time;
    Serial.printf("Write complete: %lu ms\n\n", write_time);

    // Print performance summary
    Serial.println("========================================");
    Serial.println("PERFORMANCE SUMMARY");
    Serial.println("========================================");
    Serial.printf("Recording:       %lu ms\n", record_time);
    Serial.printf("Mel-spectrogram: %lu ms\n", mel_time);
    Serial.printf("YAMNet inference: %lu ms\n", infer_time);
    Serial.printf("JSON write:      %lu ms\n", write_time);
    Serial.printf("TOTAL:           %lu ms\n", record_time + mel_time + infer_time + write_time);
    Serial.println("========================================\n");

    Serial.println("SUCCESS! Embeddings saved to SD card.");
    Serial.println("System halting (power cycle to run again).\n");

    // Halt - run once per boot
    while(1) {
        delay(1000);
    }
}

void error_halt() {
    Serial.println("\nSYSTEM HALTED DUE TO ERROR");
    Serial.println("Check wiring and SD card contents");
    while(1) {
        delay(1000);
    }
}
