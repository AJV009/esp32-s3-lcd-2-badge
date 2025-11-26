# microWakeWord Pretrained Test

Tests wake word detection using pre-trained microWakeWord TFLite models with EdgeNeuron library.

**Wake word: "Hey Jarvis"**

## How It Works

1. **Audio capture**: INMP441 microphone at 16kHz, 16-bit mono
2. **Feature extraction**: Microfrontend generates 40-bin spectrogram features
3. **Inference**: EdgeNeuron runs TFLite model every 10ms
4. **Detection**: Sliding window averages probabilities, triggers at 97% threshold

## Setup

### 1. Install EdgeNeuron Library

Via Arduino Library Manager, search for "EdgeNeuron" by Consentium IoT.

Or install from: https://github.com/ConsentiumIoT/EdgeNeuron

### 2. Copy Model to SD Card

Create this folder structure on your SD card:
```
SD Card/
└── models/
    └── hey_jarvis.tflite
```

Copy the model from:
```
_STASH_/micro-wake-word-models/models/v2/hey_jarvis.tflite
```

### 3. Board Settings

- Board: ESP32S3 Dev Module
- PSRAM: OPI PSRAM
- Flash Size: 16MB

### 4. Upload and Test

1. Insert SD card with model
2. Upload sketch
3. Open Serial Monitor (115200 baud)
4. Say "Hey Jarvis" - LED lights up on detection

## Expected Output

```
=== microWakeWord Test ===
Wake word: "Hey Jarvis"
Model: /models/hey_jarvis.tflite

SD Card: OK
Model loaded: 51234 bytes
TFLite: OK
  Input: dims=[1,1,40], type=9
  Output: dims=[1,1], type=9
Microphone: OK
Frontend: OK

Listening for "Hey Jarvis"...
================================

prob: 0.012, avg: 0.008
prob: 0.967, avg: 0.612
prob: 0.994, avg: 0.971

>>> HEY JARVIS DETECTED! <<<
```

## Model Parameters

**V1 models** (simpler, recommended for testing):
- **probability_cutoff**: 0.5 (50% confidence)
- **sliding_window_size**: 10 frames
- No streaming state - easier to use

**V2 models** (more accurate but complex):
- **probability_cutoff**: 0.97 (97% confidence)
- **sliding_window_size**: 5 frames
- Uses streaming state (VAR_HANDLE) - requires consistent 10ms inference timing

## Available Models

From [micro-wake-word-models](https://github.com/esphome/micro-wake-word-models):

| Version | Model | Wake Word | Threshold |
|---------|-------|-----------|-----------|
| V1 | models/hey_jarvis.tflite | "Hey Jarvis" | 50% |
| V1 | models/okay_nabu.tflite | "Okay Nabu" | 50% |
| V1 | models/alexa.tflite | "Alexa" | 50% |
| V2 | models/v2/hey_jarvis.tflite | "Hey Jarvis" | 97% |
| V2 | models/v2/alexa.tflite | "Alexa" | 97% |

**Recommendation:** Start with V1 models for testing, switch to V2 for production.

Copy model to `/models/hey_jarvis.tflite` on SD card.

## Troubleshooting

### "Model load failed"
- Check SD card has `/models/hey_jarvis.tflite`
- Try reformatting SD card as FAT32

### "TFLite init failed"
- Increase TENSOR_ARENA_SIZE if needed
- Check serial for specific error

### No detection / false positives
- Adjust PROBABILITY_CUTOFF (lower = more sensitive)
- Check microphone gain
- Speak clearly, closer to microphone

## Sources

- [microWakeWord](https://github.com/kahrendt/microWakeWord)
- [micro-wake-word-models](https://github.com/esphome/micro-wake-word-models)
- [EdgeNeuron](https://github.com/ConsentiumIoT/EdgeNeuron)
