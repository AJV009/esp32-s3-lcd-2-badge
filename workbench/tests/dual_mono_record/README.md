# Dual Microphone to Mono Downmix Recording

## Overview

This sketch captures audio from **two INMP441 microphones** and intelligently downmixes them to **MONO** for clearer voice capture than using a single microphone alone.

## Why Dual Mics → Mono?

✅ **Better spatial coverage** - Captures voice from multiple directions
✅ **Noise reduction** - Two mics can reject environmental noise better
✅ **Clearer speech** - Combined signal is more robust than single mic
✅ **No stereo "ping-pong"** - Voice stays centered, not split between channels

## Hardware Wiring

**Same as stereo setup:**

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
```

## Downmix Algorithms

The sketch provides **three algorithms** - choose one by uncommenting it in the code:

### 1. SIMPLE_AVERAGE (Default - Recommended)

```cpp
#define SIMPLE_AVERAGE
```

**Formula:** `Mono = (Left + Right) / 2`

**Best for:**
- Mics positioned close together
- Voice directly between the mics
- General-purpose recording

**Pros:**
- Standard industry approach
- Maintains proper loudness
- Smooth, natural sound

**Cons:**
- May have phase cancellation if mics are far apart
- Can reduce volume if signals are out of phase

---

### 2. PEAK_SELECT

```cpp
// #define SIMPLE_AVERAGE    // Comment out
#define PEAK_SELECT          // Uncomment this
```

**Formula:** `Mono = max(abs(L), abs(R))` (preserves sign)

**Best for:**
- Noisy environments
- Preventing phase cancellation
- When mic placement causes phase issues

**Pros:**
- No phase cancellation
- Always takes the stronger signal
- Good for speech in noise

**Cons:**
- Can sound slightly harsher
- May amplify brief noises

---

### 3. ADAPTIVE_MIX (Most Intelligent)

```cpp
// #define SIMPLE_AVERAGE    // Comment out
#define ADAPTIVE_MIX         // Uncomment this
```

**Formula:** Weighted average based on recent signal strength

```
left_weight = left_energy / (left_energy + right_energy)
Mono = Left * left_weight + Right * (1 - left_weight)
```

**Best for:**
- Moving speaker
- Voice from varying positions
- Professional-quality capture

**Pros:**
- Automatically favors the mic with better signal
- Adapts to speaker movement
- Best overall quality

**Cons:**
- Slightly more CPU usage (negligible on ESP32-S3)

---

## Output File

- **Format:** WAV (PCM)
- **Sample Rate:** 16000 Hz
- **Channels:** 1 (MONO)
- **Bit Depth:** 16-bit
- **Duration:** 30 seconds
- **File Size:** ~960 KB (half of stereo)
- **Location:** SD card `/recording_mono.wav`

## How It Works

1. **Record stereo** from both mics to PSRAM (no SD interference)
2. **Downmix** stereo to mono using selected algorithm
3. **Write mono WAV** to SD card
4. **Done!** LED stays solid ON

## Usage

1. **Wire both microphones** as shown above
2. **Insert SD card** (FAT32 formatted)
3. **Choose algorithm** in the sketch (uncomment one `#define`)
4. **Upload** to ESP32-S3-LCD-2
5. **Wait 30 seconds** for recording (LED blinks)
6. **Remove SD card** and play `/recording_mono.wav`

## Recommended Testing

Try all three algorithms with your setup to see which sounds best!

### Test Process:

1. Upload with `SIMPLE_AVERAGE`
2. Record, rename file to `test_average.wav`
3. Change to `PEAK_SELECT`, upload, record
4. Rename to `test_peak.wav`
5. Change to `ADAPTIVE_MIX`, upload, record
6. Rename to `test_adaptive.wav`
7. **Compare on your computer** - pick the clearest one!

## Expected Results

**SIMPLE_AVERAGE:**
Smooth, natural sound. Best for most cases.

**PEAK_SELECT:**
Slightly louder, more present. Good if you hear phase issues with SIMPLE_AVERAGE.

**ADAPTIVE_MIX:**
Most "intelligent" - follows your voice position. Best if you move around.

## Troubleshooting

### "Mono sounds quieter than stereo"
- This is normal - stereo has two channels
- SIMPLE_AVERAGE maintains proper volume
- Try PEAK_SELECT for louder output

### "Still hear phase cancellation / hollow sound"
- Switch to `PEAK_SELECT` or `ADAPTIVE_MIX`
- Check mic spacing (closer = less phase issues)
- Ensure both mics face the same direction

### "One mic still dominates"
- Try `ADAPTIVE_MIX` - it adapts to signal strength
- Check wiring - both mics should be equal distance from voice source

## Technical References

Based on research from:
- **Standard downmix:** (L + R) / 2 is industry standard ([Best practices for Stereo-to-Mono mixdown](https://www.audiokinetic.com/qa/5408/best-practices-for-stereo-to-mono-mixdown))
- **Phase considerations:** Important for microphone arrays ([Intelligent downmix of stereo to mono](https://sound.stackexchange.com/questions/39151/intelligent-downmix-of-stereo-to-mono))
- **Beamforming techniques:** ESP32-S3 dual mic processing ([Beamforming & DoA on ESP32-S3](https://prometeo.blog/en/practical-case-beamforming-doa-on-esp32-s3-i2s-inmp441/))
- **ESP-Skainet:** Espressif's speech SDK with noise reduction and beamforming ([ESP Audio DevKits](https://www.espressif.com/en/products/devkits/esp-audio-devkits))

## Next Steps

Once you find the best algorithm for your use case:
- Integrate with voice recognition
- Add voice activity detection (VAD)
- Implement real-time streaming
- Combine with speaker output for full duplex

---

**Pro Tip:** For best results, position mics 5-10cm apart, both facing the speaker!
