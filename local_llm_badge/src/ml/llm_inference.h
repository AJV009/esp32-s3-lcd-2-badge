/*******************************************************************************
 * OAISYS Badge - LLM Inference Wrapper
 *
 * Thin wrapper around the optimized llm_core/tokenizer/sampler from 05_llm_finetuned.
 * Provides simplified interface for badge state machine integration.
 ******************************************************************************/

#pragma once

#include <Arduino.h>
#include "llm_core.h"
#include "tokenizer.h"
#include "sampler.h"

//==============================================================================
// Token callback for streaming output
//==============================================================================
typedef void (*TokenCallback)(const char* token, void* userData);

//==============================================================================
// LLM Inference Wrapper
//==============================================================================
class LLMInference {
public:
    LLMInference();
    ~LLMInference();

    // Initialize with memory pool (pool not directly used - kept for API consistency)
    // modelPath: Path to Q8_0 model file on SD card
    // tokenizerPath: Path to tokenizer.bin on SD card
    bool begin(uint8_t* pool, size_t poolSize,
               const char* modelPath, const char* tokenizerPath);
    void end();

    // Check if model is loaded
    bool isLoaded() const { return _loaded; }

    // Configure generation parameters
    void setTemperature(float temp);
    void setTopP(float topp);
    void setMaxTokens(int max) { _maxTokens = max; }

    // Generate response from prompt
    // prompt: User's question/intent (will be wrapped with Q&A tokens)
    // callback: Called for each generated token
    // userData: Passed to callback
    // Returns: Number of tokens generated
    int generate(const char* prompt, TokenCallback callback, void* userData = nullptr);

    // Get model info
    int getVocabSize() const;
    int getDimension() const;
    int getNumLayers() const;
    int getSeqLen() const;

    // Memory requirements
    static size_t requiredPoolSize() { return 6 * 1024 * 1024; }

private:
    Transformer _transformer;
    Tokenizer _tokenizer;
    Sampler _sampler;

    int _maxTokens;
    bool _loaded;

    // Wrap prompt with Q&A special tokens
    String _wrapPrompt(const char* userInput);

    // Check if token piece contains end marker
    bool _isEndToken(const char* piece);
};
