# MAX98357A Speaker Test Guide

## Quick Start

1. **Wire the MAX98357A** according to the pin configuration
2. **Connect a speaker** to the MAX98357A output (+ and -)
3. **Upload the sketch** `speaker_test.ino`
4. **Open Serial Monitor** at 115200 baud
5. **Listen for white noise** from the speaker

## Expected Behavior

✅ **WORKING**: You should hear:
- Continuous white noise (static/hiss sound) from speaker
- Loud, clear audio output
- No distortion or crackling (unless speaker is overdriven)

❌ **NOT WORKING**: Signs of problems:
- No sound at all → Check power/ground connections
- Very quiet sound → Check GAIN pin configuration
- Distortion/crackling → Check DIN wiring or reduce volume
- Popping on startup → Normal for MAX98357A (can add RC filter)

## Pin Configuration

### Recommended (Default in sketch):
```
BCLK (Bit Clock):   GPIO 6
LRC  (Word Select): GPIO 7
DIN  (Data In):     GPIO 8
```

### MAX98357A GAIN Pin Settings:
```
GAIN = Float (disconnected): 9dB gain
GAIN = GND:                  12dB gain
GAIN = VDD:                  15dB gain
```

**Recommendation**: Start with GAIN floating (9dB) for 1W speaker.

## Wiring Diagram

```
ESP32-S3-LCD-2                MAX98357A                Speaker
──────────────                ─────────                ────────
5V ──────────────────────────── VIN

GND ─────────────────────────── GND ───────────────────── GND

GPIO 6 (BCLK) ──────────────── BCLK

GPIO 7 (LRC) ───────────────── LRC

GPIO 8 (DIN) ───────────────── DIN

                               GAIN ──> Float (or GND/VDD)

                               SD ────> VIN (always on)

                               OUT+ ──────────────────────> Speaker +

                               OUT- ──────────────────────> Speaker -
```

## Power Considerations

**For 1W Speaker:**
- Use 5V input to MAX98357A (not 3.3V)
- GAIN = Float (9dB) is usually sufficient
- MAX98357A can deliver 3W into 4Ω or 1.4W into 8Ω

**Warning:** The MAX98357A is rated for 3-5W but your speaker is 1W. The white noise at full volume should be safe, but avoid continuous testing at maximum volume for extended periods.

## Alternative Pin Combinations

If GPIO 6/7/8 conflict with other peripherals, try:
- **10, 11, 12** - All camera pins
- **13, 14, 15** - All camera pins
- **16, 17, 21** - Camera/free pins

**Avoid:**
- GPIO 2, 4, 18 (used by microphone if testing both)
- GPIO 38-42, 45 (SPI - LCD/SD)
- GPIO 43, 44 (Serial console)
- GPIO 47, 48 (I2C - IMU/Touch)

## Troubleshooting

### No sound at all
1. Check power connections (VIN to 5V, GND to GND)
2. Check SD pin is tied to VIN (not GND)
3. Verify speaker is connected correctly
4. Use multimeter to check if 5V is present at MAX98357A VIN

### Sound is very quiet
1. Tie GAIN pin to GND (12dB) or VDD (15dB) for more gain
2. Check speaker impedance (4Ω is louder than 8Ω)
3. Verify 5V power supply (not 3.3V)

### Distortion or crackling
1. Check DIN wiring - should be short and direct
2. Add 100nF capacitor near VIN/GND pins
3. Ensure good ground connection
4. Check if speaker can handle the power

### Popping sound on startup
- Normal behavior for MAX98357A
- Can be reduced with RC filter on SD pin
- Or control SD pin via GPIO and enable after I2S starts

### "ERROR: i2s_driver_install failed"
- Pin conflict with another peripheral
- Try different GPIO pins
- Make sure not using same I2S port as microphone

## Volume Control

This sketch plays at **full digital volume**. To reduce volume:

**Option 1: Reduce gain at source**
```cpp
// Reduce sample amplitude (50% volume)
samples[i] = random(-16384, 16383);  // Half of full range
```

**Option 2: Change GAIN pin**
- Float = 9dB (quietest)
- GND = 12dB (medium)
- VDD = 15dB (loudest)

**Option 3: Use a potentiometer**
- Place 10K pot between speaker and MAX98357A output
- Not recommended for best audio quality

## Next Steps

Once speaker is working:
1. Test with tone generation instead of white noise
2. Integrate with microphone for audio passthrough
3. Add ESP_SR library for speech recognition + response
4. Implement voice-activated features

## Sample Serial Output (Working)

```
=== MAX98357A Speaker Test ===
I2S Pins: BCK=6, WS=7, DOUT=8
(Using I2S1 for speaker output)

Configuring I2S...

I2S initialized successfully!
Playing white noise at full volume...

WARNING: This will be LOUD! Reduce volume if needed.

Playing... 1024 bytes written
Playing... 1024 bytes written
Playing... 1024 bytes written
```

You should hear continuous white noise from the speaker!
