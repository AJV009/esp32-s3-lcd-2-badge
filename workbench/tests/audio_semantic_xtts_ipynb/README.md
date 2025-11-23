# Audio Semantic Model - Fine-Tuning Resources

This directory contains documentation and tools for fine-tuning the audio-text semantic embedding model.

---

## 📁 Files

| File | Purpose |
|------|---------|
| `FINE_TUNING_GUIDE.md` | Complete guide covering all fine-tuning strategies |
| `QUICK_REFERENCE.md` | Quick lookup for common tasks and code snippets |
| `finetune_example.py` | Standalone script for fine-tuning with real voices |
| `labels_template.csv` | Template for labeling real voice recordings |
| `README.md` | This file |

---

## 🚀 Quick Start

### Option 1: Use the Notebook (Recommended)

1. Open `audio_semantic_xtts.ipynb` in Google Colab
2. Run all cells to generate baseline model
3. Record real voices (see below)
4. Add new cell to fine-tune:

```python
# Load existing model
audio_projection = tf.keras.models.load_model('models_xtts/audio_projection.h5')

# Load real recordings and fine-tune (see FINE_TUNING_GUIDE.md)
model.fit(real_dataset, epochs=100)
```

### Option 2: Use the Script

1. Prepare real voice recordings:
```bash
real_recordings/
├── labels.csv              # Use labels_template.csv as guide
├── person1_schedule.wav
├── person1_booth.wav
├── person2_schedule.wav
└── ...
```

2. Run fine-tuning:
```bash
python finetune_example.py \
    --real-voices real_recordings/ \
    --epochs 100 \
    --convert-tflite
```

3. Deploy to ESP32:
```bash
# Upload models_xtts/audio_projection_finetuned.tflite
```

---

## 📝 Recording Real Voices

### Requirements
- **Equipment:** Phone or laptop microphone
- **Format:** WAV, 16kHz, mono
- **Duration:** 2-5 seconds per recording
- **People:** 5-10 diverse (age, gender, accent)
- **Per person:** 1-2 variations per query

### Recording Tips
1. **Environment:** Quiet room (some background noise OK)
2. **Distance:** 20-30 cm from microphone
3. **Volume:** Speak naturally (not too loud/quiet)
4. **Clarity:** Enunciate clearly but naturally
5. **Variations:** Say each query slightly differently

### Using Phone to Record
```bash
# iOS: Voice Memos app → Export as WAV
# Android: Use "Easy Voice Recorder" → Export as WAV

# Convert M4A to WAV (if needed)
ffmpeg -i recording.m4a -ar 16000 -ac 1 recording.wav
```

### Using Python to Record
```python
import sounddevice as sd
import soundfile as sf

# Record 5 seconds
fs = 16000
duration = 5
audio = sd.rec(int(duration * fs), samplerate=fs, channels=1)
sd.wait()

# Save
sf.write('recording.wav', audio, fs)
```

---

## 📊 Labels Format

Create `labels.csv` in your recordings directory:

```csv
filename,query_idx,text
person1_schedule.wav,0,"Show me the schedule"
person1_booth.wav,1,"Find a booth"
person1_room.wav,2,"Navigate to room"
person2_schedule.wav,0,"Display the schedule"
person2_booth.wav,1,"Where is the booth"
```

**Columns:**
- `filename`: Audio file name (in same directory)
- `query_idx`: Query index (0-9 for 10 queries)
- `text`: Exact text spoken (must match dataset variations)

**Query indices** (from `audio_semantic_xtts.ipynb`):
```
0: Show me the schedule
1: Find a booth
2: Navigate to room
3: Add a contact
4: Take a note
5: View the map
6: Check messages
7: See my profile
8: Join a session
9: Connect with someone
```

---

## 📈 Expected Results

| Strategy | Effort | Improvement |
|----------|--------|-------------|
| **Baseline (XTTS only)** | - | 24.5% pair, 100% base |
| **+ 50 real voices** | 2h | 60-70% accuracy |
| **+ 100 real voices** | 3h | 70-80% accuracy |
| **+ 200 real voices** | 4h | 80-90% accuracy |

**Note:** "Base query accuracy" is what matters for ESP32 (already 100%!)

---

## 🛠️ Tools & Scripts

### Check Audio Quality
```python
import librosa
import numpy as np

audio, sr = librosa.load('recording.wav')

# RMS level (should be 0.1-0.5)
rms = np.sqrt(np.mean(audio**2))
print(f"RMS: {rms:.3f}")

# Clipping (should be 0)
clipped = np.sum(np.abs(audio) > 0.99)
print(f"Clipped samples: {clipped}")

# Duration
duration = len(audio) / sr
print(f"Duration: {duration:.2f} sec")
```

### Batch Convert Audio
```bash
# Convert all M4A to WAV
for f in *.m4a; do
    ffmpeg -i "$f" -ar 16000 -ac 1 "${f%.m4a}.wav"
done

# Normalize volume
for f in *.wav; do
    ffmpeg -i "$f" -filter:a "volume=1.5" -ar 16000 "norm_$f"
done
```

### Generate Labels CSV
```python
import os
import csv

# Mapping of keywords to query indices
query_map = {
    'schedule': 0,
    'booth': 1,
    'room': 2,
    'contact': 3,
    'note': 4,
    'map': 5,
    'message': 6,
    'profile': 7,
    'session': 8,
    'connect': 9
}

# Auto-generate labels from filenames
recordings_dir = 'real_recordings/'
labels = []

for filename in os.listdir(recordings_dir):
    if not filename.endswith('.wav'):
        continue

    # Extract query from filename (e.g., "alice_schedule.wav")
    for keyword, idx in query_map.items():
        if keyword in filename.lower():
            labels.append({
                'filename': filename,
                'query_idx': idx,
                'text': f'Automatically detected: {keyword}'
            })
            break

# Save to CSV
with open('labels.csv', 'w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=['filename', 'query_idx', 'text'])
    writer.writeheader()
    writer.writerows(labels)

print(f"Generated labels for {len(labels)} recordings")
```

---

## 🎯 Recommended Workflow

### Phase 1: Quick Validation (1 Day)
1. ✅ Record 5 people × 10 queries = 50 samples
2. ✅ Create `labels.csv`
3. ✅ Run `finetune_example.py --epochs 100`
4. ✅ Test accuracy → Expect 60-70%
5. ✅ Deploy to ESP32 and test

### Phase 2: Production Polish (1 Week)
1. ✅ Record 20 people × 10 queries = 200 samples
2. ✅ Add data augmentation (noise, reverb)
3. ✅ Fine-tune 200 epochs
4. ✅ Test accuracy → Expect 80-90%
5. ✅ A/B test with users
6. ✅ Iterate on failure cases

### Phase 3: Continuous Improvement
1. ✅ Deploy with feedback mechanism
2. ✅ Collect user corrections weekly
3. ✅ Fine-tune incrementally
4. ✅ Gradual improvement to 95%+

---

## 📚 Additional Resources

**Documentation:**
- `FINE_TUNING_GUIDE.md` - Comprehensive strategies and code examples
- `QUICK_REFERENCE.md` - Quick lookup for common tasks
- `../README_xtts_upgrade.md` - Why XTTS-v2 is better than gTTS

**Notebook:**
- `../audio_semantic_xtts.ipynb` - Main training notebook (use in Colab)
- `../audio_semantic_poc.ipynb` - Original gTTS version (for reference)

**Models:**
- `../models_xtts/audio_projection.h5` - Base model (fine-tune this!)
- `../models_xtts/audio_projection_quantized.tflite` - For ESP32 deployment
- `../models_xtts/query_embeddings.bin` - Pre-computed query vectors

---

## ❓ FAQ

### Q: How many recordings do I need?
**A:** Minimum 50 (2-4 hours), recommended 200+ (1-2 days) for production.

### Q: What if I don't have 10 people?
**A:** 5 people is enough if they have diverse accents/ages. Quality > quantity.

### Q: Can I add new queries?
**A:** Yes! See "Strategy 4" in FINE_TUNING_GUIDE.md

### Q: Will it forget XTTS training?
**A:** Not if you use low learning rate (1e-4) and mix data 50:50.

### Q: Can ESP32 learn on-device?
**A:** Yes, theoretically! See "On-Device Learning" section in guide.

### Q: What's the best recording app?
**A:** iOS: Voice Memos, Android: Easy Voice Recorder, Desktop: Audacity

---

## 🐛 Troubleshooting

**Problem:** Low accuracy after fine-tuning
- ✅ Check audio quality (RMS, clipping)
- ✅ Use lower learning rate (1e-5 instead of 1e-4)
- ✅ Mix with XTTS data (--mix-with-xtts flag)

**Problem:** Model forgot XTTS knowledge
- ✅ Use very low learning rate (1e-5 or 1e-6)
- ✅ Train fewer epochs (50 instead of 100)
- ✅ Always mix real + XTTS data

**Problem:** Audio files have errors
- ✅ Convert to 16kHz mono: `ffmpeg -i input.wav -ar 16000 -ac 1 output.wav`
- ✅ Check duration: Minimum 1 second, recommended 2-5 seconds
- ✅ Normalize volume: Should not clip (max < 0.99)

---

## 📧 Support

For questions or issues:
1. Check `FINE_TUNING_GUIDE.md` for detailed examples
2. Review `QUICK_REFERENCE.md` for common solutions
3. Inspect `finetune_example.py` code for implementation details

---

**Happy fine-tuning! 🎤**
