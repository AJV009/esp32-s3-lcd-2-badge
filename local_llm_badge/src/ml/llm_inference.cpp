/*******************************************************************************
 * OAISYS Badge - LLM Inference Wrapper Implementation
 *
 * Thin wrapper that delegates to the optimized llm_core/tokenizer/sampler.
 * The core inference code is unchanged from 05_llm_finetuned.
 ******************************************************************************/

#include "llm_inference.h"
#include "../../config.h"

LLMInference::LLMInference()
    : _maxTokens(128)
    , _loaded(false)
{
    memset(&_transformer, 0, sizeof(Transformer));
    memset(&_tokenizer, 0, sizeof(Tokenizer));
    memset(&_sampler, 0, sizeof(Sampler));
}

LLMInference::~LLMInference() {
    end();
}

bool LLMInference::begin(uint8_t* pool, size_t poolSize,
                          const char* modelPath, const char* tokenizerPath) {
    if (_loaded) return true;

    Serial.printf("LLM: Loading model %s\n", modelPath);

    // Build transformer (uses its own PSRAM allocation)
    // max_seq_len = 64 to reduce KV cache memory (saves ~448KB vs 128)
    if (!build_transformer_q8(&_transformer, modelPath, 64)) {
        Serial.println("LLM: Model load failed");
        return false;
    }

    Serial.printf("LLM: Loading tokenizer %s\n", tokenizerPath);

    // Build tokenizer
    if (!build_tokenizer(&_tokenizer, tokenizerPath, _transformer.config.vocab_size)) {
        Serial.println("LLM: Tokenizer load failed");
        free_transformer(&_transformer);
        return false;
    }

    // Build sampler
    unsigned long long seed = millis();
    build_sampler(&_sampler, _transformer.config.vocab_size,
                  DEFAULT_LLM_TEMPERATURE, DEFAULT_LLM_TOPP, seed);

    _loaded = true;
    Serial.printf("LLM: Ready (dim=%d, layers=%d, vocab=%d)\n",
                  _transformer.config.dim,
                  _transformer.config.n_layers,
                  _transformer.config.vocab_size);
    return true;
}

void LLMInference::end() {
    if (!_loaded) return;

    free_sampler(&_sampler);
    free_tokenizer(&_tokenizer);
    free_transformer(&_transformer);

    _loaded = false;
    Serial.println("LLM: Unloaded");
}

void LLMInference::setTemperature(float temp) {
    _sampler.temperature = temp;
}

void LLMInference::setTopP(float topp) {
    _sampler.topp = topp;
}

String LLMInference::_wrapPrompt(const char* userInput) {
    return String("<|user|>") + userInput + "<|assistant|>";
}

bool LLMInference::_isEndToken(const char* piece) {
    return piece && strstr(piece, "<|end|>") != nullptr;
}

int LLMInference::generate(const char* prompt, TokenCallback callback, void* userData) {
    if (!_loaded) return 0;

    // Wrap prompt with Q&A tokens
    String wrapped = _wrapPrompt(prompt);

    // Allocate token buffer
    int maxPromptTokens = wrapped.length() + 10;
    int* promptTokens = (int*)malloc(maxPromptTokens * sizeof(int));
    if (!promptTokens) {
        Serial.println("LLM: Token buffer alloc failed");
        return 0;
    }

    // Encode prompt
    int numPromptTokens = 0;
    encode(&_tokenizer, (char*)wrapped.c_str(), 0, 0, promptTokens, &numPromptTokens);

    if (numPromptTokens < 1) {
        Serial.println("LLM: Encoding failed");
        free(promptTokens);
        return 0;
    }

    Serial.printf("LLM: Encoded %d prompt tokens\n", numPromptTokens);

    // Generation loop
    int token = promptTokens[0];
    int pos = 0;
    int generated = 0;
    unsigned long startTime = 0;

    int totalSteps = numPromptTokens + _maxTokens;

    while (pos < totalSteps) {
        // Forward pass
        float* logits = forward(&_transformer, token, pos);
        if (!logits) {
            Serial.println("LLM: Forward pass failed");
            break;
        }

        // Get next token
        int next;
        if (pos < numPromptTokens - 1) {
            // Still processing prompt
            next = promptTokens[pos + 1];
        } else {
            // Sample from logits
            next = sample(&_sampler, logits);
            generated++;

            // Start timer after prompt processing
            if (startTime == 0) {
                startTime = millis();
            }

            // Decode and output token
            char* piece = decode(&_tokenizer, token, next);
            if (piece && piece[0] != '\0') {
                // Check for end token
                if (_isEndToken(piece)) {
                    break;
                }

                // Filter control characters
                bool printable = true;
                if (piece[1] == '\0') {
                    unsigned char byte_val = piece[0];
                    printable = isprint(byte_val) || isspace(byte_val);
                }

                if (printable && callback) {
                    callback(piece, userData);
                }
            }
        }

        token = next;
        pos++;

        // Check for max tokens
        if (generated >= _maxTokens) break;
    }

    // Print performance stats
    if (generated > 0 && startTime > 0) {
        unsigned long elapsed = millis() - startTime;
        float tokensPerSec = (float)generated / (elapsed / 1000.0f);
        Serial.printf("LLM: Generated %d tokens (%.1f tok/s)\n", generated, tokensPerSec);
    }

    free(promptTokens);
    return generated;
}

int LLMInference::getVocabSize() const {
    return _loaded ? _transformer.config.vocab_size : 0;
}

int LLMInference::getDimension() const {
    return _loaded ? _transformer.config.dim : 0;
}

int LLMInference::getNumLayers() const {
    return _loaded ? _transformer.config.n_layers : 0;
}

int LLMInference::getSeqLen() const {
    return _loaded ? _transformer.config.seq_len : 0;
}
