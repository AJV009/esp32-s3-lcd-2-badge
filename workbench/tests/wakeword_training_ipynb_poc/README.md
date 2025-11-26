# microWakeWord Training Notebook

Train custom wake word models compatible with ESP32-S3 using the microWakeWord framework.

## Overview

This notebook trains models that work with `wakeword_pretrained_test.ino`:
- **Input**: 40-bin log-mel spectrogram (30ms window, 10ms stride)
- **Architecture**: MixNet streaming model
- **Output**: Quantized int8 TFLite model

## Training Approach

Based on [microWakeWord](https://github.com/kahrendt/microWakeWord):

1. **Synthetic sample generation** - Piper TTS creates diverse voice samples
2. **Data augmentation** - Background noise, reverb, pitch shift, EQ
3. **Negative sampling** - Speech and noise datasets prevent false accepts
4. **Streaming conversion** - Model runs frame-by-frame on ESP32

## Quick Start

1. Open `wakeword_training.ipynb` in Google Colab
2. Wake word is pre-configured as "Hey Daisy" in Cell 4:
   ```python
   WAKE_WORD = "Hey Daisy"
   PHONETIC_VARIATIONS = ["Hey Daisy", "hey daisy", "hey day zee", ...]
   ```
3. Run all cells (~30-60 minutes with GPU)
4. Download `hey_daisy.tflite`
5. Copy to SD card at `/models/`

## Requirements

- Google Colab with GPU runtime (recommended)
- ~5GB disk space for datasets
- Internet connection for downloads

## Output Files

- `{wake_word}.tflite` - Quantized model for ESP32
- `{wake_word}.json` - Model manifest with parameters

## Deployment

1. Copy `.tflite` to SD card: `/models/hey_daisy.tflite`
2. Update sketch:
   ```cpp
   #define MODEL_PATH "/models/hey_daisy.tflite"
   ```
3. Adjust detection parameters if needed:
   ```cpp
   #define PROBABILITY_CUTOFF  0.5f
   #define SLIDING_WINDOW_SIZE 10
   ```

## Tuning Tips

| Issue | Solution |
|-------|----------|
| Too many false positives | Increase `PROBABILITY_CUTOFF` |
| Too many false negatives | Decrease `PROBABILITY_CUTOFF` |
| Inconsistent detection | Add phonetic variations, retrain |
| Model too large | Reduce architecture size |

## Training Parameters

Key settings in the notebook:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `SAMPLES_PER_COMBO` | 50 | Samples per voice/variation |
| `training_steps` | 10000 | Total training iterations |
| `negative_class_weight` | 20 | Penalty for false accepts |
| `batch_size` | 128 | Training batch size |

## Sources

- [microWakeWord](https://github.com/kahrendt/microWakeWord) - Training framework
- [micro-wake-word-models](https://github.com/esphome/micro-wake-word-models) - Pretrained models
- [Piper TTS](https://github.com/rhasspy/piper) - Sample generation
