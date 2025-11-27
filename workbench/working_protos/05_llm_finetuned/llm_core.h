// llm_core.h - Int8 Quantized LLM Inference for ESP32-S3
// Based on llama2.c/runq.c - Single-core, adaptive to any model size
// Supports 1M-6M models with 512/768/1024 vocab sizes

#ifndef LLM_CORE_H
#define LLM_CORE_H

#include <Arduino.h>
#include <SD.h>

// Type alias for compatibility with tokenizer/sampler (originally SIMD, now plain float)
typedef float v4sf;

// Global group size for quantization (read from model file header)
extern int GS;

// Quantized tensor (int8 weights + per-group float scales)
typedef struct {
    int8_t* q;    // Quantized int8 values
    float* s;     // Scale factors (one per group_size values)
} QuantizedTensor;

// Model config (read from 256-byte Q8_0 header, adapts to any model size)
typedef struct {
    int dim;        // Model dimension (128-224 for 1M-6M)
    int hidden_dim; // FFN hidden size (512-896)
    int n_layers;   // Number of transformer layers (4-8)
    int n_heads;    // Number of attention heads (8)
    int n_kv_heads; // Number of KV heads for GQA (4)
    int vocab_size; // Vocabulary size (512, 768, or 1024)
    int seq_len;    // Max sequence length (256)
} Config;

// Quantized transformer weights (from runq.c pattern)
typedef struct {
    // Token embeddings - DEQUANTIZED to fp32 at load time for fast lookup
    QuantizedTensor *q_tokens;
    float* token_embedding_table;  // Dequantized copy

    // RMS norms - stored as fp32 in model file (NOT quantized)
    float* rms_att_weight;   // Per-layer attention norm
    float* rms_ffn_weight;   // Per-layer FFN norm
    float* rms_final_weight; // Final norm before classifier

    // Quantized attention weights (array of QuantizedTensor, one per layer)
    QuantizedTensor *wq;  // Query projection
    QuantizedTensor *wk;  // Key projection
    QuantizedTensor *wv;  // Value projection
    QuantizedTensor *wo;  // Output projection

    // Quantized FFN weights (array of QuantizedTensor, one per layer)
    QuantizedTensor *w1;  // Gate projection
    QuantizedTensor *w2;  // Down projection
    QuantizedTensor *w3;  // Up projection

    // Quantized classifier (may share with q_tokens if shared_classifier=1)
    QuantizedTensor *wcls;
} TransformerWeights;

// Run state - activation buffers, all sizes allocated dynamically based on Config
typedef struct {
    float *x;       // Current activation (dim)
    float *xb;      // Normalized buffer (dim)
    float *xb2;     // Additional buffer (dim)
    float *hb;      // FFN hidden state (hidden_dim)
    float *hb2;     // FFN hidden state 2 (hidden_dim)

    // Quantized activation buffers (for matmul input)
    QuantizedTensor xq;  // Quantized x (dim)
    QuantizedTensor hq;  // Quantized hb (hidden_dim)

    float *q;       // Query vector (dim)
    float *k;       // Key vector (kv_dim)
    float *v;       // Value vector (kv_dim)
    float *att;     // Attention scores (n_heads * seq_len)
    float *logits;  // Output logits (vocab_size)

    // KV cache for autoregressive generation
    float* key_cache;   // (n_layers * seq_len * kv_dim)
    float* value_cache; // (n_layers * seq_len * kv_dim)
} RunState;

// Main transformer structure
typedef struct {
    Config config;
    TransformerWeights weights;
    RunState state;
    uint8_t* raw_data;       // Raw model file data in PSRAM
    size_t file_size;
    uint8_t shared_classifier; // 1 if wcls shares memory with q_tokens
} Transformer;

// ============================================================================
// Quantization functions
// ============================================================================

// Dequantize int8 tensor to float (used once for token embeddings at load)
void dequantize(QuantizedTensor *qx, float* x, int n);

// Quantize float activation to int8 (used before each matmul)
void quantize(QuantizedTensor *qx, float* x, int n);

// ============================================================================
// Core functions
// ============================================================================

// Build transformer from Q8_0 model file on SD card
// Reads config from header, adapts to any model size
// max_seq_len: override model's seq_len to reduce KV cache (0 = use model default)
bool build_transformer_q8(Transformer* t, const char* checkpoint_path, int max_seq_len = 0);

// Free all allocated memory
void free_transformer(Transformer* t);

// ============================================================================
// Neural network operations
// ============================================================================

// RMS normalization
void rmsnorm(float* o, float* x, float* weight, int size);

// Softmax (in-place)
void softmax(float* x, int size);

// Int8 quantized matrix multiplication: W (d,n) @ x (n,) -> xout (d,)
// Both x and w are quantized, output is float
void matmul_q8(float* xout, QuantizedTensor* x, QuantizedTensor* w, int n, int d);

// ============================================================================
// Forward pass
// ============================================================================

// Run single token through transformer, returns logits (vocab_size)
// token: input token ID
// pos: position in sequence (for RoPE and KV cache)
float* forward(Transformer* transformer, int token, int pos);

#endif // LLM_CORE_H
