# YAMNet-1024 Audio Embedding on ESP32-S3

Extract 1024-dimensional audio embeddings using YAMNet on ESP32-S3 with dual-core optimization.

## Overview

This sketch:
1. **Records 3-5 seconds** of audio from dual INMP441 microphones (downmixed to mono)
2. **Generates mel-spectrogram** (64 mels × 96 frames) using ESP-DSP accelerated FFT
3. **Runs YAMNet-1024 inference** with TensorFlow Lite Micro (dual-core optimized)
4. **Extracts 1024-D embeddings** from the model
5. **Saves to SD card** as JSON format

## Hardware Requirements

- **ESP32-S3-LCD-2** board (or any ESP32-S3 with PSRAM)
- **2× INMP441** MEMS microphones (I2S)
- **SD card** (FAT32 formatted, 4GB+ recommended)
- **Micro SD card reader/writer** (for loading model file)

## Arduino IDE Setup

### 1. Install Arduino Libraries

Open Arduino IDE → Tools → Manage Libraries, then install:

**Required:**
- `TensorFlowLite_ESP32` by Kazuhiro Tanaka
- (ESP-DSP is included with ESP32 Arduino core)

**Note:** ESP-DSP library is built into the ESP32 Arduino core (esp-dsp component).

### 2. Board Configuration

- **Board:** ESP32S3 Dev Module
- **Partition Scheme:** 16MB Flash (3MB APP/9MB FATFS)
- **PSRAM:** OPI PSRAM
- **Upload Speed:** 921600
- **Core Debug Level:** None (or Info for debugging)

## Model Download and Setup

### Download YAMNet TFLite Model

**Official Source: Kaggle (Recommended)**

1. Visit: https://www.kaggle.com/models/google/yamnet/tfLite/classification-tflite/1
2. Click "Download" button (requires free Kaggle account)
3. Download: `yamnet_classification.tflite` (approx. 3.5-4 MB)

**Alternative: Convert from TensorFlow Hub**
```python
# Python script to convert YAMNet to TFLite
import tensorflow_hub as hub
import tensorflow as tf

model = hub.load('https://tfhub.dev/google/yamnet/1')
# ... conversion code (see Kaggle yamnet-tflite notebook)
```

**For STMicroelectronics YAMNet-256 (smaller, faster)**
- GitHub: https://github.com/STMicroelectronics/stm32ai-modelzoo
- Navigate to: `audio_event_detection/yamnet/ST_pretrainedmodel_public_dataset/`
- Choose ESC-10 or FSD50K dataset folder
- Download: `yamnet_256_64x96_tl_int8.tflite` (~167 KB)

### Prepare SD Card

1. Format SD card as **FAT32**
2. Rename the downloaded model file to: `yamnet.tflite`
   - From Kaggle: `yamnet_classification.tflite` → `yamnet.tflite`
   - From STM32: `yamnet_256_64x96_tl_int8.tflite` → `yamnet.tflite`
3. Copy `yamnet.tflite` to the **root directory** of SD card
4. Insert SD card into ESP32-S3-LCD-2

The SD card structure should look like:
```
SD:/
├── yamnet.tflite        (model file, 167KB-4MB depending on version)
└── embedding.json       (will be created by sketch)
```

## Wiring Diagram

### INMP441 Microphones → ESP32-S3 (I2S0)

```
ESP32-S3-LCD-2          INMP441 #1 (LEFT)      INMP441 #2 (RIGHT)
────────────────        ─────────────────      ──────────────────
3.3V ───────────┬────── VDD                    VDD
                │
GND ────────────┼────── GND ──────────┬─────── GND
                │                     │
GPIO 2 (BCK) ───┼────── SCK ──────────┼─────── SCK
GPIO 4 (WS) ────┼────── WS ───────────┼─────── WS
GPIO 18 (DIN) ──┴────── SD ───────────┴─────── SD
                        │                      │
                        L/R → GND              L/R → 3.3V
                          (LEFT)                 (RIGHT)
```

**Pin Summary:**
| Function      | ESP32 Pin | Device Pin     |
|---------------|-----------|----------------|
| Bit Clock     | GPIO 2    | SCK (both mics)|
| Word Select   | GPIO 4    | WS (both mics) |
| Data In       | GPIO 18   | SD (both mics) |
| Left Channel  | —         | Mic #1 L/R → GND |
| Right Channel | —         | Mic #2 L/R → 3.3V |

### SD Card (SPI)

SD card uses the built-in SD slot on ESP32-S3-LCD-2:
- **CS:** GPIO 41
- **MOSI:** GPIO 38
- **MISO:** GPIO 40
- **SCK:** GPIO 39

## Upload and Run

1. Connect ESP32-S3-LCD-2 to computer via USB-C
2. Open `yamnet_audio_embedding.ino` in Arduino IDE
3. Select board and port
4. Click **Upload**
5. Open **Serial Monitor** at **115200 baud**

## Expected Output

```
========================================
YAMNet-1024 Audio Embedding
ESP32-S3 Dual-Core Optimized
========================================

Total heap: 416544 bytes
Free heap: 389216 bytes
Total PSRAM: 8388608 bytes
Free PSRAM: 8257536 bytes

Mounting SD card... OK
SD Card Size: 3780 MB

Allocating audio buffer... OK
Allocating mel-spectrogram buffer... OK
Allocating embeddings buffer... OK

Initializing I2S microphone... OK
Initializing mel-spectrogram processor... OK
Loading YAMNet-1024 model from SD...
Model file size: 3947632 bytes
Model loaded: 3947632 bytes
Tensor arena: 400 KB
Input tensor: dims=4, shape=[1, 96, 64, 1]
Output tensor: dims=2, shape=[1, 1024]
TFLite interpreter initialized
OK

Memory after initialization:
Free heap: 325440 bytes
Free PSRAM: 3894272 bytes

========================================
READY! System will auto-record on boot
========================================

Recording 3 seconds of audio...
Recording complete: 3024 ms

Generating mel-spectrogram (64x96)...
Mel-spectrogram complete: 487 ms

Running YAMNet-1024 inference...
(Using dual-core optimization)
Inference complete: 8562 ms

Embeddings (first 10 values):
  [0]: 0.234567
  [1]: -0.123456
  [2]: 0.567890
  ...

Writing embeddings to /embedding.json...
Write complete: 156 ms

========================================
PERFORMANCE SUMMARY
========================================
Recording:       3024 ms
Mel-spectrogram: 487 ms
YAMNet inference: 8562 ms
JSON write:      156 ms
TOTAL:           12229 ms
========================================

SUCCESS! Embeddings saved to SD card.
System halting (power cycle to run again).
```

## Configuration

Edit these constants in `yamnet_audio_embedding.ino`:

```cpp
const int RECORD_SECONDS = 3;  // Change to 3, 4, or 5 seconds
```

## Output Format

The sketch creates `/embedding.json` on the SD card:

```json
{
  "dimension": 1024,
  "embeddings": [
    0.234567,
    -0.123456,
    0.567890,
    ...
    (1024 values total)
  ]
}
```

## Memory Usage

**PSRAM Allocation:**
- Audio buffer (3 sec @ 16kHz): ~96 KB
- Mel-spectrogram (64×96): ~24 KB
- Model weights: ~3.9 MB
- Tensor arena: 400 KB
- Embeddings: 4 KB
- **Total: ~4.4 MB** (fits comfortably in 8 MB PSRAM)

**Heap Usage:**
- TFLite infrastructure: ~50 KB
- ESP-DSP FFT buffers: ~10 KB

## Performance

**Typical timing on ESP32-S3 @ 240MHz:**
- Recording (3 sec): ~3000 ms
- Mel-spectrogram: ~500 ms
- YAMNet-1024 inference: **6-10 seconds** (dual-core optimized)
- JSON write: ~150 ms
- **Total: ~10-14 seconds**

The inference is the bottleneck. YAMNet-1024 has 3.2M parameters - impressive for a microcontroller!

## Troubleshooting

### "Failed to load model from SD"
- Check SD card is inserted
- Verify `yamnet1024.tflite` exists in root directory
- Try reformatting SD card as FAT32
- Check file size is ~3-4 MB

### "AllocateTensors() failed"
- Model is too large or incompatible
- Try increasing `TENSOR_ARENA_SIZE` in `yamnet_inference.h`
- Check PSRAM is enabled in Arduino IDE settings

### "Invoke() failed"
- Model operations not supported
- Check Serial Monitor for TFLite error messages
- May need to add more ops to resolver in `yamnet_inference.cpp`

### Very slow inference (>20 seconds)
- Check CPU frequency is 240MHz (Tools → CPU Frequency)
- Verify PSRAM is enabled
- Model may be using PSRAM instead of SRAM (expected for large models)

### Recording is silent
- Check INMP441 wiring (GPIO 2, 4, 18)
- Verify L/R pins: Mic #1 → GND, Mic #2 → 3.3V
- Test with speaker_beep_test or dual_mic_sd_record

### "Input tensor shape mismatch"
- YAMNet expects [1, 96, 64, 1] or [1, 64, 96, 1]
- Check model variant and adjust mel-spectrogram transpose if needed

## Using a Smaller Model

If YAMNet-1024 is too slow, use **YAMNet-256** instead:

1. Download YAMNet-256 from STMicroelectronics:
   ```
   https://huggingface.co/STMicroelectronics/yamnet
   ```

2. Save as `yamnet256.tflite` on SD card

3. Edit `yamnet_audio_embedding.ino`:
   ```cpp
   const char* MODEL_PATH = "/yamnet256.tflite";
   ```

4. Edit `yamnet_inference.h`:
   ```cpp
   #define EMBEDDING_DIM 256    // Change from 1024 to 256
   ```

YAMNet-256 is ~167 KB (fits in SRAM) and runs much faster (~1-2 seconds inference).

## Technical Details

### Mel-Spectrogram Preprocessing

- **FFT size:** 512 (25ms window @ 16kHz)
- **Hop length:** 160 samples (10ms)
- **Mel bins:** 64
- **Frames:** 96
- **Frequency range:** 125-7500 Hz
- **Window:** Hann window
- **Transform:** Log mel-filterbank energies

Preprocessing uses ESP-DSP's hardware-accelerated FFT for fast computation.

### Dual-Core Optimization

- **Core 0:** Main sketch, audio recording, mel-spectrogram
- **Core 1:** TensorFlow Lite inference task

This parallelization allows preprocessing to run concurrently with inference (though inference is still the bottleneck).

### Model Architecture

YAMNet-1024 uses MobileNetV1 architecture with depthwise-separable convolutions. The embedding layer (1024-D) sits before the final classification layer, capturing rich audio features.

## Next Steps

Once you have embeddings:

1. **Audio similarity:** Compute cosine similarity between embeddings
2. **Clustering:** Use k-means to group similar audio samples
3. **Classification:** Train a simple classifier on top of embeddings
4. **Voice identification:** Compare voice embeddings for speaker recognition
5. **Anomaly detection:** Detect unusual sounds by embedding distance

## References

- [YAMNet Paper](https://arxiv.org/abs/1904.07850)
- [TensorFlow Lite Micro](https://www.tensorflow.org/lite/microcontrollers)
- [ESP-DSP Library](https://docs.espressif.com/projects/esp-dsp/)
- [STMicroelectronics YAMNet](https://huggingface.co/STMicroelectronics/yamnet)

---

**Built with ESP32-S3 dual-core power!** 💪
