// llm_core.cpp - Int8 Quantized LLM Inference for ESP32-S3
// Based on llama2.c/runq.c - Single-core, adaptive to any model size

#include "llm_core.h"
#include <math.h>
#include <string.h>

// Global group size (read from model header, typically 32 or 64)
int GS = 64;

// ============================================================================
// Quantization Functions
// ============================================================================

void dequantize(QuantizedTensor *qx, float* x, int n) {
    for (int i = 0; i < n; i++) {
        x[i] = qx->q[i] * qx->s[i / GS];
    }
}

void quantize(QuantizedTensor *qx, float* x, int n) {
    int num_groups = n / GS;
    float Q_MAX = 127.0f;

    for (int group = 0; group < num_groups; group++) {
        float wmax = 0.0f;
        for (int i = 0; i < GS; i++) {
            float val = fabsf(x[group * GS + i]);
            if (val > wmax) wmax = val;
        }

        float scale = wmax / Q_MAX;
        qx->s[group] = scale;

        float inv_scale = (scale != 0.0f) ? 1.0f / scale : 0.0f;
        for (int i = 0; i < GS; i++) {
            float quant_value = x[group * GS + i] * inv_scale;
            qx->q[group * GS + i] = (int8_t)roundf(quant_value);
        }
    }
}

// ============================================================================
// Neural Network Operations
// ============================================================================

void rmsnorm(float* o, float* x, float* weight, int size) {
    float ss = 0.0f;
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

void softmax(float* x, int size) {
    float max_val = x[0];
    for (int i = 1; i < size; i++) {
        if (x[i] > max_val) max_val = x[i];
    }

    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
        x[i] = expf(x[i] - max_val);
        sum += x[i];
    }

    for (int i = 0; i < size; i++) {
        x[i] /= sum;
    }
}

void matmul_q8(float* xout, QuantizedTensor* x, QuantizedTensor* w, int n, int d) {
    for (int i = 0; i < d; i++) {
        float val = 0.0f;
        int32_t ival = 0;
        int in = i * n;

        for (int j = 0; j <= n - GS; j += GS) {
            for (int k = 0; k < GS; k++) {
                ival += (int32_t)x->q[j + k] * (int32_t)w->q[in + j + k];
            }
            val += (float)ival * w->s[(in + j) / GS] * x->s[j / GS];
            ival = 0;
        }
        xout[i] = val;
    }
}

// ============================================================================
// Memory Allocation
// ============================================================================

static bool malloc_run_state(RunState* s, Config* p) {
    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;

    size_t kv_cache_size = (size_t)p->n_layers * p->seq_len * kv_dim * sizeof(float);
    size_t total_needed =
        3 * p->dim * sizeof(float) +
        2 * p->hidden_dim * sizeof(float) +
        p->dim * sizeof(float) +
        p->n_heads * p->seq_len * sizeof(float) +
        p->vocab_size * sizeof(float) +
        2 * kv_cache_size +
        p->dim + p->dim / GS * sizeof(float) +
        p->hidden_dim + p->hidden_dim / GS * sizeof(float);

    if (total_needed > ESP.getFreePsram()) {
        Serial.println("ERROR: Not enough PSRAM for RunState!");
        return false;
    }

    s->x = (float*)ps_calloc(p->dim, sizeof(float));
    s->xb = (float*)ps_calloc(p->dim, sizeof(float));
    s->xb2 = (float*)ps_calloc(p->dim, sizeof(float));
    s->hb = (float*)ps_calloc(p->hidden_dim, sizeof(float));
    s->hb2 = (float*)ps_calloc(p->hidden_dim, sizeof(float));
    s->q = (float*)ps_calloc(p->dim, sizeof(float));
    s->att = (float*)ps_calloc(p->n_heads * p->seq_len, sizeof(float));
    s->logits = (float*)ps_calloc(p->vocab_size, sizeof(float));

    s->key_cache = (float*)ps_calloc(p->n_layers * p->seq_len * kv_dim, sizeof(float));
    s->value_cache = (float*)ps_calloc(p->n_layers * p->seq_len * kv_dim, sizeof(float));

    s->xq.q = (int8_t*)ps_calloc(p->dim, sizeof(int8_t));
    s->xq.s = (float*)ps_calloc(p->dim / GS, sizeof(float));
    s->hq.q = (int8_t*)ps_calloc(p->hidden_dim, sizeof(int8_t));
    s->hq.s = (float*)ps_calloc(p->hidden_dim / GS, sizeof(float));

    if (!s->x || !s->xb || !s->xb2 || !s->hb || !s->hb2 || !s->q ||
        !s->att || !s->logits || !s->key_cache || !s->value_cache ||
        !s->xq.q || !s->xq.s || !s->hq.q || !s->hq.s) {
        Serial.println("ERROR: RunState allocation failed!");
        return false;
    }

    return true;
}

static void free_run_state(RunState* s) {
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
    free(s->xq.q);
    free(s->xq.s);
    free(s->hq.q);
    free(s->hq.s);
}

// ============================================================================
// Weight Memory Mapping (Q8_0 Format)
// ============================================================================

static QuantizedTensor* init_quantized_tensors(void** ptr, int n, int size_each) {
    void* p = *ptr;
    QuantizedTensor* res = (QuantizedTensor*)malloc(n * sizeof(QuantizedTensor));

    for (int i = 0; i < n; i++) {
        res[i].q = (int8_t*)p;
        p = (int8_t*)p + size_each;
        res[i].s = (float*)p;
        p = (float*)p + size_each / GS;
    }

    *ptr = p;
    return res;
}

static void memory_map_weights(TransformerWeights* w, Config* p, void* ptr, uint8_t shared_classifier) {
    int head_size = p->dim / p->n_heads;

    float* fptr = (float*)ptr;
    w->rms_att_weight = fptr;
    fptr += p->n_layers * p->dim;
    w->rms_ffn_weight = fptr;
    fptr += p->n_layers * p->dim;
    w->rms_final_weight = fptr;
    fptr += p->dim;

    ptr = (void*)fptr;

    w->q_tokens = init_quantized_tensors(&ptr, 1, p->vocab_size * p->dim);
    w->wq = init_quantized_tensors(&ptr, p->n_layers, p->dim * (p->n_heads * head_size));
    w->wk = init_quantized_tensors(&ptr, p->n_layers, p->dim * (p->n_kv_heads * head_size));
    w->wv = init_quantized_tensors(&ptr, p->n_layers, p->dim * (p->n_kv_heads * head_size));
    w->wo = init_quantized_tensors(&ptr, p->n_layers, (p->n_heads * head_size) * p->dim);
    w->w1 = init_quantized_tensors(&ptr, p->n_layers, p->dim * p->hidden_dim);
    w->w2 = init_quantized_tensors(&ptr, p->n_layers, p->hidden_dim * p->dim);
    w->w3 = init_quantized_tensors(&ptr, p->n_layers, p->dim * p->hidden_dim);
    w->wcls = shared_classifier ? w->q_tokens : init_quantized_tensors(&ptr, 1, p->dim * p->vocab_size);
}

// ============================================================================
// Transformer Build/Free
// ============================================================================

bool build_transformer_q8(Transformer* t, const char* checkpoint_path, int max_seq_len) {
    File file = SD.open(checkpoint_path, FILE_READ);
    if (!file) {
        Serial.printf("Failed to open model: %s\n", checkpoint_path);
        return false;
    }

    uint32_t magic, version;
    file.read((uint8_t*)&magic, 4);
    file.read((uint8_t*)&version, 4);

    if (magic != 0x616b3432) {
        Serial.printf("Invalid magic: 0x%08X\n", magic);
        file.close();
        return false;
    }
    if (version != 2) {
        Serial.printf("Invalid version: %d\n", version);
        file.close();
        return false;
    }

    file.read((uint8_t*)&t->config, sizeof(Config));
    file.read(&t->shared_classifier, 1);
    file.read((uint8_t*)&GS, 4);

    if (max_seq_len > 0 && max_seq_len < t->config.seq_len) {
        t->config.seq_len = max_seq_len;
    }

    Serial.printf("Model: dim=%d, layers=%d, vocab=%d, seq=%d\n",
                  t->config.dim, t->config.n_layers,
                  t->config.vocab_size, t->config.seq_len);

    t->file_size = file.size();
    t->raw_data = (uint8_t*)ps_malloc(t->file_size);
    if (!t->raw_data) {
        Serial.println("PSRAM allocation failed!");
        file.close();
        return false;
    }

    file.seek(0);
    size_t bytes_read = file.read(t->raw_data, t->file_size);
    file.close();

    if (bytes_read != t->file_size) {
        free(t->raw_data);
        return false;
    }

    memory_map_weights(&t->weights, &t->config, t->raw_data + 256, t->shared_classifier);

    int emb_size = t->config.vocab_size * t->config.dim;
    t->weights.token_embedding_table = (float*)ps_malloc(emb_size * sizeof(float));
    if (!t->weights.token_embedding_table) {
        Serial.println("Embedding allocation failed!");
        free(t->raw_data);
        return false;
    }

    dequantize(t->weights.q_tokens, t->weights.token_embedding_table, emb_size);

    if (!malloc_run_state(&t->state, &t->config)) {
        free(t->weights.token_embedding_table);
        free(t->raw_data);
        return false;
    }

    Serial.println("Model ready!");
    return true;
}

void free_transformer(Transformer* t) {
    if (t->raw_data) {
        free(t->raw_data);
        t->raw_data = nullptr;
    }
    if (t->weights.token_embedding_table) {
        free(t->weights.token_embedding_table);
        t->weights.token_embedding_table = nullptr;
    }

    free(t->weights.q_tokens);
    free(t->weights.wq);
    free(t->weights.wk);
    free(t->weights.wv);
    free(t->weights.wo);
    free(t->weights.w1);
    free(t->weights.w2);
    free(t->weights.w3);
    if (!t->shared_classifier) {
        free(t->weights.wcls);
    }

    free_run_state(&t->state);
}

// ============================================================================
// Forward Pass
// ============================================================================

float* forward(Transformer* transformer, int token, int pos) {
    Config* p = &transformer->config;
    TransformerWeights* w = &transformer->weights;
    RunState* s = &transformer->state;

    int dim = p->dim;
    int kv_dim = (dim * p->n_kv_heads) / p->n_heads;
    int kv_mul = p->n_heads / p->n_kv_heads;
    int hidden_dim = p->hidden_dim;
    int head_size = dim / p->n_heads;

    if (token < 0 || token >= p->vocab_size) {
        return nullptr;
    }

    // Copy token embedding
    float* content_row = w->token_embedding_table + token * dim;
    memcpy(s->x, content_row, dim * sizeof(float));

    // Forward through all layers
    for (int l = 0; l < p->n_layers; l++) {
        rmsnorm(s->xb, s->x, w->rms_att_weight + l * dim, dim);

        int loff = l * p->seq_len * kv_dim;
        s->k = s->key_cache + loff + pos * kv_dim;
        s->v = s->value_cache + loff + pos * kv_dim;

        quantize(&s->xq, s->xb, dim);
        matmul_q8(s->q, &s->xq, &w->wq[l], dim, dim);
        matmul_q8(s->k, &s->xq, &w->wk[l], dim, kv_dim);
        matmul_q8(s->v, &s->xq, &w->wv[l], dim, kv_dim);

        // RoPE
        for (int i = 0; i < dim; i += 2) {
            int head_dim = i % head_size;
            float freq = 1.0f / powf(10000.0f, head_dim / (float)head_size);
            float val = pos * freq;
            float fcr = cosf(val);
            float fci = sinf(val);
            int rotn = i < kv_dim ? 2 : 1;
            for (int v = 0; v < rotn; v++) {
                float* vec = v == 0 ? s->q : s->k;
                float v0 = vec[i];
                float v1 = vec[i + 1];
                vec[i] = v0 * fcr - v1 * fci;
                vec[i + 1] = v0 * fci + v1 * fcr;
            }
        }

        // Multi-head attention
        for (int h = 0; h < p->n_heads; h++) {
            float* q = s->q + h * head_size;
            float* att = s->att + h * p->seq_len;

            for (int t = 0; t <= pos; t++) {
                float* k = s->key_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                float score = 0.0f;
                for (int i = 0; i < head_size; i++) {
                    score += q[i] * k[i];
                }
                att[t] = score / sqrtf(head_size);
            }

            softmax(att, pos + 1);

            float* xb = s->xb + h * head_size;
            memset(xb, 0, head_size * sizeof(float));
            for (int t = 0; t <= pos; t++) {
                float* v = s->value_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                float a = att[t];
                for (int i = 0; i < head_size; i++) {
                    xb[i] += a * v[i];
                }
            }
        }

        quantize(&s->xq, s->xb, dim);
        matmul_q8(s->xb2, &s->xq, &w->wo[l], dim, dim);

        for (int i = 0; i < dim; i++) {
            s->x[i] += s->xb2[i];
        }

        rmsnorm(s->xb, s->x, w->rms_ffn_weight + l * dim, dim);

        quantize(&s->xq, s->xb, dim);
        matmul_q8(s->hb, &s->xq, &w->w1[l], dim, hidden_dim);
        matmul_q8(s->hb2, &s->xq, &w->w3[l], dim, hidden_dim);

        // SwiGLU
        for (int i = 0; i < hidden_dim; i++) {
            float val = s->hb[i];
            val *= 1.0f / (1.0f + expf(-val));
            s->hb[i] = val * s->hb2[i];
        }

        quantize(&s->hq, s->hb, hidden_dim);
        matmul_q8(s->xb, &s->hq, &w->w2[l], hidden_dim, dim);

        for (int i = 0; i < dim; i++) {
            s->x[i] += s->xb[i];
        }
    }

    rmsnorm(s->x, s->x, w->rms_final_weight, dim);

    quantize(&s->xq, s->x, dim);
    matmul_q8(s->logits, &s->xq, w->wcls, dim, p->vocab_size);

    return s->logits;
}
