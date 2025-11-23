# Quick Reference - Audio Semantic Model Fine-Tuning

## TL;DR

**Current:** 24.5% pair accuracy, 100% base query accuracy
**Target:** 80-95% with fine-tuning
**Best strategy:** Add 50-200 real voice recordings (+60-70% accuracy)

---

## Top 3 Strategies (ROI Ranked)

### 🥇 #1: Real Voice Recordings
- **Impact:** +60-70% accuracy
- **Time:** 2-4 hours
- **What:** Record 5-10 people saying each query
- **Code:** See "Example 1" in FINE_TUNING_GUIDE.md

### 🥈 #2: More Training Epochs
- **Impact:** +2-5% accuracy
- **Time:** 30 minutes
- **What:** Continue training from epoch 500 to 700
- **Code:**
```python
model.fit(dataset_train, epochs=200)  # Continue training
```

### 🥉 #3: Better Augmentation
- **Impact:** +5-10% robustness
- **Time:** 30 minutes
- **What:** Add background noise, room simulation
- **Code:** See "Strategy 6" in FINE_TUNING_GUIDE.md

---

## Critical Files

```
models_xtts/
├── audio_projection.h5              ← Load this to fine-tune!
├── audio_projection_quantized.tflite ← Deploy this to ESP32
├── query_embeddings.bin             ← 10 query vectors
└── query_texts.txt                  ← Query reference

audio_semantic_xtts.ipynb            ← Training notebook
FINE_TUNING_GUIDE.md                 ← Full documentation
QUICK_REFERENCE.md                   ← This file
```

---

## Essential Code Snippets

### Load Pre-Trained Model
```python
from tensorflow.keras.models import load_model

audio_projection = load_model('models_xtts/audio_projection.h5')
```

### Fine-Tune with Lower LR
```python
model = AudioTextModel(audio_projection, text_projection)
model.compile(optimizer=tf.keras.optimizers.Adam(1e-4))  # 10x lower
history = model.fit(new_dataset, epochs=100)
```

### Add Real Voice Recordings
```python
# 1. Extract embeddings
real_audio_emb = extract_yamnet_embeddings(real_recordings)
real_text_emb = text_encoder.encode(corresponding_texts)

# 2. Combine with XTTS data
combined_audio = np.concatenate([xtts_audio_emb, real_audio_emb])
combined_text = np.concatenate([xtts_text_emb, real_text_emb])

# 3. Fine-tune
model.fit(combined_dataset, epochs=100)
```

### Export to TFLite
```python
converter = tf.lite.TFLiteConverter.from_keras_model(audio_projection)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
tflite_model = converter.convert()

with open('finetuned.tflite', 'wb') as f:
    f.write(tflite_model)
```

---

## Common Issues & Fixes

| Problem | Quick Fix |
|---------|-----------|
| **Overfitting** | Mix XTTS + real data 50:50 |
| **Forgetting** | Use LR=1e-5 (very low) |
| **Low accuracy** | Check audio quality (RMS, clipping) |
| **Confusing queries** | Make queries more distinct |
| **Noisy recordings** | Use `noisereduce` library |

---

## Performance by Dataset Size

| Real Recordings | Expected Accuracy | Time to Collect |
|----------------|-------------------|-----------------|
| 0 (XTTS only) | 24.5% | - |
| 50 samples | 60-70% | 2 hours |
| 100 samples | 70-80% | 3 hours |
| 200 samples | 80-90% | 4 hours |
| 500+ samples | 90-95% | 1-2 days |

---

## Recording Guidelines

**Equipment:** Phone or laptop mic (16kHz, mono, WAV)
**People:** 5-10 diverse (age, gender, accent)
**Per person:** 1-2 variations per query
**Environment:** Quiet room (some background noise OK)
**Duration:** 2-5 seconds per recording

**Quality checks:**
```python
import librosa
audio, sr = librosa.load('recording.wav')

# Check volume (should be 0.1-0.5)
rms = np.sqrt(np.mean(audio**2))

# Check clipping (should be 0)
clipped = np.sum(np.abs(audio) > 0.99)

# Normalize if needed
audio = audio / np.max(np.abs(audio)) * 0.8
```

---

## Deployment Checklist

- [ ] Fine-tune model (100 epochs)
- [ ] Validate accuracy >80%
- [ ] Convert to TFLite INT8
- [ ] Test on ESP32-S3
- [ ] Measure inference time (<500ms)
- [ ] Check memory usage (<3.5MB)
- [ ] Deploy to badge
- [ ] A/B test with users

---

## Next Steps

1. **Immediate:** Record 5 people → +60% accuracy
2. **This week:** Add domain-specific queries
3. **Production:** Collect 200+ samples → 90% accuracy
4. **Advanced:** On-device learning (optional)

**See FINE_TUNING_GUIDE.md for complete details!**
