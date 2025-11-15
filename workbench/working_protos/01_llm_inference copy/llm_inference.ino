// llm_inference.ino - Main Arduino sketch for LLM inference
// Optimized with ESP-DSP SIMD + dual-core parallelization

#include <FFat.h>
#include "llm_core.h"
#include "tokenizer.h"
#include "sampler.h"

// Global instances
Transformer transformer;
Tokenizer tokenizer;
Sampler sampler;

// Configuration
const char* MODEL_PATH = "/stories260K.bin";
const char* TOKENIZER_PATH = "/tok512.bin";
const float TEMPERATURE = 1.0f;  // 0.0 = greedy, 1.0 = original
const float TOPP = 0.9f;         // top-p nucleus sampling
const int MAX_TOKENS = 256;      // max tokens to generate

bool model_loaded = false;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n========================================");
    Serial.println("ESP32 LLM Inference - Maximum Performance");
    Serial.println("========================================\n");

    // Mount FFat filesystem
    Serial.print("Mounting FFat... ");
    if (!FFat.begin(true)) {
        Serial.println("FAILED");
        Serial.println("Run upload_to_flash.sh to upload model files");
        return;
    }
    Serial.println("OK");

    // Print memory info
    Serial.printf("Total heap: %d bytes\n", ESP.getHeapSize());
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Total PSRAM: %d bytes\n", ESP.getPsramSize());
    Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
    Serial.println();

    // Load model
    Serial.println("Loading model...");
    if (!build_transformer(&transformer, MODEL_PATH)) {
        Serial.println("Model load FAILED");
        return;
    }

    Serial.printf("Model config:\n");
    Serial.printf("  dim: %d\n", transformer.config.dim);
    Serial.printf("  hidden_dim: %d\n", transformer.config.hidden_dim);
    Serial.printf("  n_layers: %d\n", transformer.config.n_layers);
    Serial.printf("  n_heads: %d\n", transformer.config.n_heads);
    Serial.printf("  vocab_size: %d\n", transformer.config.vocab_size);
    Serial.printf("  seq_len: %d\n", transformer.config.seq_len);
    Serial.println();

    // Load tokenizer
    Serial.println("Loading tokenizer...");
    if (!build_tokenizer(&tokenizer, TOKENIZER_PATH, transformer.config.vocab_size)) {
        Serial.println("Tokenizer load FAILED");
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
    Serial.println("Example: Once upon a time");
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
