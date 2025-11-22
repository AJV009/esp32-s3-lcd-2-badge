# Record, Downmix, and Playback Loop

## Overview

This sketch demonstrates a complete audio capture and playback pipeline:
1. **Records 30 seconds** of stereo audio from dual INMP441 microphones
2. **Downmixes to mono** using HYBRID algorithm (adaptive + width)
3. **Plays back** continuously through MAX98357A speaker

## Hardware Requirements

- ESP32-S3-LCD-2 board
- 2× INMP441 MEMS microphones
- 1× MAX98357A I2S amplifier module
- 2× 1W flat mag speakers (4-8 ohm)
- Breadboard and jumper wires

## Wiring Diagram

### INMP441 Microphones → ESP32 (I2S0)

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

### MAX98357A Speaker → ESP32 (I2S1)

```
MAX98357A               ESP32-S3-LCD-2
─────────────           ────────────────
VIN    ─────────────────→ 5V (or 3.3V)
GND    ─────────────────→ GND
BCLK   ─────────────────→ GPIO 6
LRC    ─────────────────→ GPIO 7
DIN    ─────────────────→ GPIO 8
GAIN   ─────────────────→ Float (9dB) / GND (12dB) / VDD (15dB)
SD     ─────────────────→ VIN (always on)

SPK+   ─────────────────→ Speaker +
SPK-   ─────────────────→ Speaker -
```

## Pin Summary

| Function      | ESP32 Pin | Device Pin     | Notes                          |
|---------------|-----------|----------------|--------------------------------|
| **Microphones (I2S0)** |           |                |                                |
| Bit Clock     | GPIO 2    | SCK (both mics)| Shared I2S bus                 |
| Word Select   | GPIO 4    | WS (both mics) | Shared I2S bus                 |
| Data In       | GPIO 18   | SD (both mics) | Shared I2S bus                 |
| Left Channel  | —         | Mic #1 L/R → GND | Channel select                 |
| Right Channel | —         | Mic #2 L/R → 3.3V | Channel select                 |
| **Speaker (I2S1)** |           |                |                                |
| Bit Clock     | GPIO 6    | BCLK           | Camera pin (safe if no camera) |
| Word Select   | GPIO 7    | LRC            | Camera pin (safe if no camera) |
| Data Out      | GPIO 8    | DIN            | Camera pin (safe if no camera) |
| Power         | 5V        | VIN            | 3.3V also works (lower volume) |

**Important:** GPIO 6, 7, 8 are camera pins from the schematic, but safe to use when the camera is not attached.

## Audio Specifications

| Parameter       | Value              |
|-----------------|--------------------|
| Sample Rate     | 16,000 Hz          |
| Bit Depth       | 16-bit PCM         |
| Recording       | 30 seconds stereo  |
| Playback        | Mono (looped)      |
| Downmix Method  | HYBRID (adaptive + width) |
| Memory Usage    | ~1.92 MB PSRAM     |

## Downmix Algorithm: HYBRID

The HYBRID downmix algorithm combines the best of two approaches:

- **Adaptive weighting**: Favors the microphone with stronger signal (clarity)
- **30%-70% constraint**: Ensures both channels always contribute (width)

This prevents the "interruption" feeling when blocking one mic while maintaining clarity and reducing background noise compared to simple averaging.

## Usage Instructions

### 1. Wire Everything

Follow the wiring diagrams above. Critical points:

- **Microphones**: Both share I2S bus (GPIO 2, 4, 18)
  - Mic #1: L/R → **GND** (left channel)
  - Mic #2: L/R → **3.3V** (right channel)

- **Speaker**: Connect to I2S1 (GPIO 6, 7, 8)
  - Use **5V** for full power (3.3V works but quieter)
  - Connect speaker to SPK+ and SPK-
  - Set GAIN pin for desired volume

### 2. Upload the Sketch

1. Open `record_play_loop.ino` in Arduino IDE
2. Select **Board**: ESP32S3 Dev Module
3. Select **Partition Scheme**: 16MB Flash (3MB APP/9MB FATFS)
4. Select **PSRAM**: OPI PSRAM
5. Upload

### 3. Monitor Operation

Open Serial Monitor at **115200 baud**:

```
========================================
Record -> Downmix -> Playback Loop
========================================

Total PSRAM: 8388608 bytes
Free PSRAM: 8257536 bytes

Allocating stereo buffer (1920000 bytes)... OK
Allocating mono buffer (960000 bytes)... OK

Initializing I2S0 (microphones)... OK
Initializing I2S1 (speaker)... OK

Recording 30 seconds (stereo)...

Progress: 10% (192000 / 960000 samples)
Progress: 20% (384000 / 960000 samples)
...
Progress: 100% (960000 / 960000 samples)

Recorded: 960000 samples in 30012 ms

========================================
Recording complete!
========================================

Applying HYBRID downmix... OK

========================================
Starting playback loop...
========================================

Playing... sample 0 / 480000
Playing... sample 32768 / 480000
...
Playback complete, looping...
```

### 4. LED Feedback

- **Blinking**: Recording in progress
- **Solid ON**: Playback running

## Testing Tips

### Test Recording Quality

During the 30-second recording:
- Speak clearly from different positions
- Tap near each microphone to verify both are working
- Try speaking while moving between mics

### Test Playback Volume

If playback is too quiet or too loud:

**Hardware adjustments:**
- **GAIN pin**: Float (9dB), GND (12dB), or VDD (15dB)
- **Power**: 5V gives more power than 3.3V
- **Speaker**: 4-ohm speakers are louder than 8-ohm

**Code adjustments (if needed):**
```cpp
// In playMonoBuffer(), scale samples before writing:
int16_t scaled = mono_buffer[i] * 2;  // 2x gain (may clip!)
i2s_write(I2S_NUM_1, &scaled, ...);
```

### Test Downmix Quality

The HYBRID algorithm should provide:
- ✅ Clear, intelligible voice
- ✅ Captures from multiple directions
- ✅ No "interruption" when blocking one mic
- ✅ Less background noise than simple average

## Troubleshooting

### No audio from speaker

- Check MAX98357A wiring (especially BCLK, LRC, DIN)
- Verify speaker is connected to SPK+/SPK-
- Try connecting SD pin to VIN (enable amplifier)
- Check Serial Monitor for I2S1 initialization errors

### Distorted/clipped audio

- Lower GAIN pin setting (try Float = 9dB)
- Check speaker impedance (4-8 ohm required)
- Verify 5V power is stable

### Quiet playback

- Increase GAIN pin setting (connect to VDD = 15dB)
- Use 5V instead of 3.3V for VIN
- Use 4-ohm speaker instead of 8-ohm
- **Software gain**: Increase `GAIN_BOOST` to 6.0f or 8.0f (line 235 in .ino)
- **Recording level**: Speak very loudly into mics during recording, position mics 5-10cm from mouth

### Recording noise/static

- Check microphone wiring (especially L/R pins)
- Verify both mics are powered (3.3V to VDD)
- Ensure solid I2S connections (GPIO 2, 4, 18)
- Try different microphones if one is defective

### Playback loops too fast/slow

- Sample rate mismatch - ensure both I2S buses use 16kHz
- Check `SAMPLE_RATE` constant is 16000

## How It Works

### 1. Dual I2S Buses

ESP32-S3 has multiple I2S peripherals:
- **I2S_NUM_0**: Microphones (RX mode)
- **I2S_NUM_1**: Speaker (TX mode)

These run independently and can operate simultaneously.

### 2. Recording Phase

```
I2S0 (mics) → i2s_buffer (32-bit) → stereo_buffer (16-bit, PSRAM)
```

Captures 30 seconds of stereo audio (left + right interleaved).

### 3. Downmix Phase

```
stereo_buffer → HYBRID algorithm → mono_buffer (PSRAM)
```

Applies adaptive weighting with 30%-70% constraints.

### 4. Playback Phase

```
mono_buffer → I2S1 (speaker) → Loop forever
```

Continuously plays the mono recording.

## Next Steps

Once you've verified the basic loop works:

1. **Add voice activity detection (VAD)** - Only record when voice detected
2. **Implement push-to-talk** - Button to trigger recording
3. **Add effects** - Reverb, echo, pitch shift
4. **Stream to WiFi** - Send audio over network
5. **Add compression** - OPUS codec for smaller files
6. **Dual direction** - Record while playing (walkie-talkie mode)

## Technical References

- **ESP32-S3 I2S**: [ESP-IDF I2S Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/i2s.html)
- **INMP441 Datasheet**: Omnidirectional I2S MEMS Microphone
- **MAX98357A Datasheet**: I2S Class D Amplifier
- **Hybrid Downmix**: Adaptive weighting with minimum contribution per channel

---

**Power Tip:** For battery operation, use 3.3V for MAX98357A VIN to reduce current draw. Volume will be lower but adequate for close listening!
