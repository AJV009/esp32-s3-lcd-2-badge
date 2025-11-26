# microWakeWord Pretrained Test

Tests wake word detection using microWakeWord TFLite models with EdgeNeuron library.

**Supported wake words:**
- "Hey Daisy" (custom trained) - **default**
- "Hey Jarvis" (pretrained)

## How It Works

1. **Audio capture**: INMP441 microphone at 16kHz, 16-bit mono
2. **Feature extraction**: Microfrontend generates 40-bin spectrogram features
3. **Inference**: EdgeNeuron runs TFLite model every 10ms
4. **Detection**: Sliding window averages probabilities, triggers at threshold

## Setup

### 1. Install EdgeNeuron Library

Via Arduino Library Manager, search for "EdgeNeuron" by Consentium IoT.

Or install from: https://github.com/ConsentiumIoT/EdgeNeuron

### 2. Copy Model to SD Card

Create this folder structure on your SD card:
```
SD Card/
└── models/
    └── hey_daisy.tflite    (or hey_jarvis.tflite)
```

### 3. Select Wake Word (optional)

Edit the sketch to select your model:
```cpp
// Uncomment ONE of these:
// #define MODEL_HEY_JARVIS    // Pretrained "Hey Jarvis" model
#define MODEL_HEY_DAISY     // Custom trained "Hey Daisy" model
```

### 4. Board Settings

- Board: ESP32S3 Dev Module
- PSRAM: OPI PSRAM
- Flash Size: 16MB

### 5. Upload and Test

1. Insert SD card with model
2. Upload sketch
3. Open Serial Monitor (115200 baud)
4. Say "Hey Daisy" - LED lights up on detection

## Expected Output

```
=== microWakeWord Test ===
Wake word: "Hey Daisy"
Model: /models/hey_daisy.tflite

SD Card: OK
Model loaded: 51234 bytes
TFLite: OK
  Input: dims=[1,1,40], type=9
  Output: dims=[1,1], type=9
Microphone: OK
Frontend: OK

Listening for "Hey Daisy"...
================================

prob: 0.012, avg: 0.008
prob: 0.867, avg: 0.512
prob: 0.994, avg: 0.871

>>> HEY DAISY DETECTED! <<<
```

## Model Parameters

- **probability_cutoff**: 0.5 (50% confidence)
- **sliding_window_size**: 10 frames

## Troubleshooting

### "Model load failed"
- Check SD card has `/models/hey_daisy.tflite`
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
