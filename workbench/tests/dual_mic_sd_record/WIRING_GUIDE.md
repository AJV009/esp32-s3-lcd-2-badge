# INMP441 Dual Microphone SD Recording Test - Wiring Guide

## Overview

This test records 30 seconds of stereo audio from two INMP441 microphones and saves it as a WAV file to the SD card on boot.

## Hardware Requirements

- ESP32-S3-LCD-2 development board
- 2× INMP441 MEMS microphones
- **Micro SD card** (formatted as FAT32)
- Breadboard and jumper wires

## Pin Connections

### ESP32-S3-LCD-2 to INMP441 Microphones

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

### Pin Summary

| ESP32 Pin | INMP441 Pin | Function           | Notes                          |
|-----------|-------------|--------------------|--------------------------------|
| 3.3V      | VDD         | Power Supply       | Both mics connected in parallel|
| GND       | GND         | Ground             | Both mics connected in parallel|
| GPIO 2    | SCK         | I2S Bit Clock      | Shared by both microphones     |
| GPIO 4    | WS          | I2S Word Select    | Shared by both microphones     |
| GPIO 18   | SD          | I2S Serial Data    | Shared by both microphones     |
| —         | L/R (Mic #1)| Channel Select     | Connect to **GND** for LEFT    |
| —         | L/R (Mic #2)| Channel Select     | Connect to **3.3V** for RIGHT  |

### Critical: L/R Pin Configuration

The **L/R pin** on each INMP441 determines which stereo channel it transmits on:

- **INMP441 #1**: L/R → **GND** (transmits on **LEFT** channel)
- **INMP441 #2**: L/R → **3.3V** (transmits on **RIGHT** channel)

**Do not leave L/R floating!** This will cause unpredictable behavior.

## Pin Selection Rationale

From the ESP32-S3-LCD-2 schematic:

- **GPIO 2, 4**: Camera interface pins (safe to use when camera not attached)
- **GPIO 18**: Free GPIO, not assigned to any peripheral
- These pins are broken out to the board's pin headers
- Compatible with I2S peripheral routing on ESP32-S3

## Recording Specifications

| Parameter       | Value              |
|-----------------|--------------------|
| Sample Rate     | 16,000 Hz          |
| Channels        | 2 (Stereo)         |
| Bit Depth       | 16-bit PCM         |
| Duration        | 30 seconds         |
| File Format     | WAV                |
| File Size       | ~1.92 MB           |
| Output File     | `/recording.wav`   |
| Storage         | SD card (FAT32)    |

## Usage Instructions

### 1. Prepare SD Card

Before running this sketch:

1. **Insert a micro SD card** into your computer
2. **Format as FAT32** (most SD cards come pre-formatted)
3. **Insert into ESP32-S3-LCD-2** SD card slot
4. No files needed - the sketch will create `/recording.wav` automatically

### 2. Wire the Microphones

Follow the pin connections table above. **Critical points:**

- Connect both microphones' **SCK**, **WS**, and **SD** pins together (I2S bus)
- **Mic #1 L/R → GND** (for left channel)
- **Mic #2 L/R → 3.3V** (for right channel)
- Ensure solid power and ground connections

### 3. Upload the Sketch

1. Open `dual_mic_sd_record.ino` in Arduino IDE
2. Select **Board**: ESP32S3 Dev Module
3. Select **Partition Scheme**: 16MB Flash (3MB APP/9MB FATFS)
4. Select **PSRAM**: OPI PSRAM
5. Upload the sketch

### 4. Monitor Recording

Open Serial Monitor at **115200 baud**. You should see:

```
========================================
INMP441 Dual Mic SD Recording Test
========================================

Initializing SD card... OK
SD Card Type: SDHC
SD Card Size: 15193 MB
SD Total: 15193 MB
SD Used: 0 MB

Initializing I2S... OK

Recording 30 seconds of stereo audio...
Target file: /recording.wav
Expected size: 1920044 bytes

Progress: 10% (192000 / 960000 samples, 3000 ms)
Progress: 20% (384000 / 960000 samples, 6000 ms)
Progress: 30% (576000 / 960000 samples, 9000 ms)
...
Progress: 100% (960000 / 960000 samples, 30000 ms)

Recording finished: 960000 samples in 30012 ms

========================================
Recording complete!
========================================

File saved to SD card as /recording.wav
You can remove the SD card and play it on your computer!
```

### 5. LED Feedback

- **Blinking during recording**: Normal operation
- **Solid ON after completion**: Recording successful
- **Blinking after completion**: Recording failed (check Serial Monitor)

## Retrieving the Recording

**Super simple!**

1. Wait for the recording to complete (LED stays solid ON)
2. **Remove the SD card** from the ESP32-S3-LCD-2
3. **Insert into your computer** (use SD card reader if needed)
4. **Open `/recording.wav`** - it's in the root directory
5. **Play with any audio software** (VLC, Windows Media Player, Audacity, etc.)

## Testing the Recording

To verify both microphones are working:

1. **During recording**, tap or speak near each microphone separately
2. After retrieving `/recording.wav`, play it back on your computer
3. **Left channel** should have audio from **Mic #1** (L/R → GND)
4. **Right channel** should have audio from **Mic #2** (L/R → 3.3V)

Use an audio editor like Audacity to view the stereo waveforms separately.

## Troubleshooting

### "Initializing SD card... FAILED"

- **No SD card inserted** - Insert a micro SD card
- **SD card not formatted** - Format as FAT32 on your computer
- **Bad SD card** - Try a different card
- **Poor contact** - Re-insert the SD card firmly

### "ERROR: No SD card attached"

- SD card detected but not readable
- Try formatting as FAT32
- Check card is not write-protected

### "ERROR: Cannot create file on SD card"

- **SD card full** - Delete old files or use a larger card
- **SD card write-protected** - Remove write protection
- **Corrupted filesystem** - Reformat the SD card

### "ERROR: I2S read failed"

- Incorrect pin connections
- Check GPIO 2, 4, 18 are correctly wired
- Verify I2S peripheral not in use by other code

### "ERROR: File write failed"

- FFat partition full or corrupted
- Reformat FFat partition

### No audio in one channel

- Check L/R pin on that microphone
  - Mic #1 (left) L/R should be at **GND**
  - Mic #2 (right) L/R should be at **3.3V**
- Verify SCK, WS, SD connections to that mic

### Very low audio levels

- Normal for quiet environments
- Speak loudly or clap near microphones during recording
- INMP441 has high sensitivity; adjust gain in post-processing if needed

### Recording stops early

- I2S buffer overflow (unlikely with current settings)
- SD card write speed too slow (try a faster/Class 10 SD card)
- SD card becoming full during recording

## Next Steps

Once you've verified stereo recording works:

1. Integrate with audio processing algorithms
2. Add VAD (Voice Activity Detection) to trigger recording
3. Implement real-time audio streaming
4. Add compression (e.g., OPUS, MP3) to reduce file size
5. Combine with speaker output for full audio I/O

## Reference

- **INMP441 Datasheet**: I2S MEMS microphone specifications
- **ESP32-S3 I2S Documentation**: ESP-IDF I2S driver API
- **CLAUDE.md**: Pin assignments and board architecture
- **Related Tests**:
  - `single_mic_test/`: Single INMP441 test
  - `mic_pin_test/`: Dual INMP441 pin validation (no SD)
