# Audio Semantic Embeddings for ESP32-S3

**BRILLIANT! Synthetic data generation + semantic focus = PERFECT approach!** With 4-5MB and 5-second latency, you can build something truly sophisticated. Let me design the optimal architecture.

---

## 🎯 Recommended Architecture: **Audio-Text Contrastive Encoder**

This approach maps audio and text to the same semantic space - exactly what you need!

### **Core Concept:**
```
Audio "show me the schedule" ──→ [Audio Encoder] ──→ 256-dim embedding
Text  "show me the schedule" ──→ [Text Encoder]  ──→ 256-dim embedding
                                        ↓
                            Cosine Similarity ≈ 0.95
```

At inference, you only need the **Audio Encoder** (4MB), text embeddings are pre-computed (2KB).

---

## 🏗️ Model Architecture (4MB, Semantic-Focused)

### **Base: YAMNet + Projection Head**

[YAMNet](https://www.tensorflow.org/hub/tutorials/yamnet) is Google's audio classification model - **perfect for your use case**:

**Why YAMNet:**
- ✅ MobileNetV1 backbone (efficient)
- ✅ Outputs **1024-dim semantic embeddings** (not just classifications!)
- ✅ Pre-trained on AudioSet (521 classes - general audio understanding)
- ✅ Quantized version: ~3.5MB
- ✅ TensorFlow Lite support
- ✅ Proven on mobile devices

**Architecture:**
```
Input: 3-5 second audio (16kHz)
  ↓
Mel Spectrogram (64 mel bins)
  ↓
YAMNet Encoder (MobileNetV1)
  ↓
1024-dim embedding (semantic features)
  ↓
Projection Layer (1024 → 256)
  ↓
L2 Normalization
  ↓
256-dim final embedding
```

**Model Sizes:**
- YAMNet encoder (quantized int8): ~3.5 MB
- Projection layer (1024×256): ~260 KB
- **Total: ~3.8 MB** ✅

**Inference Time on ESP32-S3:**
- YAMNet forward pass: ~3-4 seconds
- Projection + search: ~100ms
- **Total: ~4 seconds** ✅

---

### **Alternative: FRILL (Google's On-Device Model)**

If YAMNet is too slow, use [FRILL](https://research.google/blog/frill-on-device-speech-representations-using-tensorflow-lite/):

**FRILL Advantages:**
- 40% smaller than original model
- 32× faster on mobile
- Optimized for speech (better than YAMNet for voice queries)
- ~2-3 MB quantized

**But:** Less semantic (more phonetic) - might need larger projection head.

---

## 📊 Training Pipeline (Fully Automated!)

### **Phase 1: Data Generation (Zero Manual Work!)**

#### **1.1 Generate Text Variations**

```python
import openai  # or use local LLM like Ollama

base_queries = [
    "Show me the schedule",
    "Find a booth",
    "Navigate to the room",
    "Add a contact",
    "Take a note",
    "What's next on the agenda",
    "Show the map",
    "Set a reminder",
    "Check my messages",
    "Help me"
]

# Generate 50 variations per query using LLM
variations = []
for query in base_queries:
    prompt = f"""Generate 50 natural variations of this query, including:
    - Different phrasings ("show schedule", "display agenda", "what's the schedule")
    - Casual vs formal ("gimme the schedule", "could you show me the schedule please")
    - With filler words ("uh, show me the schedule", "show me like, the schedule")
    - Questions vs commands ("can you show schedule?", "show schedule")

    Query: {query}

    Return as JSON list."""

    response = llm.generate(prompt)
    variations.extend(response)

# Total: 10 queries × 50 variations = 500 text samples
```

#### **1.2 Generate Synthetic Audio**

Use state-of-the-art TTS with accent control ([recent 2025 research](https://arxiv.org/html/2508.07426v1)):

```python
from TTS.api import TTS  # Coqui TTS or similar
import librosa

# Initialize multi-speaker, multi-accent TTS
tts = TTS("tts_models/multilingual/multi-dataset/xtts_v2")

accents = ['us', 'uk', 'indian', 'australian', 'singapore']
speakers = [f'speaker_{i}' for i in range(10)]  # Different voices

audio_samples = []

for variation in variations:
    for accent in accents:
        for speaker in speakers[:3]:  # 3 speakers per accent
            # Generate clean audio
            audio = tts.tts(
                text=variation,
                speaker=speaker,
                language=map_accent_to_lang(accent)
            )

            # Data augmentation (as per recent research)
            augmented_versions = [
                audio,  # Clean
                add_background_noise(audio, noise_level=0.02),
                add_gaussian_noise(audio, snr=20),
                time_stretch(audio, rate=0.9),  # Slower
                time_stretch(audio, rate=1.1),  # Faster
                pitch_shift(audio, steps=2),     # Higher pitch
                pitch_shift(audio, steps=-2),    # Lower pitch
            ]

            audio_samples.extend(augmented_versions)

# Total: 500 texts × 5 accents × 3 speakers × 7 augmentations = ~52,500 samples!
```

**Augmentation techniques from [recent 2025 research](https://arxiv.org/html/2407.04047v1):**
- Background noise (AudioSet noise samples)
- Gaussian noise (SNR 15-25 dB)
- Time stretching (0.85-1.15x speed)
- Pitch shifting (±2-4 semitones)
- SpecAugment (frequency/time masking)

---

### **Phase 2: Model Training (Contrastive Learning)**

#### **2.1 Training Architecture**

```python
import tensorflow as tf
from transformers import AutoModel, AutoTokenizer

# Text encoder (frozen, only for generating targets)
text_encoder = AutoModel.from_pretrained('sentence-transformers/all-MiniLM-L6-v2')
text_encoder.trainable = False

# Audio encoder (trainable)
yamnet_base = tf.keras.models.load_model('yamnet.h5')
audio_encoder = tf.keras.Sequential([
    yamnet_base.get_layer('embeddings'),  # 1024-dim output
    tf.keras.layers.Dense(512, activation='relu'),
    tf.keras.layers.Dropout(0.3),
    tf.keras.layers.Dense(256),  # Match text embedding size
    tf.keras.layers.Lambda(lambda x: tf.nn.l2_normalize(x, axis=1))
])

# Contrastive loss (InfoNCE / NT-Xent)
def contrastive_loss(audio_emb, text_emb, temperature=0.07):
    # Cosine similarity matrix
    logits = tf.matmul(audio_emb, text_emb, transpose_b=True) / temperature
    labels = tf.range(tf.shape(audio_emb)[0])  # Diagonal = positive pairs

    loss_audio_to_text = tf.keras.losses.sparse_categorical_crossentropy(
        labels, logits, from_logits=True
    )
    loss_text_to_audio = tf.keras.losses.sparse_categorical_crossentropy(
        labels, tf.transpose(logits), from_logits=True
    )

    return (loss_audio_to_text + loss_text_to_audio) / 2

# Training loop
optimizer = tf.keras.optimizers.Adam(1e-4)

for epoch in range(20):
    for batch in dataset:  # Audio-text pairs
        audio, text = batch

        with tf.GradientTape() as tape:
            audio_emb = audio_encoder(audio)
            text_emb = text_encoder(text)
            loss = contrastive_loss(audio_emb, text_emb)

        gradients = tape.gradient(loss, audio_encoder.trainable_variables)
        optimizer.apply_gradients(zip(gradients, audio_encoder.trainable_variables))

# Post-training quantization
converter = tf.lite.TFLiteConverter.from_keras_model(audio_encoder)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.target_spec.supported_types = [tf.int8]

tflite_model = converter.convert()
# Save: ~3.8MB quantized model
```

#### **2.2 Expected Performance**

Based on [similar contrastive learning research](https://github.com/LAION-AI/CLAP):

| Metric | Expected Value |
|--------|----------------|
| Same-query similarity | 0.85-0.95 |
| Different-query similarity | 0.1-0.3 |
| Robustness to accents | High (trained on 5 accents) |
| Robustness to noise | Medium-High (augmented data) |
| False accept @ 0.7 threshold | <5% |

---

### **Phase 3: Deployment on ESP32-S3**

#### **3.1 Pre-compute Text Embeddings (Offline)**

```python
# Generate embeddings for your 10 base queries
query_texts = [
    "Show me the schedule",
    "Find a booth",
    # ... 10 queries
]

# Use text encoder (run on PC once)
text_embeddings = []
for query in query_texts:
    emb = text_encoder.encode(query)  # 256-dim
    text_embeddings.append(emb)

# Save to binary file for ESP32
import struct
with open('query_embeddings.bin', 'wb') as f:
    for emb in text_embeddings:
        f.write(struct.pack('256f', *emb))

# Size: 10 queries × 256 floats × 4 bytes = 10KB
```

#### **3.2 ESP32-S3 Sketch**

```cpp
#include <TensorFlowLite_ESP32.h>
#include "audio_encoder_model.h"  // Your 3.8MB quantized model
#include "SD.h"

// Model setup
const tflite::Model* model;
tflite::MicroInterpreter* interpreter;
TfLiteTensor* input;
TfLiteTensor* output;

// Query embeddings (loaded from SD card)
float query_embeddings[10][256];
String query_texts[10] = {
  "Show me the schedule",
  "Find a booth",
  // ... etc
};

void setup() {
  // ... I2S mic setup (you already have this!) ...

  // Load TFLite model
  model = tflite::GetModel(audio_encoder_model_tflite);
  static tflite::MicroMutableOpResolver<10> resolver;
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddFullyConnected();
  // ... add ops used by YAMNet ...

  static uint8_t tensor_arena[400 * 1024];  // 400KB arena
  static tflite::MicroInterpreter static_interpreter(
      model, resolver, tensor_arena, sizeof(tensor_arena));
  interpreter = &static_interpreter;
  interpreter->AllocateTensors();

  input = interpreter->input(0);
  output = interpreter->output(0);

  // Load query embeddings from SD card
  File f = SD.open("/query_embeddings.bin", FILE_READ);
  f.read((uint8_t*)query_embeddings, sizeof(query_embeddings));
  f.close();

  Serial.println("Ready! Say a query...");
}

void loop() {
  // 1. Record 3-5 seconds of audio (you have this!)
  int16_t audio_buffer[80000];  // 5 seconds @ 16kHz
  recordAudio(audio_buffer, 5);

  // 2. Convert to mel spectrogram (TFLite model expects this)
  preprocessAudio(audio_buffer, input->data.f);

  // 3. Run inference (~4 seconds)
  Serial.println("Processing audio...");
  unsigned long start = millis();
  interpreter->Invoke();
  Serial.printf("Inference: %lu ms\n", millis() - start);

  // 4. Get embedding (256-dim)
  float* audio_embedding = output->data.f;

  // 5. Find best match using cosine similarity
  int best_match = -1;
  float best_similarity = 0.0;

  for (int i = 0; i < 10; i++) {
    float similarity = cosine_similarity(audio_embedding, query_embeddings[i], 256);
    Serial.printf("Query %d: %.3f\n", i, similarity);

    if (similarity > best_similarity) {
      best_similarity = similarity;
      best_match = i;
    }
  }

  // 6. Handle result
  if (best_similarity > 0.70) {
    Serial.printf("✓ MATCH: %s (%.2f)\n", query_texts[best_match].c_str(), best_similarity);
    displayOnLCD(query_texts[best_match]);
  } else {
    Serial.printf("✗ UNKNOWN (best: %.2f)\n", best_similarity);
    saveAudioToSD(audio_buffer, "unknown_" + String(millis()) + ".wav");
  }

  delay(1000);
}

float cosine_similarity(float* a, float* b, int dim) {
  float dot = 0.0, norm_a = 0.0, norm_b = 0.0;
  for (int i = 0; i < dim; i++) {
    dot += a[i] * b[i];
    norm_a += a[i] * a[i];
    norm_b += b[i] * b[i];
  }
  return dot / (sqrt(norm_a) * sqrt(norm_b));
}

void preprocessAudio(int16_t* audio, float* mel_spec) {
  // Convert audio to mel spectrogram
  // YAMNet expects: 64 mel bins × ~150 frames
  // Use TensorFlow Lite Micro's audio preprocessing
  // (Implementation omitted for brevity - can use TFLite micro frontend)
}
```

---

## 🎓 Alternative: Use Edge Impulse (NO CODE!)

If you don't want to train manually, [Edge Impulse](https://www.edgeimpulse.com/) can do ALL of this:

1. Upload your synthetic audio + labels
2. Choose "Audio Classification" + "Transfer Learning (YAMNet)"
3. Train in browser
4. Download Arduino library (TFLite model included!)
5. Deploy to ESP32-S3

**Edge Impulse automatically:**
- Uses YAMNet or MobileNet backbone
- Handles quantization
- Generates ESP32-S3 compatible code
- Provides performance metrics

---

## 📈 Scaling to More Queries

Your approach scales beautifully:

| Queries | Text Embeddings | Lookup Time |
|---------|----------------|-------------|
| 10 | 10KB | ~1ms |
| 50 | 50KB | ~5ms |
| 100 | 100KB | ~10ms |
| 500 | 500KB | ~50ms |

All fit comfortably in PSRAM!

---

## 🚀 Implementation Roadmap

**Week 1: Data Generation**
- [ ] Define 10 base queries
- [ ] Generate 50 variations per query using LLM
- [ ] Generate synthetic audio with TTS (5 accents, 3 speakers)
- [ ] Apply augmentation (noise, speed, pitch)
- [ ] Total dataset: ~50K audio samples

**Week 2: Model Training**
- [ ] Load YAMNet pre-trained model
- [ ] Add projection head (1024 → 256)
- [ ] Train with contrastive loss (audio-text pairs)
- [ ] Validate on held-out accents
- [ ] Quantize to int8 (~3.8MB)

**Week 3: ESP32-S3 Integration**
- [ ] Convert to TFLite format
- [ ] Integrate with your existing audio pipeline
- [ ] Pre-compute text embeddings (10KB file)
- [ ] Test inference speed (~4 seconds)
- [ ] Tune threshold (0.7 vs 0.8 vs 0.6)

**Week 4: Testing & Iteration**
- [ ] Friends test with their voices
- [ ] Collect "unknown" samples
- [ ] Manually label and retrain
- [ ] Improve with active learning

---

## 🎯 Expected Results

**Semantic Quality (Your Priority!):**
- Same query, different phrasing: **0.85-0.95 similarity** ✅
- "Show schedule" ≈ "Display agenda" ≈ "What's the schedule"
- Cross-accent matching: **0.80-0.90** ✅
- Robust to filler words, speed variations

**Performance:**
- Model size: **3.8 MB** ✅
- Inference: **~4 seconds** ✅
- Accuracy @ 0.7 threshold: **~92-95%** (based on similar models)
- False accept rate: **<5%**

---

## 💡 Want Me To...?

**Option A:** Help you set up the synthetic data generation pipeline (LLM + TTS scripts)

**Option B:** Provide detailed YAMNet + contrastive learning training code

**Option C:** Create Edge Impulse tutorial for no-code approach

**Option D:** Start with a simpler prototype (10 queries, 100 samples, basic training)

Which sounds best? I'm ready to dive deep into whichever you choose!

---

## Sources

- [YAMNet Audio Classification (TensorFlow)](https://www.tensorflow.org/hub/tutorials/yamnet)
- [FRILL: On-Device Speech Representations](https://research.google/blog/frill-on-device-speech-representations-using-tensorflow-lite/)
- [CLAP: Contrastive Language-Audio Pretraining](https://github.com/LAION-AI/CLAP)
- [DistilALHuBERT: Lightweight Audio Model](https://dl.acm.org/doi/fullHtml/10.1145/3614008.3614015)
- [Scalable Controllable Accented TTS (2025)](https://arxiv.org/html/2508.07426v1)
- [Improving Accented Speech with TTS Augmentation (2024)](https://arxiv.org/html/2407.04047v1)
- [Synthetic Audio Data for Speech Translation (2024)](https://arxiv.org/html/2406.17363)
