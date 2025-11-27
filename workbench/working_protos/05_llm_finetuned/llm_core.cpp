// llm_core.cpp - Int8 Quantized LLM Inference for ESP32-S3
// Based on llama2.c/runq.c - Single-core, adaptive to any model size

#include "llm_core.h"
#include <math.h>
#include <string.h>
#include <esp_heap_caps.h>  // For heap_caps_aligned_alloc (16-byte aligned SIMD buffers)

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
    // Sum of squares using SIMD dot product
    float ss = 0.0f;
    dsps_dotprod_f32_aes3(x, x, &ss, size);

    // Normalization factor
    ss = 1.0f / sqrtf(ss / size + 1e-5f);

    // Scale x by ss, then multiply by weight
    // o[i] = weight[i] * (ss * x[i])
    dsps_mulc_f32_ae32(x, o, size, ss, 1, 1);       // o = x * ss
    dsps_mul_f32_ae32(o, weight, o, size, 1, 1, 1); // o = o * weight
}

void softmax(float* x, int size) {
    // Find max (no SIMD equivalent that's faster for small arrays)
    float max_val = x[0];
    for (int i = 1; i < size; i++) {
        if (x[i] > max_val) max_val = x[i];
    }

    // Exp and sum (expf() is the bottleneck, can't SIMD this)
    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
        x[i] = expf(x[i] - max_val);
        sum += x[i];
    }

    // Vectorized division using SIMD multiply by inverse
    float inv_sum = 1.0f / sum;
    dsps_mulc_f32_ae32(x, x, size, inv_sum, 1, 1);
}

// PIE SIMD int8 dot product - returns full int32 accumulator
// Processes 16 int8 pairs at a time using ESP32-S3 PIE vector instructions
// Requirements: len must be multiple of 16, both pointers must be 16-byte aligned
static inline int32_t dotprod_s8_simd(const int8_t* a, const int8_t* b, int len) {
    int32_t result;
    int chunks = len >> 4;  // len / 16

    asm volatile(
        // Clear 40-bit accumulator
        "wur.accx_0 %[zero]\n"
        "wur.accx_1 %[zero]\n"

        // Process 16 int8 pairs per iteration
        "loopnez %[chunks], 1f\n"
        "ee.vld.128.ip q0, %[a], 16\n"    // Load 16 int8s from a
        "ee.vld.128.ip q1, %[b], 16\n"    // Load 16 int8s from b
        "ee.vmulas.s8.accx q0, q1\n"      // MAC: accx += sum(q0[i] * q1[i])
        "1:\n"

        // Read low 32 bits of accumulator
        "rur.accx_0 %[result]\n"

        : [result] "=r" (result), [a] "+r" (a), [b] "+r" (b)
        : [chunks] "r" (chunks), [zero] "r" (0)
        : "memory"
    );

    return result;
}

void matmul_q8(float* xout, QuantizedTensor* x, QuantizedTensor* w, int n, int d) {
    for (int i = 0; i < d; i++) {
        float val = 0.0f;
        int in = i * n;

        // Process each quantization group with SIMD
        for (int j = 0; j <= n - GS; j += GS) {
            // SIMD dot product for this group (GS elements)
            int32_t ival = dotprod_s8_simd(x->q + j, w->q + in + j, GS);

            // Scale by quantization scales
            val += (float)ival * w->s[(in + j) / GS] * x->s[j / GS];
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

    // Quantized activation buffers - MUST be 16-byte aligned for PIE SIMD
    s->xq.q = (int8_t*)heap_caps_aligned_alloc(16, p->dim * sizeof(int8_t), MALLOC_CAP_SPIRAM);
    s->xq.s = (float*)ps_calloc(p->dim / GS, sizeof(float));
    s->hq.q = (int8_t*)heap_caps_aligned_alloc(16, p->hidden_dim * sizeof(int8_t), MALLOC_CAP_SPIRAM);
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
    // MUST be 16-byte aligned for PIE SIMD weight access
    t->raw_data = (uint8_t*)heap_caps_aligned_alloc(16, t->file_size, MALLOC_CAP_SPIRAM);
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

    // Initialize RoPE cache
    init_rope_cache(t);

    Serial.println("Model ready!");
    return true;
}

void init_rope_cache(Transformer* t) {
    int head_size = t->config.dim / t->config.n_heads;
    int half_head = head_size / 2;
    int cache_size = t->config.seq_len * half_head;

    t->rope_cos = (float*)ps_malloc(cache_size * sizeof(float));
    t->rope_sin = (float*)ps_malloc(cache_size * sizeof(float));

    if (!t->rope_cos || !t->rope_sin) {
        Serial.println("Warning: RoPE cache allocation failed, using runtime trig");
        return;
    }

    // Precompute sin/cos for all positions and head dimensions
    for (int pos = 0; pos < t->config.seq_len; pos++) {
        for (int i = 0; i < half_head; i++) {
            float freq = 1.0f / powf(10000.0f, (2.0f * i) / head_size);
            float val = pos * freq;
            t->rope_cos[pos * half_head + i] = cosf(val);
            t->rope_sin[pos * half_head + i] = sinf(val);
        }
    }

    Serial.printf("RoPE cache: %d KB\n", (cache_size * 2 * sizeof(float)) / 1024);
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
    if (t->rope_cos) {
        free(t->rope_cos);
        t->rope_cos = nullptr;
    }
    if (t->rope_sin) {
        free(t->rope_sin);
        t->rope_sin = nullptr;
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

        // RoPE with cached sin/cos lookup (eliminates runtime trig)
        int half_head = head_size / 2;
        float* cos_ptr = transformer->rope_cos + pos * half_head;
        float* sin_ptr = transformer->rope_sin + pos * half_head;

        for (int i = 0; i < dim; i += 2) {
            int idx = (i / 2) % half_head;
            float fcr = cos_ptr[idx];
            float fci = sin_ptr[idx];
            int rotn = i < kv_dim ? 2 : 1;
            for (int v = 0; v < rotn; v++) {
                float* vec = v == 0 ? s->q : s->k;
                float v0 = vec[i];
                float v1 = vec[i + 1];
                vec[i] = v0 * fcr - v1 * fci;
                vec[i + 1] = v0 * fci + v1 * fcr;
            }
        }

        // Multi-head attention with SIMD
        float scale = 1.0f / sqrtf(head_size);
        for (int h = 0; h < p->n_heads; h++) {
            float* q = s->q + h * head_size;
            float* att = s->att + h * p->seq_len;

            // Attention scoring with SIMD dot product
            for (int t = 0; t <= pos; t++) {
                float* k = s->key_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                float score = 0.0f;
                dsps_dotprod_f32_aes3(q, k, &score, head_size);
                att[t] = score * scale;
            }

            softmax(att, pos + 1);

            // Attention aggregation with SIMD
            float* xb = s->xb + h * head_size;
            memset(xb, 0, head_size * sizeof(float));
            for (int t = 0; t <= pos; t++) {
                float* v = s->value_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                float a = att[t];
                // xb += a * v using SIMD: temp = v * a, xb += temp
                dsps_mulc_f32_ae32(v, s->xb2, head_size, a, 1, 1);
                dsps_add_f32_ae32(xb, s->xb2, xb, head_size, 1, 1, 1);
            }
        }

        quantize(&s->xq, s->xb, dim);
        matmul_q8(s->xb2, &s->xq, &w->wo[l], dim, dim);

        // Residual connection with SIMD
        dsps_add_f32_ae32(s->x, s->xb2, s->x, dim, 1, 1, 1);

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

        // Residual connection with SIMD
        dsps_add_f32_ae32(s->x, s->xb, s->x, dim, 1, 1, 1);
    }

    rmsnorm(s->x, s->x, w->rms_final_weight, dim);

    quantize(&s->xq, s->x, dim);
    matmul_q8(s->logits, &s->xq, w->wcls, dim, p->vocab_size);

    return s->logits;
}
