// llm_core.h - Core LLM inference engine for ESP32-S3
// Optimized with ESP-DSP SIMD and dual-core parallelization

#ifndef LLM_CORE_H
#define LLM_CORE_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>

// Type alias for float (matching esp32-llm)
typedef float v4sf;

// Configuration structure
typedef struct {
    int dim;
    int hidden_dim;
    int n_layers;
    int n_heads;
    int n_kv_heads;
    int vocab_size;
    int seq_len;
} Config;

// Transformer weights (raw pointers for zero-copy memory mapping)
typedef struct {
    v4sf* token_embedding_table;
    v4sf* rms_att_weight;
    v4sf* wq;
    v4sf* wk;
    v4sf* wv;
    v4sf* wo;
    v4sf* rms_ffn_weight;
    v4sf* w1;
    v4sf* w2;
    v4sf* w3;
    v4sf* rms_final_weight;
    v4sf* wcls;
} TransformerWeights;

// Run state (activation buffers)
typedef struct {
    v4sf* x;
    v4sf* xb;
    v4sf* xb2;
    v4sf* hb;
    v4sf* hb2;
    v4sf* q;
    v4sf* k;
    v4sf* v;
    v4sf* att;
    v4sf* logits;
    v4sf* key_cache;
    v4sf* value_cache;
} RunState;

// Main transformer structure
typedef struct {
    Config config;
    TransformerWeights weights;
    RunState state;
    v4sf* data;
    size_t file_size;
} Transformer;

// Task parameters for dual-core parallelization
typedef struct {
    v4sf* xout;
    v4sf* x;
    v4sf* w;
    int start;
    int end;
    int n;
    int d;
    int task_num;
} MatMulTaskParams;

typedef struct {
    RunState* s;
    TransformerWeights* w;
    Config* p;
    int pos;
    int start;
    int loff;
    int end;
    int dim;
    int kv_dim;
    int kv_mul;
    int hidden_dim;
    int head_size;
    int task_num;
} ForwardTaskParams;

// Event bits for task synchronization
#define TASK_0_BIT (1 << 0)
#define TASK_1_BIT (1 << 1)
#define ALL_SYNC_BITS (TASK_0_BIT | TASK_1_BIT)

// Global handles
extern EventGroupHandle_t xEventGroup;
extern SemaphoreHandle_t semaDataReady;
extern TaskHandle_t matmul_task_handle;
extern MatMulTaskParams* matmul_params;

// Core functions
void malloc_run_state(RunState* s, Config* p);
void free_run_state(RunState* s);
void memory_map_weights(TransformerWeights* w, Config* p, v4sf* ptr, int shared_weights);
bool read_checkpoint(const char* checkpoint, Config* config, TransformerWeights* weights, v4sf** data, size_t* file_size);
bool build_transformer(Transformer* t, const char* checkpoint_path);
void free_transformer(Transformer* t);

// Neural net operations
void rmsnorm(v4sf* o, v4sf* x, v4sf* weight, int size);
void softmax(v4sf* x, int size);
void matmul(v4sf* xout, v4sf* x, v4sf* w, int n, int d);

// Forward pass
v4sf* forward(Transformer* transformer, int token, int pos);

// Task functions (must be public for FreeRTOS)
void matmul_task(void* params);

#endif // LLM_CORE_H
