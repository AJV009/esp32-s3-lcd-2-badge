# INMP441 Pin Test Guide

## Quick Start

1. **Upload the sketch** `mic_pin_test.ino` to your ESP32-S3-LCD-2
2. **Open Serial Monitor** at 115200 baud
3. **Wire both microphones** according to the pin configuration
4. **Test each mic** by tapping or blowing on them individually

## Expected Behavior

✅ **WORKING**: You should see:
- Two bar graphs labeled LEFT and RIGHT
- LEFT bar grows when you tap/blow on mic #1 (L/R=GND)
- RIGHT bar grows when you tap/blow on mic #2 (L/R=VDD)
- Both bars respond independently

❌ **NOT WORKING**: Signs of problems:
- No bars at all → Check power/ground connections
- Both bars always identical → Check L/R pin connections
- One bar always zero → Check that specific mic's wiring
- Random noise only → Normal for quiet room, make sound!

## Pin Configuration Options

### Recommended (Default in sketch):
```
BCK (Bit Clock):    GPIO 18
WS  (Word Select):  GPIO 19
DIN (Data In):      GPIO 20
```

### Available Alternatives on ESP32-S3-LCD-2:

**Free GPIO pins you can try** (avoid pins listed as "Used" in analysis):

**Safe options:**
- GPIO 3, 18, 19, 20, 33, 34, 35, 36, 37, 43, 44, 46

**Avoid these** (already in use):
- GPIO 0 (BOOT), 1 (BL), 5 (BAT)
- GPIO 38,39,40,41,42,45 (SPI/LCD/SD)
- GPIO 47,48 (I2C/IMU)
- GPIO 2,4,6,7,8,9,10,11,12,13,14,15,16,17,21 (Camera - if attached)

## How to Test Different Pins

1. **Edit line 24-26** in `mic_pin_test.ino`:
   ```cpp
   #define I2S_BCK_PIN   18    // Try different pin here
   #define I2S_WS_PIN    19    // Try different pin here
   #define I2S_DIN_PIN   20    // Try different pin here
   ```

2. **Re-upload** and check Serial Monitor
3. **Note which pins work** for your final implementation

## Wiring Diagram

```
ESP32-S3-LCD-2                INMP441 #1 (LEFT)       INMP441 #2 (RIGHT)
──────────────                ─────────────────       ──────────────────
3.3V ────────────┬──────────── VDD                    VDD
                 │
GND ─────────────┼──────────── GND ──────────────┬─── GND
                 │                                │
GPIO 18 (BCK) ───┼──────────── SCK ───────────────┼─── SCK
                 │                                │
GPIO 19 (WS) ────┼──────────── WS ────────────────┼─── WS
                 │                                │
GPIO 20 (DIN) ───┴──────────── SD ────────────────┴─── SD
                                │                     │
                                L/R ──> GND           L/R ──> 3.3V
                                      (LEFT)                (RIGHT)
```

## Troubleshooting

### "ERROR: i2s_driver_install failed"
- Pin conflict with another peripheral
- Try different GPIO pins

### "ERROR: i2s_set_pin failed"
- Invalid pin number
- Check ESP32-S3 GPIO matrix limitations

### Both channels show identical signals
- L/R pins not configured correctly
- Check: Mic #1 L/R → GND, Mic #2 L/R → VDD

### Very low signal levels (< 1000)
- Normal for quiet environment
- Try clapping or speaking loudly close to mic
- Good levels: 10,000 - 1,000,000+ for loud sounds

### One channel always zero
- Check that specific microphone's power/ground
- Verify SD line connection
- Try swapping the two mics to isolate bad unit

## Next Steps

Once you confirm both microphones are working:
1. Note the working pin numbers
2. We'll integrate with ESP_SR library
3. Add speaker output on I2S1
4. Build the full speech recognition prototype

## Sample Serial Output (Working)

```
=== INMP441 Dual Mic Pin Test ===
I2S Pins: BCK=18, WS=19, DIN=20
Configuring I2S...

I2S initialized successfully!
Reading audio data...

Tap or blow on each microphone to test.
You should see different peak values for LEFT vs RIGHT.

LEFT:  ████████                                 (  45231)  |  RIGHT:                                          (    892)
LEFT:  ██                                       (   3421)  |  RIGHT: ████████████                             ( 125432)
LEFT:  ███████████████                          ( 234567)  |  RIGHT: ██                                       (   2341)
```

The bars should respond independently to tapping each microphone!
