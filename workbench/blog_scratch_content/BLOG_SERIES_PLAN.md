# OAISYS25 Badge Blog Series Plan (Revised)

## Series Title: "Project 'Tiny Haze' - An ESP32 powered Digital Badge"

### Previously Published Blogs

**Blog 1 (series_order: 1)**: `0015-esp32-video-badge-gyro-rotation/index.md`
- Hardware overview (ESP32-S3-LCD-2)
- MJPEG video playback from PSRAM
- MemoryStream pattern for JPEG decoding
- Gyro-based display rotation with debouncing
- Button controls (OneButton library)
- FFat partition and upload_to_flash.sh

**Blog 2 (series_order: 2)**: `0018-decontructing-llama2-c-and-exp32-llm/index.md`
- Transformer architecture basics (embeddings, attention, FFN)
- Memory deep-dive (KV cache, activation buffers)
- Karpathy's llama2.c overview
- ESP-DSP SIMD optimization (`dsps_dotprod_f32_aes3`)
- Dual-core parallelization with FreeRTOS
- Stories260K (pure PSRAM) - 25-30 tok/s
- Stories15M (SD streaming) - proof of concept

---

## New Blog Series (4 Parts)

### Blog 3: Wake Word Detection - Training "Hey Daisy" from Scratch
**series_order: 3**
**Estimated Length**: 3000-3500 words

#### Narrative Arc
"We needed an always-listening trigger phrase. Here's how we trained a custom wake word detector that runs continuously while consuming minimal resources."

#### Topics
1. **Why Wake Words?** - Always-listening vs push-to-talk tradeoffs
2. **MicroWakeWord Framework** - Architecture overview
3. **Training Data Generation**
   - 20 phonetic variations of "Hey Daisy"
   - Piper TTS for synthetic samples (2,000)
   - XTTS-v2 for realistic voice diversity (1,600 from 8 TED speakers)
4. **The Confusable Negatives Innovation**
   - Why generic negatives aren't enough
   - Training explicit rejection: "hey lazy", "hey baby", "hey crazy", partials
   - High sampling weight (15.0) during training
5. **Audio Augmentation Pipeline**
   - Background noise injection (75%)
   - Room impulse response convolution (50%)
   - SpecAugment: time/frequency masking
6. **Feature Extraction**
   - 40-bin mel-spectrogram (30ms window, 10ms stride)
   - TensorFlow Lite Microfrontend library
   - PCAN gain control for robustness
7. **On-Device Inference**
   - EdgeNeuron TFLite streaming model
   - Placement new for interpreter reset
   - Sliding window probability detection
   - Cooldown mechanism

#### Key Files Referenced
- `local_llm_badge/src/ml/wake_word.cpp` - Detection implementation
- `local_llm_badge/src/audio/mic_stream.cpp` - Microfrontend integration
- `workbench/working_protos/03_custom_wakeword/` - Original prototype
- `workbench/tests/wakeword_training_ipynb_poc/` - Training notebook

#### Code Highlights
```cpp
// Placement new resets interpreter without reallocation
static uint8_t interpreterBuffer[sizeof(tflite::MicroInterpreter)] __attribute__((aligned(16)));
_interpreter = new (interpreterBuffer) tflite::MicroInterpreter(model, resolver, _tensorArena, ...);

// Sliding window detection with hysteresis
if (avg >= _config.probability_cutoff && high_frames >= _config.min_high_frames) {
    if (millis() - _lastDetectionMs >= _config.cooldown_ms) {
        return true;  // Wake word detected!
    }
}
```

#### Conversation History References
- `.specstory/history/2025-11-23_*` - Wake word training discussions
- `.specstory/history/2025-11-24_*` - Confusable negatives strategy

---

### Blog 4: From STT Failure to Intent Embeddings - When Plan A Doesn't Work
**series_order: 4**
**Estimated Length**: 3500-4000 words

#### Narrative Arc
"We wanted speech-to-text, but every STT model was too big for 8MB PSRAM. Instead of giving up, we invented a workaround: train an audio encoder to match spoken questions to pre-embedded intents using similarity search. Here's that journey."

#### Topics
1. **The STT Problem**
   - Whisper: 75MB+ minimum
   - wav2vec: 100MB+
   - Even tiny models: 30MB+
   - Reality: 8MB PSRAM, 6MB needed for LLM
2. **The Pivot: Intent Similarity**
   - If we have 2,008 known questions, why transcribe?
   - Match audio embedding → closest pre-computed text embedding
   - Trade flexibility for efficiency
3. **Contrastive Learning for Audio-Text Alignment**
   - Audio branch: YAMNet (1024-dim) → projection → 256-dim
   - Text branch: MPNet (768-dim) → projection → 256-dim
   - Loss: Bring same Q&A pairs together, push different apart
4. **The Bucketed Batch Sampling Innovation**
   - Problem: Naive batching learns trivial distinctions
   - Solution: Semantic clustering (threshold 0.85)
   - Each batch = ONE sample per semantic bucket
   - Result: 70-90% bucket match accuracy (vs 4% baseline)
5. **Multi-GPU Dataset Generation**
   - 12 RTX 4070 SUPER GPUs in parallel
   - 32,128 audio samples (2008 × 8 voices × 2 augmentations)
   - XTTS-v2 for realistic voice cloning
   - Worker-level state persistence with resume
6. **ESP32-Optimized CNN Encoder**
   - Small CNN: 288KB TFLite (vs YAMNet 3.5MB)
   - Mel-spectrogram: 64 bins × 96 frames
   - ESP-DSP FFT acceleration
7. **SIMD Similarity Search**
   - 256 embeddings × 256 dimensions = 262KB database
   - `dsps_dotprod_f32_aes3()` for cosine similarity
   - Top-K tracking with insertion sort

#### Key Files Referenced
- `local_llm_badge/src/ml/audio_embed.cpp` - Mel-spectrogram + CNN inference
- `local_llm_badge/src/ml/embed_search.cpp` - SIMD similarity search
- `workbench/working_protos/04_yamnet_audio_embedding/` - YAMNet prototype
- `workbench/tests/audio_embedding_dataset_ipynb/` - Dataset generation
- `workbench/tests/audio_semantic_xtts_ipynb/` - XTTS synthesis pipeline

#### Code Highlights
```cpp
// ESP-DSP SIMD cosine similarity (5-10x faster than scalar)
float _cosineSimilarity(const float* a, const float* b) {
    float dot = 0.0f, normA = 0.0f, normB = 0.0f;
    dsps_dotprod_f32_aes3(a, b, &dot, AUDIO_EMBEDDING_DIM);
    dsps_dotprod_f32_aes3(a, a, &normA, AUDIO_EMBEDDING_DIM);
    dsps_dotprod_f32_aes3(b, b, &normB, AUDIO_EMBEDDING_DIM);
    return dot / (sqrtf(normA) * sqrtf(normB));
}

// Triangular mel filterbank with precomputed weights
for (int m = 0; m < AUDIO_MEL_BINS; m++) {
    dsps_dotprod_f32_aes3(&_melFilterbank[m * numFreqBins], powerSpectrum, &sum, numFreqBins);
    melOutput[m] = log10f(sum + 1e-10f);
}
```

#### Conversation History References
- `.specstory/history/2025-11-27_*` - Embedding model discussions
- `.specstory/history/2025-11-28_*` - Training pipeline development

---

### Blog 5: Fine-Tuned LLM with PIE Assembly - 100x Faster Matmul
**series_order: 5**
**Estimated Length**: 4000-4500 words

#### Narrative Arc
"The previous blog covered basic LLM inference. This one covers what changed: Q8_0 quantization, inline ESP32-S3 PIE assembly achieving 16 int8 MACs per cycle, and a three-phase training pipeline that taught a 6MB model to answer 2,008 questions about a person."

#### Topics
1. **What Changed Since Last Time**
   - Previous: Stories260K/15M with ESP-DSP float SIMD
   - New: Fine-tuned Q8_0 model with PIE int8 assembly
   - Performance: 2-3x faster inference
2. **Q8_0 Quantization Deep Dive**
   - Per-group scaling (group size = 64)
   - 8-bit weights, float32 activations
   - Max quantization error tracking
   - 3.5-4x compression ratio
3. **PIE Vector Unit Assembly** (THE FLAGSHIP SECTION)
   - ESP32-S3 PIE instruction set
   - 128-bit vector registers (q0-q7)
   - 40-bit accumulator prevents overflow
   - Zero-overhead hardware loops
   ```asm
   "ee.vld.128.ip q0, %[a], 16\n"    // Load 16 int8s from a[]
   "ee.vld.128.ip q1, %[b], 16\n"    // Load 16 int8s from b[]
   "ee.vmulas.s8.accx q0, q1\n"      // 16 MACs in one instruction!
   ```
4. **Three-Phase Training Pipeline**
   - Phase 1: TinyStories (200K stories, language structure)
   - Phase 2: GPT-generated profile facts (242 Q&A from prose)
   - Phase 3: Q&A memorization (2,008 × 50 repetitions)
   - Learning rate scheduling: 3e-4 → 1e-4 → 5e-5
5. **Model Scaling Options**
   - 1M params: 1.07MB int8 (fits with everything)
   - 3M params: 2.82MB int8 (good balance)
   - 6M params: 5.98MB int8 (best quality, tight fit)
6. **SAM TTS Integration** (Simpler section)
   - ESP32-SAM library by pschatzmann
   - Custom I2S output adapter
   - Voice parameter tuning (speed, pitch, throat, mouth)
   - Why we didn't spend much time: it's a working library

#### Key Files Referenced
- `local_llm_badge/src/ml/llm_core.cpp` - **PIE assembly matmul**
- `local_llm_badge/src/ml/llm_inference.cpp` - Token generation
- `local_llm_badge/src/ml/sampler.cpp` - Top-p nucleus sampling
- `local_llm_badge/src/tts/robot_tts.cpp` - SAM integration
- `workbench/working_protos/05_llm_finetuned/` - Fine-tuned prototype
- `workbench/tests/llm_qa_scaled_training/` - Training notebook

#### Code Highlights (The Assembly Section)
```cpp
// PIE SIMD: 16 int8 MACs per iteration
static inline int32_t dotprod_s8_simd(const int8_t* a, const int8_t* b, int len) {
    int32_t result;
    int chunks = len >> 4;  // len / 16

    asm volatile(
        "wur.accx_0 %[zero]\n"            // Clear accumulator low
        "wur.accx_1 %[zero]\n"            // Clear accumulator high

        "loopnez %[chunks], 1f\n"         // Hardware loop (zero overhead)

        "ee.vld.128.ip q0, %[a], 16\n"    // Load 16 bytes from a[], auto-increment
        "ee.vld.128.ip q1, %[b], 16\n"    // Load 16 bytes from b[], auto-increment
        "ee.vmulas.s8.accx q0, q1\n"      // accx += sum(q0[i] * q1[i]) for i=0..15

        "1:\n"
        "rur.accx_0 %[result]\n"          // Read result from accumulator

        : [result] "=r" (result), [a] "+r" (a), [b] "+r" (b)
        : [chunks] "r" (chunks), [zero] "r" (0)
        : "memory"
    );
    return result;
}

// Quantized matmul using PIE SIMD
void matmul_q8(float* xout, QuantizedTensor* x, QuantizedTensor* w, int n, int d) {
    for (int i = 0; i < d; i++) {
        float val = 0.0f;
        for (int j = 0; j <= n - GS; j += GS) {
            int32_t ival = dotprod_s8_simd(x->q + j, w->q + i*n + j, GS);
            val += (float)ival * w->s[(i*n + j) / GS] * x->s[j / GS];  // Post-scale
        }
        xout[i] = val;
    }
}
```

#### Conversation History References
- `.specstory/history/2025-11-27_22-35-20Z-so-i-have-some.md` - Training discussions
- `.specstory/history/2025-11-30_*` - Assembly optimization work

---

### Blog 6: Putting It All Together - The Complete Badge Flow
**series_order: 6**
**Estimated Length**: 2500-3000 words

#### Narrative Arc
"Five separate systems. One badge. Here's how we orchestrated wake word detection, audio embeddings, LLM inference, and TTS into a seamless voice-activated experience—all within 8MB of RAM."

#### Topics
1. **The State Machine**
   - BOOT → LOGO_LOOP ↔ DEEP_SLEEP
   - Wake word triggers: RECORDING → EMBEDDING → SIMILARITY
   - High confidence: LLM_INFERENCE → TTS_OUTPUT
   - Low confidence: TTS_SORRY → STASH_DATA
2. **The Memory Dance** (Critical Section)
   - Problem: Video (1.2MB) + Embeddings (2MB) + LLM (6MB) > 8MB
   - Solution: Sequential load/unload
   ```
   LOGO_LOOP: Video + Embeddings resident (3.2MB)
   LLM_INFERENCE: Unload video + embeddings → Load LLM (6MB)
   After LLM: Unload LLM → Reload video + embeddings
   ```
3. **Configuration System**
   - Runtime JSON from SD card
   - Threshold tuning without recompile
   - Model paths, temperature, top-p
4. **Visual Feedback**
   - RED screen during recording
   - GREEN text overlay for responses
   - Logo resumes after interaction
5. **Hardware Mode Switches**
   - BTN1 bridged: Voice disabled, logo only
   - BTN2 bridged: Deep sleep mode
6. **Performance Timeline**
   - Wake word detection: 50ms per frame
   - Recording: 5 seconds
   - Embedding extraction: ~200ms
   - Similarity search: ~100ms
   - LLM (128 tokens): 60-90 seconds
   - TTS: 2-4 seconds

#### Key Files Referenced
- `local_llm_badge/local_llm_badge.ino` - Main state machine
- `local_llm_badge/config.h` - Pin definitions, constants
- `workbench/docs/OAISYS_BADGE_PLAN.md` - Architecture design

#### Code Highlights
```cpp
// Memory swap for LLM inference
void enterLLMState() {
    embedSearch.end();           // Free 2MB (embeddings)
    free(mlPool); mlPool = nullptr;  // Free 1MB (ML pool)
    video.unloadVideo();         // Free 1.2MB (video buffer)
    // Total freed: 4.2MB → Load 6MB LLM
    llmInference.begin(nullptr, 0, LLM_MODEL_PATH, TOKENIZER_PATH);
}

void exitLLMState() {
    llmInference.end();          // Free LLM
    video.reloadVideo();         // Restore from SD
    mlPool = (uint8_t*)ps_malloc(ML_POOL_SIZE);
    embedSearch.begin(EMBEDDINGS_PATH, INTENTS_PATH);
}
```

#### Backlinks to Other Posts
- "As covered in [Blog 3: Wake Word Detection](#), the 'Hey Daisy' trigger..."
- "The embedding approach from [Blog 4: Intent Embeddings](#) gives us..."
- "Using the PIE assembly optimizations from [Blog 5: Fine-Tuned LLM](#)..."
- "Building on the video system from [Blog 1: Video Badge](#)..."

#### Conversation History References
- `.specstory/history/2025-11-30_04-59-55Z-this-session-is-being.md` - Integration work
- `.specstory/history/2025-12-01_*` - Final assembly

---

## Series Metadata

### Hugo Front Matter Template
```yaml
series_id: "esp32-oaisys25-badge"
series_name: "Project 'Tiny Haze' - An ESP32 powered Digital Badge"
series_order: N  # 3, 4, 5, or 6 for new posts
```

### Cross-Linking Strategy
- Each blog references specific file paths from `badge/` repo
- Blog 6 heavily backlinks to all previous posts
- Code snippets include line numbers where relevant

### Repository References
All paths relative to `badge/`:
- Main firmware: `local_llm_badge/`
- Prototypes: `workbench/working_protos/`
- Training: `workbench/tests/`
- Documentation: `workbench/docs/`

### Estimated Total New Content
~13,000-15,000 words across 4 new posts

### Publication Order
1. Blog 3: Wake Word (foundation for voice interaction)
2. Blog 4: Embeddings (explains the STT alternative)
3. Blog 5: LLM + TTS (the response generation)
4. Blog 6: Integration (ties everything together with backlinks)
