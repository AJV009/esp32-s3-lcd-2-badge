# Speaker Beep Test

## What This Does

This is a **hardware verification test** for your MAX98357A amplifier and speakers. It plays simple beeps and tones so you can confirm everything is wired correctly.

## What You Should Hear

If everything is working, you'll hear:

1. **Four musical notes** (A, C, E, G) - short beeps
2. **A rising sweep** from low to high pitch (200 Hz → 2000 Hz)
3. **Pattern repeats** forever

## Wiring (Same as Before)

```
MAX98357A → ESP32
─────────────────
VIN   → 5V
GND   → GND
BCLK  → GPIO 6
LRC   → GPIO 7
DIN   → GPIO 8
GAIN  → 5V (loud) or Float (quieter) or GND (medium)

Speakers (PARALLEL):
SPK+ ───┬──── Speaker 1 (+)
        └──── Speaker 2 (+)

SPK- ───┬──── Speaker 1 (-)
        └──── Speaker 2 (-)
```

## Upload and Test

1. Open `speaker_beep_test.ino` in Arduino IDE
2. Upload to ESP32
3. Open Serial Monitor (115200 baud)
4. **Listen for beeps!**

## Troubleshooting

### ❌ I hear NOTHING

**Check wiring:**
- GPIO 6 → BCLK (not DIN!)
- GPIO 7 → LRC (not BCLK!)
- GPIO 8 → DIN (not LRC!)
- 5V → VIN (power)
- GND → GND

**Check speakers:**
- SPK+ and SPK- connected?
- Speakers in parallel (not series)?
- Speaker wires not touching/shorting?

**Check Serial Monitor:**
- Does it say "Initializing I2S... OK"?
- If it says "FAILED", there's an I2S problem

### ❌ Very quiet / barely audible

**Try this:**
1. **Disconnect GAIN pin** (leave it floating) - see if you hear anything
2. **Connect GAIN to GND** - should be medium volume
3. **Connect GAIN to 5V** - should be loudest

If you still can't hear anything even at maximum GAIN, check:
- Speaker impedance (should be 4-8 ohm)
- Speaker is not broken (test with another device)
- MAX98357A board is not damaged

### ❌ Distorted / crackling

- Lower the GAIN pin (try Float or GND instead of 5V)
- Check speaker impedance (might be too low, like 2 ohm)
- Verify 5V power supply can provide enough current

### ✅ I hear clear beeps!

**Your speaker hardware is PERFECT!** The problem was with the recording/playback sketch, not the hardware. Possible issues with the original sketch:
1. Recording didn't work (mic issue)
2. Downmix had a bug
3. Playback buffer was empty

Go back to the `record_play_loop` sketch and check the Serial Monitor during recording to see where it failed.

## What the Test Proves

If this test works:
- ✅ MAX98357A is working
- ✅ I2S1 peripheral is configured correctly
- ✅ Speakers are wired correctly
- ✅ GPIO pins 6, 7, 8 are functional
- ✅ Power supply is adequate

The issue is NOT with your speaker hardware!

## Next Steps

Once you confirm this test works:
1. Go back to `record_play_loop` sketch
2. Connect to USB and check Serial Monitor
3. Look for which step fails:
   - Recording? (mic issue)
   - Downmix? (algorithm issue)
   - Playback? (buffer issue)

Let me know what you hear and we'll fix the recording sketch!
