# microWakeWord - Hey Daisy

Wake word detection using custom trained microWakeWord TFLite model with EdgeNeuron library.

**Wake word: "Hey Daisy"**

## How It Works

1. **Audio capture**: INMP441 microphone at 16kHz, 16-bit mono
2. **Feature extraction**: Microfrontend generates 40-bin spectrogram features
3. **Inference**: EdgeNeuron runs TFLite model every 10ms
4. **Detection**: Sliding window averages probabilities, triggers at threshold

## Setup

### 1. Install Libraries

**Required libraries** (via Arduino Library Manager):
- **EdgeNeuron** by Consentium IoT
- **ArduinoJson** by Benoit Blanchon

### 2. Copy Files to SD Card

```
SD Card/
└── models/
    ├── hey_daisy.tflite    # The trained model
    └── hey_daisy.json      # Configuration manifest
```

### 3. JSON Configuration

The `hey_daisy.json` file controls tunable detection parameters:

```json
{
  "type": "micro",
  "wake_word": "Hey Daisy",
  "model": "hey_daisy.tflite",
  "version": 1,
  "micro": {
    "probability_cutoff": 0.5,
    "sliding_window_average_size": 10,
    "min_high_frames": 6,
    "min_frame_prob": 0.5,
    "cooldown_ms": 1500
  }
}
```

**Parameters:**
| Parameter | Description | Default |
|-----------|-------------|---------|
| `probability_cutoff` | Average probability threshold to trigger | 0.5 |
| `sliding_window_average_size` | Number of frames to average | 10 |
| `min_high_frames` | Minimum frames above `min_frame_prob` | 6 |
| `min_frame_prob` | Per-frame probability threshold | 0.5 |
| `cooldown_ms` | Milliseconds to ignore after detection | 1500 |

### 4. Board Settings

- Board: ESP32S3 Dev Module
- PSRAM: OPI PSRAM
- Flash Size: 16MB

### 5. Upload and Test

1. Insert SD card with model and config
2. Upload sketch
3. Open Serial Monitor (115200 baud)
4. Say "Hey Daisy" - LED lights up on detection

## Expected Output

```
=== microWakeWord - Hey Daisy ===

SD Card: OK
Config loaded: /models/hey_daisy.json

Configuration:
  Wake word: "Hey Daisy"
  Probability cutoff: 0.50
  Sliding window size: 10
  Min high frames: 6
  Min frame prob: 0.50
  Cooldown: 1500 ms

Model loaded: 132848 bytes
TFLite: OK
  Arena used: 89432 bytes
  Input: dims=[1,3,40], type=9
  Output: dims=[1,1], type=9
Microphone: OK
Frontend: OK

Listening for "Hey Daisy"...
================================

prob: 0.012, avg: 0.008, high: 0/10
prob: 0.867, avg: 0.512, high: 7/10
prob: 0.994, avg: 0.871, high: 9/10

>>> Hey Daisy DETECTED! <<<
```

## Tuning Tips

**Too many false positives:**
- Increase `probability_cutoff` (try 0.7, 0.8, 0.9)
- Increase `min_high_frames` (try 8 or 10)
- Increase `min_frame_prob` (try 0.6 or 0.7)

**Missing detections:**
- Decrease `probability_cutoff` (try 0.3 or 0.4)
- Decrease `min_high_frames` (try 4 or 5)
- Speak closer to microphone

**Multiple triggers per wake word:**
- Increase `cooldown_ms` (try 2000 or 2500)

## Troubleshooting

### "Cannot open config" / "Config load failed"
- Check SD card has `/models/hey_daisy.json`
- Verify JSON syntax is valid

### "Model load failed"
- Check SD card has `/models/hey_daisy.tflite`
- Try reformatting SD card as FAT32

### "TFLite init failed"
- Model may need more memory - check serial for specific error
- TENSOR_ARENA_SIZE is set to 500KB which should be sufficient

## Sources

- [microWakeWord](https://github.com/kahrendt/microWakeWord)
- [EdgeNeuron](https://github.com/ConsentiumIoT/EdgeNeuron)
- [ArduinoJson](https://arduinojson.org/)
