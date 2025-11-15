// llm_core.cpp - Core LLM inference with ESP-DSP SIMD and dual-core optimization
// Based on esp32-llm by karpathy/llama2.c with ESP32 optimizations

#include "llm_core.h"
#include <FFat.h>
#include <esp_dsp.h>

// Global task handles and synchronization
EventGroupHandle_t xEventGroup = NULL;
SemaphoreHandle_t semaDataReady = NULL;
TaskHandle_t matmul_task_handle = NULL;
MatMulTaskParams* matmul_params = NULL;

// ============================================================================
// Memory Management
// ============================================================================

void malloc_run_state(RunState* s, Config* p) {
    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;

    s->x = (v4sf*)calloc(p->dim, sizeof(v4sf));
    s->xb = (v4sf*)calloc(p->dim, sizeof(v4sf));
    s->xb2 = (v4sf*)calloc(p->dim, sizeof(v4sf));
    s->hb = (v4sf*)calloc(p->hidden_dim, sizeof(v4sf));
    s->hb2 = (v4sf*)calloc(p->hidden_dim, sizeof(v4sf));
    s->q = (v4sf*)calloc(p->dim, sizeof(v4sf));
    s->key_cache = (v4sf*)calloc(p->n_layers * p->seq_len * kv_dim, sizeof(v4sf));
    s->value_cache = (v4sf*)calloc(p->n_layers * p->seq_len * kv_dim, sizeof(v4sf));
    s->att = (v4sf*)calloc(p->n_heads * p->seq_len, sizeof(v4sf));
    s->logits = (v4sf*)calloc(p->vocab_size, sizeof(v4sf));

    if (!s->x || !s->xb || !s->xb2 || !s->hb || !s->hb2 || !s->q ||
        !s->key_cache || !s->value_cache || !s->att || !s->logits) {
        Serial.println("RunState malloc failed!");
    }
}

void free_run_state(RunState* s) {
    free(s->x);
    free(s->xb);
    free(s->xb2);
    free(s->hb);
    free(s->hb2);
    free(s->q);
    free(s->att);
    free(s->logits);
    free(s->key_cache);
    free(s->value_cache);
}

void memory_map_weights(TransformerWeights* w, Config* p, v4sf* ptr, int shared_weights) {
    int head_size = p->dim / p->n_heads;
    unsigned long long n_layers = p->n_layers;

    w->token_embedding_table = ptr;
    ptr += p->vocab_size * p->dim;
    w->rms_att_weight = ptr;
    ptr += n_layers * p->dim;
    w->wq = ptr;
    ptr += n_layers * p->dim * (p->n_heads * head_size);
    w->wk = ptr;
    ptr += n_layers * p->dim * (p->n_kv_heads * head_size);
    w->wv = ptr;
    ptr += n_layers * p->dim * (p->n_kv_heads * head_size);
    w->wo = ptr;
    ptr += n_layers * (p->n_heads * head_size) * p->dim;
    w->rms_ffn_weight = ptr;
    ptr += n_layers * p->dim;
    w->w1 = ptr;
    ptr += n_layers * p->dim * p->hidden_dim;
    w->w2 = ptr;
    ptr += n_layers * p->hidden_dim * p->dim;
    w->w3 = ptr;
    ptr += n_layers * p->dim * p->hidden_dim;
    w->rms_final_weight = ptr;
    ptr += p->dim;
    ptr += p->seq_len * head_size / 2;
    ptr += p->seq_len * head_size / 2;
    w->wcls = shared_weights ? w->token_embedding_table : ptr;
}

bool read_checkpoint(const char* checkpoint, Config* config, TransformerWeights* weights,
                     v4sf** data, size_t* file_size) {
    File file = FFat.open(checkpoint, "r");
    if (!file) {
        Serial.printf("Failed to open %s\n", checkpoint);
        return false;
    }

    // Read config header
    if (file.read((uint8_t*)config, sizeof(Config)) != sizeof(Config)) {
        file.close();
        return false;
    }

    int shared_weights = config->vocab_size > 0 ? 1 : 0;
    config->vocab_size = abs(config->vocab_size);

    *file_size = file.size();
    Serial.printf("Model size: %zu bytes\n", *file_size);
    Serial.printf("Free heap before malloc: %d\n", esp_get_free_heap_size());

    // Allocate memory (malloc uses PSRAM when configured)
    *data = (v4sf*)malloc(*file_size);
    if (*data == NULL) {
        Serial.println("Malloc failed!");
        file.close();
        return false;
    }

    // Read entire file into memory
    file.seek(0);
    size_t bytes_read = file.read((uint8_t*)*data, *file_size);
    file.close();

    if (bytes_read != *file_size) {
        Serial.printf("Read failed: %zu / %zu bytes\n", bytes_read, *file_size);
        free(*data);
        return false;
    }

    Serial.printf("Model loaded to memory\n");
    Serial.printf("Free heap after load: %d\n", esp_get_free_heap_size());

    v4sf* weights_ptr = *data + sizeof(Config) / sizeof(v4sf);
    memory_map_weights(weights, config, weights_ptr, shared_weights);

    return true;
}

// ============================================================================
// Dual-Core Matrix Multiplication Task
// ============================================================================

void matmul_task(void* params) {
    MatMulTaskParams* p = (MatMulTaskParams*)params;

    for (;;) {
        if (xSemaphoreTake(semaDataReady, portMAX_DELAY) == pdTRUE) {
            // Process second half of matrix multiply using ESP-DSP SIMD
            for (int i = p->start; i < p->end; i++) {
                v4sf val = 0.0f;
                v4sf* row = &p->w[i * p->n];
                dsps_dotprod_f32_aes3(row, p->x, &val, p->n);
                p->xout[i] = val;
            }

            xSemaphoreGive(semaDataReady);
            xEventGroupSync(xEventGroup, p->task_num, ALL_SYNC_BITS, portMAX_DELAY);
        }
    }
}

// ============================================================================
// Neural Network Operations
// ============================================================================

void rmsnorm(v4sf* o, v4sf* x, v4sf* weight, int size) {
    v4sf ss = 0.0f;
    for (int j = 0; j < size; j++) {
        ss += x[j] * x[j];
    }
    ss /= size;
    ss += 1e-5f;
    ss = 1.0f / sqrtf(ss);

    for (int j = 0; j < size; j++) {
        o[j] = weight[j] * (ss * x[j]);
    }
}

void softmax(v4sf* x, int size) {
    v4sf max_val = x[0];
    for (int i = 1; i < size; i++) {
        if (x[i] > max_val) max_val = x[i];
    }

    v4sf sum = 0.0f;
    for (int i = 0; i < size; i++) {
        x[i] = expf(x[i] - max_val);
        sum += x[i];
    }

    for (int i = 0; i < size; i++) {
        x[i] /= sum;
    }
}

void matmul(v4sf* xout, v4sf* x, v4sf* w, int n, int d) {
    // Split work: Core 0 does first half, Core 1 does second half
    *matmul_params = (MatMulTaskParams){xout, x, w, d / 2, d, n, d, TASK_1_BIT};
    xSemaphoreGive(semaDataReady);

    // Core 0: Process first half using ESP-DSP SIMD
    for (int i = 0; i < d / 2; i++) {
        v4sf val = 0.0f;
        v4sf* row = &w[i * n];
        dsps_dotprod_f32_aes3(row, x, &val, n);
        xout[i] = val;
    }

    // Wait for Core 1 to finish
    if (xSemaphoreTake(semaDataReady, portMAX_DELAY) == pdTRUE) {
        xEventGroupSync(xEventGroup, TASK_0_BIT, ALL_SYNC_BITS, portMAX_DELAY);
        xEventGroupClearBits(xEventGroup, ALL_SYNC_BITS);
    }
}

// ============================================================================
// Transformer Forward Pass
// ============================================================================

v4sf* forward(Transformer* transformer, int token, int pos) {
    Config* p = &transformer->config;
    TransformerWeights* w = &transformer->weights;
    RunState* s = &transformer->state;
    v4sf* x = s->x;
    int dim = p->dim;
    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;
    int kv_mul = p->n_heads / p->n_kv_heads;
    int hidden_dim = p->hidden_dim;
    int head_size = dim / p->n_heads;

    // Copy token embedding into x
    v4sf* content_row = w->token_embedding_table + token * dim;
    memcpy(x, content_row, dim * sizeof(*x));

    // Forward through all layers
    for (unsigned long long l = 0; l < p->n_layers; l++) {
        // Attention RMSnorm
        rmsnorm(s->xb, x, w->rms_att_weight + l * dim, dim);

        // QKV projections
        int loff = l * p->seq_len * kv_dim;
        s->k = s->key_cache + loff + pos * kv_dim;
        s->v = s->value_cache + loff + pos * kv_dim;

        matmul(s->q, s->xb, w->wq + l * dim * dim, dim, dim);
        matmul(s->k, s->xb, w->wk + l * dim * kv_dim, dim, kv_dim);
        matmul(s->v, s->xb, w->wv + l * dim * kv_dim, dim, kv_dim);

        // RoPE positional encoding
        for (int i = 0; i < dim; i += 2) {
            int head_dim = i % head_size;
            v4sf freq = 1.0f / powf(10000.0f, head_dim / (v4sf)head_size);
            v4sf val = pos * freq;
            v4sf fcr = cosf(val);
            v4sf fci = sinf(val);
            int rotn = i < kv_dim ? 2 : 1;
            for (int v = 0; v < rotn; v++) {
                v4sf* vec = v == 0 ? s->q : s->k;
                v4sf v0 = vec[i];
                v4sf v1 = vec[i + 1];
                vec[i] = v0 * fcr - v1 * fci;
                vec[i + 1] = v0 * fci + v1 * fcr;
            }
        }

        // Multi-head attention
        for (int h = 0; h < p->n_heads; h++) {
            v4sf* q = s->q + h * head_size;
            v4sf* att = s->att + h * p->seq_len;

            for (int t = 0; t <= pos; t++) {
                v4sf* k = s->key_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                v4sf score = 0.0f;
                for (int i = 0; i < head_size; i++) {
                    score += q[i] * k[i];
                }
                score /= sqrtf(head_size);
                att[t] = score;
            }

            softmax(att, pos + 1);

            v4sf* xb = s->xb + h * head_size;
            memset(xb, 0, head_size * sizeof(v4sf));
            for (int t = 0; t <= pos; t++) {
                v4sf* v = s->value_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                v4sf a = att[t];
                for (int i = 0; i < head_size; i++) {
                    xb[i] += a * v[i];
                }
            }
        }

        // Output projection
        matmul(s->xb2, s->xb, w->wo + l * dim * dim, dim, dim);

        // Residual connection
        for (int i = 0; i < dim; i++) {
            x[i] += s->xb2[i];
        }

        // FFN RMSnorm
        rmsnorm(s->xb, x, w->rms_ffn_weight + l * dim, dim);

        // FFN
        matmul(s->hb, s->xb, w->w1 + l * dim * hidden_dim, dim, hidden_dim);
        matmul(s->hb2, s->xb, w->w3 + l * dim * hidden_dim, dim, hidden_dim);

        // SwiGLU activation
        for (int i = 0; i < hidden_dim; i++) {
            v4sf val = s->hb[i];
            val *= (1.0f / (1.0f + expf(-val)));
            val *= s->hb2[i];
            s->hb[i] = val;
        }

        matmul(s->xb, s->hb, w->w2 + l * dim * hidden_dim, hidden_dim, dim);

        // Residual connection
        for (int i = 0; i < dim; i++) {
            x[i] += s->xb[i];
        }
    }

    // Final RMSnorm
    rmsnorm(x, x, w->rms_final_weight, dim);

    // Classifier
    matmul(s->logits, x, w->wcls, dim, p->vocab_size);

    return s->logits;
}

// ============================================================================
// Transformer Initialization
// ============================================================================

bool build_transformer(Transformer* t, const char* checkpoint_path) {
    if (!read_checkpoint(checkpoint_path, &t->config, &t->weights, &t->data, &t->file_size)) {
        return false;
    }

    malloc_run_state(&t->state, &t->config);

    // Create FreeRTOS synchronization primitives
    xEventGroup = xEventGroupCreate();
    semaDataReady = xSemaphoreCreateBinary();
    xSemaphoreGive(semaDataReady);
    xSemaphoreTake(semaDataReady, 0);

    // Allocate task parameters
    matmul_params = (MatMulTaskParams*)malloc(sizeof(MatMulTaskParams));

    // Create dual-core matmul task on Core 1
    xTaskCreatePinnedToCore(
        matmul_task,
        "MatMul",
        4096,
        matmul_params,
        19,
        &matmul_task_handle,
        1  // Core 1
    );

    Serial.println("Transformer built successfully");
    return true;
}

void free_transformer(Transformer* t) {
    if (t->data) {
        free(t->data);
    }
    free_run_state(&t->state);

    if (matmul_task_handle) {
        vTaskDelete(matmul_task_handle);
    }
    if (xEventGroup) {
        vEventGroupDelete(xEventGroup);
    }
    if (semaDataReady) {
        vSemaphoreDelete(semaDataReady);
    }
    if (matmul_params) {
        free(matmul_params);
    }
}
