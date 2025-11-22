# Quick Setup Guide - YAMNet-1024 on ESP32-S3

Follow these steps to get YAMNet-1024 running on your ESP32-S3.

## Step 1: Arduino IDE Setup (5 minutes)

### Install Required Library

1. Open Arduino IDE
2. Go to **Tools → Manage Libraries**
3. Search for: `TensorFlowLite_ESP32`
4. Install: **TensorFlowLite_ESP32** by Kazuhiro Tanaka
5. Wait for installation to complete

### Configure Board

1. Go to **Tools → Board → ESP32 Arduino**
2. Select: **ESP32S3 Dev Module**
3. Configure these settings:
   - **Partition Scheme:** 16MB Flash (3MB APP/9MB FATFS)
   - **PSRAM:** OPI PSRAM
   - **CPU Frequency:** 240MHz
   - **Upload Speed:** 921600

## Step 2: Download YAMNet TFLite Model (2 minutes)

### Official Download (Kaggle - Recommended)

1. Visit: https://www.kaggle.com/models/google/yamnet/tfLite/classification-tflite/1
2. Click "Download" button (requires free Kaggle account - quick signup!)
3. Save the downloaded file as: `yamnet.tflite`

**Expected file size:** ~3.5-4.0 MB

### Alternative: YAMNet-256 (Smaller, Faster)

For faster inference (~2 seconds instead of 8):

1. Visit: https://github.com/STMicroelectronics/stm32ai-modelzoo
2. Navigate to: `audio_event_detection/yamnet/ST_pretrainedmodel_public_dataset/fsd50k/yamnet_256_64x96_tl/`
3. Download: `yamnet_256_64x96_tl_int8.tflite`
4. Rename to: `yamnet.tflite`

**Expected file size:** ~167 KB

**Note:** If using YAMNet-256, change `#define EMBEDDING_DIM 1024` to `256` in `yamnet_inference.h`

## Step 3: Prepare SD Card (3 minutes)

1. Insert SD card into your computer
2. **Format as FAT32** (very important!)
   - Windows: Right-click → Format → FAT32
   - Mac: Disk Utility → Erase → MS-DOS (FAT)
   - Linux: `sudo mkfs.vfat -F 32 /dev/sdX1`

3. Copy `yamnet.tflite` to the **root directory** of SD card
4. Eject SD card safely
5. Insert into ESP32-S3-LCD-2 SD card slot

## Step 4: Hardware Wiring (10 minutes)

### You Need:
- 2× INMP441 microphones
- Jumper wires
- Breadboard (optional)

### Connections:

**Both INMP441 mics share I2S bus:**
```
ESP32 GPIO 2  → SCK (both mics)
ESP32 GPIO 4  → WS (both mics)
ESP32 GPIO 18 → SD (both mics)
ESP32 3.3V    → VDD (both mics)
ESP32 GND     → GND (both mics)
```

**Channel selection (CRITICAL!):**
```
Mic #1 L/R pin → GND     (left channel)
Mic #2 L/R pin → 3.3V    (right channel)
```

**Tip:** Use different colored jumper wires for each mic to avoid confusion!

## Step 5: Upload Sketch (2 minutes)

1. Open `yamnet_audio_embedding.ino` in Arduino IDE
2. Connect ESP32-S3 via USB-C
3. Select correct COM port (Tools → Port)
4. Click **Upload** button (→)
5. Wait for compilation and upload (~60 seconds)

## Step 6: Test & Verify (5 minutes)

1. Open **Serial Monitor** (Tools → Serial Monitor)
2. Set baud rate to **115200**
3. Press **Reset** button on ESP32-S3
4. You should see:
   ```
   ========================================
   YAMNet-1024 Audio Embedding
   ESP32-S3 Dual-Core Optimized
   ========================================

   Mounting SD card... OK
   Loading YAMNet-1024 model from SD...
   Model loaded: 3947632 bytes
   ...
   ```

5. **Speak into the microphones** during the 3-second recording
6. Wait ~10-15 seconds for processing
7. Check for: `SUCCESS! Embeddings saved to SD card.`

## Step 7: Retrieve Results (1 minute)

1. Power off ESP32-S3
2. Remove SD card
3. Insert into computer
4. Open `embedding.json` - you should see:
   ```json
   {
     "dimension": 1024,
     "embeddings": [
       0.234567,
       -0.123456,
       ...
     ]
   }
   ```

## Troubleshooting Quick Fixes

### "Failed to load model from SD"
→ Check file is named exactly: `yamnet.tflite` (lowercase!)
→ Make sure it's in the root directory (not in a folder)
→ Verify file size is 3.5-4MB (Kaggle) or 167KB (STM32)

### "AllocateTensors() failed"
→ Go to Tools → PSRAM → Select "OPI PSRAM"
→ Re-upload sketch

### Recording is silent
→ Swap L/R connections (try opposite 3.3V/GND)
→ Check GPIO 2, 4, 18 are correct

### Compilation errors
→ Make sure TensorFlowLite_ESP32 library is installed
→ Try restarting Arduino IDE
→ Check ESP32 Arduino core is version 2.0.0+

## Expected Performance

✅ **Recording:** ~3 seconds
✅ **Mel-spectrogram:** ~0.5 seconds
✅ **Inference:** ~6-10 seconds (this is normal!)
✅ **Total:** ~10-15 seconds

The inference is slow because YAMNet-1024 has 3.2 million parameters. This is IMPRESSIVE for a microcontroller!

## Next: Use Your Embeddings!

Now that you have 1024-dimensional embeddings, you can:
- Compare different audio samples (cosine similarity)
- Cluster similar sounds
- Train classifiers
- Build voice recognition systems

Check the main README.md for advanced usage!

---

**Questions? Issues?** Check README.md for detailed troubleshooting.
