// 01_llm_inference.ino - SD Streaming LLM Inference
// 15M parameter model with full SD card streaming
// Optimized with ESP-DSP SIMD + dual-core parallelization

#include <SD.h>
#include <SPI.h>
#include "llm_core.h"
#include "tokenizer.h"
#include "sampler.h"

// SD card SPI pins (shared with LCD)
#define SD_CS   41
#define SD_MOSI 38
#define SD_MISO 40
#define SD_SCK  39

// Model files (all on SD card)
const char* MODEL_PATH = "/stories15M.bin";
const char* TOKENIZER_PATH = "/tok32000.bin";

// Generation parameters
const float TEMPERATURE = 1.0f;
const float TOPP = 0.9f;
const int MAX_TOKENS = 256;

// Global instances
Transformer transformer;
Tokenizer tokenizer;
Sampler sampler;

bool model_loaded = false;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n========================================");
    Serial.println("ESP32 LLM - 15M SD Streaming");
    Serial.println("========================================\n");

    // Print memory info
    Serial.printf("Total heap: %d bytes\n", ESP.getHeapSize());
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Total PSRAM: %d bytes\n", ESP.getPsramSize());
    Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
    Serial.println();

    // Initialize SD card (SPI mode - shared bus with LCD)
    Serial.print("Mounting SD card... ");
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS)) {
        Serial.println("FAILED");
        return;
    }
    Serial.println("OK");

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %llu MB\n", cardSize);
    Serial.println();

    // Load model from SD card (streaming mode)
    Serial.println("Loading model (streaming)...");
    if (!open_sd_model(&transformer, MODEL_PATH)) {
        Serial.println("FAILED");
        return;
    }

    Serial.printf("\nModel config:\n");
    Serial.printf("  dim: %d\n", transformer.config.dim);
    Serial.printf("  hidden_dim: %d\n", transformer.config.hidden_dim);
    Serial.printf("  n_layers: %d\n", transformer.config.n_layers);
    Serial.printf("  n_heads: %d\n", transformer.config.n_heads);
    Serial.printf("  vocab_size: %d\n", transformer.config.vocab_size);
    Serial.printf("  seq_len: %d\n", transformer.config.seq_len);
    Serial.println();

    // Load tokenizer from SD card
    Serial.println("Loading tokenizer...");
    if (!build_tokenizer(&tokenizer, TOKENIZER_PATH, transformer.config.vocab_size)) {
        Serial.println("FAILED");
        return;
    }

    // Build sampler
    unsigned long long seed = millis();
    build_sampler(&sampler, transformer.config.vocab_size, TEMPERATURE, TOPP, seed);

    Serial.printf("\nMemory after load:\n");
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
    Serial.println();

    model_loaded = true;

    Serial.println("========================================");
    Serial.println("Ready! Type prompt and press Enter");
    Serial.println("Expected: 0.5-1 tok/s (SD streaming)");
    Serial.println("========================================\n");
}

void loop() {
    if (!model_loaded) {
        delay(1000);
        return;
    }

    if (Serial.available()) {
        String prompt = Serial.readStringUntil('\n');
        prompt.trim();

        if (prompt.length() == 0) {
            return;
        }

        Serial.printf("\nPrompt: %s\n", prompt.c_str());
        Serial.println("Generating...\n");

        // Generate text
        generate((char*)prompt.c_str(), MAX_TOKENS);

        Serial.println("\n\n========================================");
        Serial.println("Enter another prompt:");
        Serial.println("========================================\n");
    }
}

// Generation function
void generate(char* prompt, int steps) {
    // Encode prompt
    int num_prompt_tokens = 0;
    int* prompt_tokens = (int*)malloc((strlen(prompt) + 3) * sizeof(int));
    encode(&tokenizer, prompt, 1, 0, prompt_tokens, &num_prompt_tokens);

    if (num_prompt_tokens < 1) {
        Serial.println("ERROR: Encoding failed");
        free(prompt_tokens);
        return;
    }

    // Generation loop
    unsigned long start_time = 0;
    int next;
    int token = prompt_tokens[0];
    int pos = 0;

    while (pos < steps) {
        // Forward pass
        v4sf* logits = forward(&transformer, token, pos);

        // Check for failure
        if (!logits) {
            Serial.println("\nERROR: Forward pass failed (SD streaming issue)");
            free(prompt_tokens);
            return;
        }

        // Get next token
        if (pos < num_prompt_tokens - 1) {
            next = prompt_tokens[pos + 1];
        } else {
            next = sample(&sampler, logits);
        }
        pos++;

        // Check for end of sequence
        if (next == 1) break;

        // Decode and print token
        char* piece = decode(&tokenizer, token, next);
        if (piece != NULL && piece[0] != '\0') {
            if (piece[1] == '\0') {
                unsigned char byte_val = piece[0];
                if (isprint(byte_val) || isspace(byte_val)) {
                    Serial.print(piece);
                }
            } else {
                Serial.print(piece);
            }
        }

        token = next;

        // Start timer after first iteration
        if (start_time == 0) {
            start_time = millis();
        }
    }

    // Calculate and print performance
    if (pos > 1 && start_time > 0) {
        unsigned long elapsed = millis() - start_time;
        float tokens_per_sec = (float)(pos - 1) / (elapsed / 1000.0f);
        Serial.printf("\n\nPerformance: %.2f tok/s (%d tokens in %lu ms)\n",
                      tokens_per_sec, pos - 1, elapsed);
    }

    free(prompt_tokens);
}
