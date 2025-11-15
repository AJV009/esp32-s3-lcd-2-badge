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

    // Allocate activation buffers in PSRAM (not heap!)
    s->x = (v4sf*)ps_calloc(p->dim, sizeof(v4sf));
    s->xb = (v4sf*)ps_calloc(p->dim, sizeof(v4sf));
    s->xb2 = (v4sf*)ps_calloc(p->dim, sizeof(v4sf));
    s->hb = (v4sf*)ps_calloc(p->hidden_dim, sizeof(v4sf));
    s->hb2 = (v4sf*)ps_calloc(p->hidden_dim, sizeof(v4sf));
    s->q = (v4sf*)ps_calloc(p->dim, sizeof(v4sf));
    s->key_cache = (v4sf*)ps_calloc(p->n_layers * p->seq_len * kv_dim, sizeof(v4sf));
    s->value_cache = (v4sf*)ps_calloc(p->n_layers * p->seq_len * kv_dim, sizeof(v4sf));
    s->att = (v4sf*)ps_calloc(p->n_heads * p->seq_len, sizeof(v4sf));
    s->logits = (v4sf*)ps_calloc(p->vocab_size, sizeof(v4sf));

    if (!s->x || !s->xb || !s->xb2 || !s->hb || !s->hb2 || !s->q ||
        !s->key_cache || !s->value_cache || !s->att || !s->logits) {
        Serial.println("ERROR: RunState PSRAM allocation failed!");
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

// ============================================================================
// SD Card Streaming for Large Models
// ============================================================================

void calculate_layer_offsets(Transformer* t) {
    // Calculate byte offset for each layer's weights in the model file
    Config* p = &t->config;
    int head_size = p->dim / p->n_heads;
    size_t offset = sizeof(Config); // Start after config header

    // Token embedding table (shared, loaded to PSRAM)
    offset += p->vocab_size * p->dim * sizeof(v4sf);

    // For each layer, calculate offsets
    for (int l = 0; l < p->n_layers; l++) {
        t->layer_offsets[l].offset = offset;
        size_t layer_size = 0;

        // RMS attention weight
        layer_size += p->dim * sizeof(v4sf);

        // Attention weights (wq, wk, wv, wo)
        layer_size += p->dim * (p->n_heads * head_size) * sizeof(v4sf); // wq
        layer_size += p->dim * (p->n_kv_heads * head_size) * sizeof(v4sf); // wk
        layer_size += p->dim * (p->n_kv_heads * head_size) * sizeof(v4sf); // wv
        layer_size += (p->n_heads * head_size) * p->dim * sizeof(v4sf); // wo

        // RMS FFN weight
        layer_size += p->dim * sizeof(v4sf);

        // FFN weights (w1, w2, w3)
        layer_size += p->dim * p->hidden_dim * sizeof(v4sf); // w1
        layer_size += p->hidden_dim * p->dim * sizeof(v4sf); // w2
        layer_size += p->dim * p->hidden_dim * sizeof(v4sf); // w3

        t->layer_offsets[l].size = layer_size;
        offset += layer_size;

        Serial.printf("Layer %d: offset=%zu size=%zu KB\n", l,
                      t->layer_offsets[l].offset,
                      t->layer_offsets[l].size / 1024);
    }
}

bool open_sd_model(Transformer* t, const char* sd_path) {
    // Open model file from SD card
    t->sd_file = SD.open(sd_path, FILE_READ);
    if (!t->sd_file) {
        Serial.printf("Failed to open SD model: %s\n", sd_path);
        return false;
    }

    // Read config
    if (t->sd_file.read((uint8_t*)&t->config, sizeof(Config)) != sizeof(Config)) {
        t->sd_file.close();
        return false;
    }

    int shared_weights = t->config.vocab_size > 0 ? 1 : 0;
    t->config.vocab_size = abs(t->config.vocab_size);
    t->file_size = t->sd_file.size();

    Serial.printf("SD Model opened: %zu MB\n", t->file_size / (1024 * 1024));
    Serial.printf("Config: dim=%d layers=%d heads=%d vocab=%d\n",
                  t->config.dim, t->config.n_layers,
                  t->config.n_heads, t->config.vocab_size);

    // Allocate layer buffer in PSRAM (reused for each layer)
    // Size: largest possible layer weight matrix
    int head_size = t->config.dim / t->config.n_heads;
    size_t max_layer_size = t->config.dim * t->config.hidden_dim * sizeof(v4sf) * 3; // w1, w2, w3
    t->layer_buffer_size = max_layer_size;
    t->layer_buffer = (v4sf*)ps_malloc(t->layer_buffer_size);

    if (!t->layer_buffer) {
        Serial.println("Failed to allocate layer buffer in PSRAM!");
        t->sd_file.close();
        return false;
    }

    Serial.printf("Layer buffer allocated: %zu KB in PSRAM\n", t->layer_buffer_size / 1024);

    // Store embedding table offset (too large to fit in PSRAM - will stream per token)
    t->embedding_offset = sizeof(Config);

    // Allocate small buffer for single token embedding
    size_t single_emb_size = t->config.dim * sizeof(v4sf);
    t->embedding_buffer = (v4sf*)ps_malloc(single_emb_size);
    if (!t->embedding_buffer) {
        Serial.println("Failed to allocate embedding buffer!");
        free(t->layer_buffer);
        t->sd_file.close();
        return false;
    }

    // Point token_embedding_table to the buffer (will be loaded per token)
    t->weights.token_embedding_table = t->embedding_buffer;

    Serial.printf("Embedding buffer allocated: %zu bytes (streaming mode)\n", single_emb_size);

    // Calculate layer offsets in file
    calculate_layer_offsets(t);

    // Load final RMS norm weight (after all layers)
    size_t final_rms_offset = t->layer_offsets[t->config.n_layers - 1].offset +
                               t->layer_offsets[t->config.n_layers - 1].size;
    size_t final_rms_size = t->config.dim * sizeof(v4sf);

    t->weights.rms_final_weight = (v4sf*)ps_malloc(final_rms_size);
    if (!t->weights.rms_final_weight) {
        Serial.println("Failed to allocate final RMS weight!");
        return false;
    }

    t->sd_file.seek(final_rms_offset);
    if (t->sd_file.read((uint8_t*)t->weights.rms_final_weight, final_rms_size) != final_rms_size) {
        Serial.println("Failed to read final RMS weight!");
        return false;
    }

    // Load classifier weights (wcls) - same as token embedding table
    t->weights.wcls = t->weights.token_embedding_table;

    Serial.println("Final weights loaded to PSRAM");

    // Allocate run state (activation buffers)
    malloc_run_state(&t->state, &t->config);

    // Check ALL allocations succeeded
    RunState* s = &t->state;
    if (!s->x || !s->xb || !s->xb2 || !s->hb || !s->hb2 || !s->q ||
        !s->key_cache || !s->value_cache || !s->att || !s->logits) {
        Serial.println("ERROR: Run state allocation failed!");
        Serial.printf("x=%p xb=%p xb2=%p hb=%p hb2=%p q=%p\n", s->x, s->xb, s->xb2, s->hb, s->hb2, s->q);
        Serial.printf("key_cache=%p value_cache=%p att=%p logits=%p\n", s->key_cache, s->value_cache, s->att, s->logits);
        return false;
    }

    Serial.printf("Run state allocated (free PSRAM: %d bytes)\n", ESP.getFreePsram());

    // Create FreeRTOS synchronization primitives for dual-core matmul
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

    Serial.println("Dual-core task created");

    // Mark as using streaming
    t->use_streaming = true;

    return true;
}

bool load_layer_from_sd(Transformer* t, int layer) {
    if (!t->use_streaming || !t->sd_file) {
        return false;
    }

    if (layer < 0 || layer >= t->config.n_layers) {
        return false;
    }

    // Seek to layer position in file
    size_t offset = t->layer_offsets[layer].offset;
    size_t size = t->layer_offsets[layer].size;

    if (!t->sd_file.seek(offset)) {
        Serial.printf("Failed to seek to layer %d\n", layer);
        return false;
    }

    // DMA read entire layer to PSRAM buffer
    size_t bytes_read = t->sd_file.read((uint8_t*)t->layer_buffer, size);
    if (bytes_read != size) {
        Serial.printf("Layer %d read failed: %zu/%zu bytes\n", layer, bytes_read, size);
        return false;
    }

    // Map weight pointers to layer buffer
    Config* p = &t->config;
    int head_size = p->dim / p->n_heads;
    v4sf* ptr = t->layer_buffer;

    // RMS attention weight
    t->weights.rms_att_weight = ptr;
    ptr += p->dim;

    // Attention weights
    t->weights.wq = ptr;
    ptr += p->dim * (p->n_heads * head_size);
    t->weights.wk = ptr;
    ptr += p->dim * (p->n_kv_heads * head_size);
    t->weights.wv = ptr;
    ptr += p->dim * (p->n_kv_heads * head_size);
    t->weights.wo = ptr;
    ptr += (p->n_heads * head_size) * p->dim;

    // RMS FFN weight
    t->weights.rms_ffn_weight = ptr;
    ptr += p->dim;

    // FFN weights
    t->weights.w1 = ptr;
    ptr += p->dim * p->hidden_dim;
    t->weights.w2 = ptr;
    ptr += p->hidden_dim * p->dim;
    t->weights.w3 = ptr;

    return true;
}

bool load_token_embedding(Transformer* t, int token) {
    if (!t->use_streaming || !t->sd_file) {
        return false;
    }

    if (token < 0 || token >= t->config.vocab_size) {
        return false;
    }

    // Calculate offset for this token's embedding
    size_t token_emb_size = t->config.dim * sizeof(v4sf);
    size_t offset = t->embedding_offset + (token * token_emb_size);

    // Seek to token embedding
    if (!t->sd_file.seek(offset)) {
        return false;
    }

    // Read single token embedding into buffer
    size_t bytes_read = t->sd_file.read((uint8_t*)t->embedding_buffer, token_emb_size);
    if (bytes_read != token_emb_size) {
        return false;
    }

    return true;
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

    // Safety checks
    if (!s->x || !s->xb || !s->logits || !s->key_cache || !s->value_cache) {
        Serial.println("ERROR: RunState has NULL pointers!");
        return nullptr;
    }

    v4sf* x = s->x;
    int dim = p->dim;
    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;
    int kv_mul = p->n_heads / p->n_kv_heads;
    int hidden_dim = p->hidden_dim;
    int head_size = dim / p->n_heads;

    // Copy token embedding into x
    if (transformer->use_streaming) {
        // SD STREAMING: Load token embedding from SD card
        if (!load_token_embedding(transformer, token)) {
            Serial.printf("Failed to load embedding for token %d\n", token);
            return nullptr;
        }
        // embedding_buffer already contains the token embedding
        memcpy(x, transformer->embedding_buffer, dim * sizeof(*x));
    } else {
        // PSRAM mode: embedding table fully loaded
        v4sf* content_row = w->token_embedding_table + token * dim;
        memcpy(x, content_row, dim * sizeof(*x));
    }

    // Progress: Start new line for this token
    if (transformer->use_streaming && pos >= 0) {
        Serial.print("\n-> ");
    }

    // Forward through all layers
    for (unsigned long long l = 0; l < p->n_layers; l++) {
        // SD STREAMING: Load layer weights from SD card if using streaming
        if (transformer->use_streaming) {
            Serial.printf("[L%llu]", l);
            if (!load_layer_from_sd(transformer, l)) {
                Serial.printf("\nFailed to load layer %llu from SD\n", l);
                return nullptr;
            }
        }

        // Attention RMSnorm
        v4sf* rms_att = transformer->use_streaming ? w->rms_att_weight : (w->rms_att_weight + l * dim);
        rmsnorm(s->xb, x, rms_att, dim);

        // QKV projections
        int loff = l * p->seq_len * kv_dim;
        s->k = s->key_cache + loff + pos * kv_dim;
        s->v = s->value_cache + loff + pos * kv_dim;

        // Weight pointers (indexed for PSRAM mode, base for streaming mode)
        v4sf* wq_ptr = transformer->use_streaming ? w->wq : (w->wq + l * dim * dim);
        v4sf* wk_ptr = transformer->use_streaming ? w->wk : (w->wk + l * dim * kv_dim);
        v4sf* wv_ptr = transformer->use_streaming ? w->wv : (w->wv + l * dim * kv_dim);

        matmul(s->q, s->xb, wq_ptr, dim, dim);
        matmul(s->k, s->xb, wk_ptr, dim, kv_dim);
        matmul(s->v, s->xb, wv_ptr, dim, kv_dim);

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
        v4sf* wo_ptr = transformer->use_streaming ? w->wo : (w->wo + l * dim * dim);
        matmul(s->xb2, s->xb, wo_ptr, dim, dim);

        // Residual connection
        for (int i = 0; i < dim; i++) {
            x[i] += s->xb2[i];
        }

        // FFN RMSnorm
        v4sf* rms_ffn = transformer->use_streaming ? w->rms_ffn_weight : (w->rms_ffn_weight + l * dim);
        rmsnorm(s->xb, x, rms_ffn, dim);

        // FFN
        v4sf* w1_ptr = transformer->use_streaming ? w->w1 : (w->w1 + l * dim * hidden_dim);
        v4sf* w2_ptr = transformer->use_streaming ? w->w2 : (w->w2 + l * dim * hidden_dim);
        v4sf* w3_ptr = transformer->use_streaming ? w->w3 : (w->w3 + l * dim * hidden_dim);

        matmul(s->hb, s->xb, w1_ptr, dim, hidden_dim);
        matmul(s->hb2, s->xb, w3_ptr, dim, hidden_dim);

        // SwiGLU activation
        for (int i = 0; i < hidden_dim; i++) {
            v4sf val = s->hb[i];
            val *= (1.0f / (1.0f + expf(-val)));
            val *= s->hb2[i];
            s->hb[i] = val;
        }

        matmul(s->xb, s->hb, w2_ptr, hidden_dim, dim);

        // Residual connection
        for (int i = 0; i < dim; i++) {
            x[i] += s->xb[i];
        }
    }

    // Final RMSnorm
    rmsnorm(x, x, w->rms_final_weight, dim);

    // Classifier (compute logits for all vocab tokens)
    if (transformer->use_streaming) {
        // SD STREAMING: Compute logits by streaming each vocab embedding
        // This is SLOW but necessary when embeddings don't fit in PSRAM
        Serial.print("[CLS]");
        for (int i = 0; i < p->vocab_size; i++) {
            // Load embedding for vocab token i
            if (!load_token_embedding(transformer, i)) {
                Serial.printf("\nFailed to load vocab embedding %d\n", i);
                return nullptr;
            }
            // Compute dot product: logits[i] = x · embedding[i]
            float val = 0.0f;
            for (int j = 0; j < dim; j++) {
                val += x[j] * transformer->embedding_buffer[j];
            }
            s->logits[i] = val;
        }
        Serial.print(" ");
    } else {
        // PSRAM mode: full wcls matrix available
        matmul(s->logits, x, w->wcls, dim, p->vocab_size);
    }

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
