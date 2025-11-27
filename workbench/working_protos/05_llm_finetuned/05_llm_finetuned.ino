// 05_llm_finetuned.ino - Fine-tuned Q8_0 LLM Inference
// Supports multiple model sizes (1M-6M) with switchable config
// SD card loading, JSON config, auto-wrap prompts with Q&A tokens

#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include "llm_core.h"
#include "tokenizer.h"
#include "sampler.h"

// SD card SPI pins (shared with LCD)
#define SD_CS   41
#define SD_MOSI 38
#define SD_MISO 40
#define SD_SCK  39

// Config file path
#define CONFIG_PATH "/models/config.json"

// Default values if config missing
#define DEFAULT_MODEL      "/models/ajv009_3M_1024TK_q80.bin"
#define DEFAULT_TOKENIZER  "/models/ajv009_1024TK.bin"
#define DEFAULT_TEMPERATURE 0.8f
#define DEFAULT_TOPP       0.9f
#define DEFAULT_MAX_TOKENS 128

// Runtime config structure
struct LLMConfig {
    char model_path[64];
    char tokenizer_path[64];
    float temperature;
    float topp;
    int max_tokens;
    int max_seq_len;  // Override model's seq_len to reduce KV cache (0 = use model default)
};

// Global instances
LLMConfig config;
Transformer transformer;
Tokenizer tokenizer;
Sampler sampler;
bool model_loaded = false;

// ============================================================================
// Config Loading
// ============================================================================

bool loadConfig(const char* path) {
    File f = SD.open(path);
    if (!f) {
        Serial.printf("Config not found: %s\n", path);
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("JSON parse error: %s\n", err.c_str());
        return false;
    }

    // Read with defaults
    strlcpy(config.model_path, doc["model"] | DEFAULT_MODEL, sizeof(config.model_path));
    strlcpy(config.tokenizer_path, doc["tokenizer"] | DEFAULT_TOKENIZER, sizeof(config.tokenizer_path));
    config.temperature = doc["temperature"] | DEFAULT_TEMPERATURE;
    config.topp = doc["topp"] | DEFAULT_TOPP;
    config.max_tokens = doc["max_tokens"] | DEFAULT_MAX_TOKENS;
    config.max_seq_len = doc["max_seq_len"] | 0;  // 0 = use model default

    Serial.println("Config loaded:");
    Serial.printf("  model: %s\n", config.model_path);
    Serial.printf("  tokenizer: %s\n", config.tokenizer_path);
    Serial.printf("  temp=%.2f, topp=%.2f, max_tokens=%d, max_seq_len=%d\n",
                  config.temperature, config.topp, config.max_tokens, config.max_seq_len);

    return true;
}

void setDefaultConfig() {
    strlcpy(config.model_path, DEFAULT_MODEL, sizeof(config.model_path));
    strlcpy(config.tokenizer_path, DEFAULT_TOKENIZER, sizeof(config.tokenizer_path));
    config.temperature = DEFAULT_TEMPERATURE;
    config.topp = DEFAULT_TOPP;
    config.max_tokens = DEFAULT_MAX_TOKENS;
    config.max_seq_len = 0;

    Serial.println("Using default config");
}

// ============================================================================
// Prompt Wrapping
// ============================================================================

// Wrap user input with Q&A special tokens
String wrapPrompt(const char* userInput) {
    return String("<|user|>") + userInput + "<|assistant|>";
}

// Check if token piece contains end marker
bool isEndToken(const char* piece) {
    return piece && strstr(piece, "<|end|>") != nullptr;
}

// ============================================================================
// Generation
// ============================================================================

void generate(const char* prompt, int steps) {
    // Wrap prompt with special tokens
    String wrapped = wrapPrompt(prompt);
    Serial.printf("[DEBUG] Wrapped prompt: %s\n", wrapped.c_str());

    // Allocate token buffer (prompt length + some extra)
    int max_prompt_tokens = wrapped.length() + 10;
    int* prompt_tokens = (int*)malloc(max_prompt_tokens * sizeof(int));
    if (!prompt_tokens) {
        Serial.println("Token buffer allocation failed!");
        return;
    }

    // Encode wrapped prompt
    int num_prompt_tokens = 0;
    encode(&tokenizer, (char*)wrapped.c_str(), 0, 0, prompt_tokens, &num_prompt_tokens);

    if (num_prompt_tokens < 1) {
        Serial.println("Encoding failed!");
        free(prompt_tokens);
        return;
    }

    // Debug: print encoded tokens
    Serial.printf("[DEBUG] Encoded %d tokens: ", num_prompt_tokens);
    for (int i = 0; i < num_prompt_tokens && i < 20; i++) {
        Serial.printf("%d ", prompt_tokens[i]);
    }
    Serial.println();

    // Generation loop
    unsigned long start_time = 0;
    int token = prompt_tokens[0];
    int pos = 0;

    Serial.printf("[DEBUG] First token=%d, vocab_size=%d\n", token, transformer.config.vocab_size);
    int generated = 0;

    while (pos < steps) {
        // Forward pass
        float* logits = forward(&transformer, token, pos);
        if (!logits) {
            Serial.println("\nForward pass failed!");
            break;
        }

        // Get next token
        int next;
        if (pos < num_prompt_tokens - 1) {
            // Still processing prompt
            next = prompt_tokens[pos + 1];
        } else {
            // Sample from logits
            next = sample(&sampler, logits);
            generated++;
        }
        pos++;

        // Decode and print token
        char* piece = decode(&tokenizer, token, next);
        if (piece && piece[0] != '\0') {
            // Check for end token
            if (isEndToken(piece)) {
                break;
            }

            // Print token (filter control characters)
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

        // Start timer after prompt processing
        if (start_time == 0 && pos >= num_prompt_tokens) {
            start_time = millis();
        }
    }

    // Print performance stats
    if (generated > 0 && start_time > 0) {
        unsigned long elapsed = millis() - start_time;
        float tokens_per_sec = (float)generated / (elapsed / 1000.0f);
        Serial.printf("\n\n[%d tokens, %.1f tok/s]\n", generated, tokens_per_sec);
    }

    free(prompt_tokens);
}

// ============================================================================
// Setup
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n========================================");
    Serial.println("ESP32 Fine-tuned LLM (Q8_0)");
    Serial.println("========================================\n");

    // Memory info
    Serial.printf("Total PSRAM: %d bytes\n", ESP.getPsramSize());
    Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
    Serial.println();

    // Initialize SD card
    Serial.print("Mounting SD card... ");
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS)) {
        Serial.println("FAILED!");
        Serial.println("Please insert SD card with model files.");
        return;
    }
    Serial.println("OK");

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %llu MB\n\n", cardSize);

    // Load config (or use defaults)
    if (!loadConfig(CONFIG_PATH)) {
        setDefaultConfig();
    }
    Serial.println();

    // Load model (with optional seq_len override)
    Serial.printf("Loading model: %s\n", config.model_path);
    if (!build_transformer_q8(&transformer, config.model_path, config.max_seq_len)) {
        Serial.println("Model load FAILED!");
        Serial.println("Check that model file exists on SD card.");
        return;
    }
    Serial.println();

    // Load tokenizer
    Serial.printf("Loading tokenizer: %s\n", config.tokenizer_path);
    if (!build_tokenizer(&tokenizer, config.tokenizer_path, transformer.config.vocab_size)) {
        Serial.println("Tokenizer load FAILED!");
        return;
    }
    Serial.println("Tokenizer loaded\n");

    // Build sampler
    unsigned long long seed = millis();
    build_sampler(&sampler, transformer.config.vocab_size,
                  config.temperature, config.topp, seed);

    // Ready
    model_loaded = true;

    Serial.printf("Memory after load:\n");
    Serial.printf("  Free PSRAM: %d bytes\n", ESP.getFreePsram());
    Serial.printf("  Free Heap: %d bytes\n\n", ESP.getFreeHeap());

    Serial.println("========================================");
    Serial.println("Ready! Type your question and press Enter");
    Serial.println("========================================\n");
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
    if (!model_loaded) {
        delay(1000);
        return;
    }

    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();

        if (input.length() == 0) {
            return;
        }

        Serial.printf("\nQ: %s\n", input.c_str());
        Serial.print("A: ");

        generate(input.c_str(), config.max_tokens);

        Serial.println("\n----------------------------------------\n");
    }
}
