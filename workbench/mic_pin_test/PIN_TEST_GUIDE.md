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
BCK (Bit Clock):    GPIO 2
WS  (Word Select):  GPIO 4
DIN (Data In):      GPIO 18
```

**IMPORTANT LIMITATIONS:**
- GPIO 19, 20: Occupied by USB (USB_N, USB_P)
- GPIO 33-37: NOT broken out to pin headers (internal only)
- GPIO 2, 4: Camera pins (safe to use if camera not attached)

### Available Alternatives on ESP32-S3-LCD-2:

**Pins broken out to headers** (from PinOut in schematic):
- IO2, IO4, IO6, IO7, IO8, IO9, IO10, IO11, IO12, IO13, IO14, IO15, IO16, IO17, IO18, IO21, IO43, IO44, IO47, IO48

**Truly free pins** (no camera attached):
- **GPIO 18** - Free, not assigned
- **GPIO 2, 4, 6-17** - Camera pins (safe if camera not attached)

**Avoid these** (already in use):
- GPIO 0 (BOOT), 1 (BL), 5 (BAT) - System pins
- GPIO 19, 20 (USB_N, USB_P) - USB occupied
- GPIO 33-37 - **NOT broken out to headers!**
- GPIO 38-42, 45 (SPI/LCD/SD) - Display/SD card
- GPIO 43, 44 (U0_TXD, U0_RXD) - Serial console
- GPIO 46 (TP_INT) - Touch panel interrupt
- GPIO 47, 48 (I2C) - IMU and touch panel

## How to Test Different Pins

1. **Edit lines in the sketch** (line numbers vary by sketch):
   ```cpp
   #define I2S_BCK_PIN   2     // Try different pin here
   #define I2S_WS_PIN    4     // Try different pin here
   #define I2S_DIN_PIN   18    // Try different pin here
   ```

   **Alternative pin combinations** (if camera not attached):
   - 18, 2, 4 (default)
   - 6, 7, 8
   - 10, 11, 12

2. **Re-upload** and check Serial Monitor
3. **Note which pins work** for your final implementation

## Sketch Options

### `mic_pin_test.ino` - Dual Microphone Test
- Tests TWO INMP441 microphones in stereo configuration
- LEFT mic: L/R pin → GND
- RIGHT mic: L/R pin → VDD
- Good for validating stereo recording setup

### `single_mic_test.ino` - Single Microphone Test
- Tests ONE INMP441 microphone
- Simpler wiring and validation
- Use this first to verify basic I2S functionality
- Wire mic L/R pin → GND

## Wiring Diagram

### Dual Microphone (stereo):
```
ESP32-S3-LCD-2                INMP441 #1 (LEFT)       INMP441 #2 (RIGHT)
──────────────                ─────────────────       ──────────────────
3.3V ────────────┬──────────── VDD                    VDD
                 │
GND ─────────────┼──────────── GND ──────────────┬─── GND
                 │                                │
GPIO 2 (BCK) ────┼──────────── SCK ───────────────┼─── SCK
                 │                                │
GPIO 4 (WS) ─────┼──────────── WS ────────────────┼─── WS
                 │                                │
GPIO 18 (DIN) ───┴──────────── SD ────────────────┴─── SD
                                │                     │
                                L/R ──> GND           L/R ──> 3.3V
                                      (LEFT)                (RIGHT)
```

### Single Microphone (mono):
```
ESP32-S3-LCD-2                INMP441
──────────────                ─────────
3.3V ────────────────────────── VDD
GND ─────────────┬───────────── GND
                 │
GPIO 2 (BCK) ────┼───────────── SCK
GPIO 4 (WS) ─────┼───────────── WS
GPIO 18 (DIN) ───┴───────────── SD
                                │
                                L/R ──> GND (for LEFT)
                                    or  3.3V (for RIGHT)
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
