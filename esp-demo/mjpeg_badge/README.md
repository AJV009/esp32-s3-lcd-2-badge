# ESP32-S3-LCD-2 Event Badge - MJPEG Video Player

A continuous-loop MJPEG video player for the ESP32-S3-LCD-2 display board, perfect for event badges and digital signage.

## Features

- 🎬 Hardware-accelerated MJPEG decoding using ESP32-S3 JPEG decoder
- 🔄 Infinite loop playback
- 📺 Full-screen 320x240 video display
- 💾 Plays from SD card
- ⚡ Optimized performance with SIMD operations
- 📊 Real-time FPS and performance statistics

## Hardware Requirements

- **ESP32-S3-LCD-2** development board by Waveshare
  - 2.0" IPS LCD display (240x320, ST7789 controller)
  - ESP32-S3 with 8MB PSRAM and 16MB Flash
  - MicroSD card slot
- **MicroSD card** (formatted as FAT32, 32GB or less recommended)
- **USB-C cable** for programming

## Software Requirements

### Arduino IDE Setup

1. **Arduino IDE** 2.3.6 or newer
2. **ESP32 Board Support** version >= 3.0.0
   - Board: "ESP32S3 Dev Module"
3. **Required Libraries:**
   - `GFX_Library_for_Arduino` v1.5.0 or newer (should already be installed)
   - `ESP32_JPEG_Library` (included in ESP32 core >= 3.0)

### Arduino IDE Board Configuration

Go to **Tools** menu and configure:

```
Board: "ESP32S3 Dev Module"
USB CDC On Boot: "Enabled"
Flash Size: "16MB (128Mb)"
Partition Scheme: "16M Flash (3MB APP/9.9MB FATFS)"
PSRAM: "OPI PSRAM"
Upload Mode: "USB-OTG CDC (TinyUSB)"
```

## Installation & Usage

### Step 1: Prepare SD Card

1. Format your microSD card as **FAT32**
2. Copy `output.mjpeg` to the **root directory** of the SD card
3. The file structure should be:
   ```
   SD Card (root)
   └── output.mjpeg
   ```
4. Insert the microSD card into the ESP32-S3-LCD-2 board

### Step 2: Upload Sketch

1. Open `mjpeg_badge.ino` in Arduino IDE
2. Select the correct COM port under **Tools > Port**
3. Click **Upload** (or press Ctrl+U)
4. Wait for compilation and upload to complete

### Step 3: Run

- The badge will automatically start playing the video in a loop
- Serial monitor (115200 baud) shows performance statistics
- Video plays continuously until power is removed

## Troubleshooting

### "SD Card Error!" on display

**Problem:** SD card cannot be mounted

**Solutions:**
- Ensure SD card is properly inserted
- Reformat SD card as FAT32
- Try a different SD card (some cards are not compatible)
- Check that card is 32GB or smaller

### "File Not Found!" on display

**Problem:** `output.mjpeg` is not on the SD card

**Solutions:**
- Verify the file is named exactly `output.mjpeg` (lowercase)
- Ensure the file is in the root directory (not in a folder)
- Re-copy the file to the SD card

### "Memory Error!" on display

**Problem:** Not enough RAM for video buffers

**Solutions:**
- Ensure PSRAM is enabled in board settings
- Try reducing `MJPEG_BUFFER_SIZE` in the code
- Use a lower resolution video

### Video plays too fast or too slow

**Solution:** Adjust frame delay in `loop()` function:
```cpp
delay(33);  // ~30 FPS (1000ms / 30 = 33ms per frame)
```

### Compilation Errors

**"ESP32_JPEG_Library.h: No such file"**
- Update ESP32 board support to version >= 3.0.0
- The ESP32_JPEG library is included in newer ESP32 cores

**"Arduino.h: No such file"** (Linux only)
- This was already fixed in your codebase!
- Make sure you're using the corrected examples

## Performance

On ESP32-S3 @ 240MHz with hardware JPEG decoder:
- **Decoding:** ~15-30 ms per frame (depends on frame complexity)
- **Display:** ~5-10 ms per frame
- **FPS:** Typically 25-40 FPS for 320x240 video

## Video Format Specifications

The player expects MJPEG files with these specifications:

- **Format:** Motion JPEG (concatenated JPEG frames)
- **Resolution:** 320x240 pixels (matches landscape display)
- **Color:** RGB (will be converted to RGB565)
- **File size:** Recommended < 10MB for smooth SD card reading

### Creating Compatible Videos

Use FFmpeg to convert videos to the correct format:

```bash
ffmpeg -i input.mp4 \
  -vf "scale=320:240:force_original_aspect_ratio=decrease,pad=320:240:(ow-iw)/2:(oh-ih)/2" \
  -q:v 8 \
  -r 15 \
  output.mjpeg
```

Options explained:
- `-vf scale=320:240` - Resize to 320x240
- `-q:v 8` - JPEG quality (2-31, lower = better quality, larger file)
- `-r 15` - Frame rate (15 FPS recommended for SD card performance)

## Code Structure

```
mjpeg_badge/
├── mjpeg_badge.ino    # Main Arduino sketch
├── MjpegClass.h       # MJPEG parser and decoder
└── README.md          # This file
```

### Key Components

**mjpeg_badge.ino:**
- Display initialization (ST7789 driver)
- SD card mounting
- Main playback loop with restart logic
- Performance monitoring

**MjpegClass.h:**
- MJPEG format parser (finds JPEG boundaries)
- ESP32-S3 hardware JPEG decoder wrapper
- Memory management for frame buffers

## Customization

### Change Video File

Edit line 31 in `mjpeg_badge.ino`:
```cpp
#define MJPEG_FILENAME    "/your_video.mjpeg"
```

### Adjust Buffer Sizes

For different resolution videos, modify lines 32-33:
```cpp
#define MJPEG_BUFFER_SIZE (width * height * 2 / 10)  // Compressed data
#define OUTPUT_BUFFER_SIZE (width * height * 2)      // RGB565 frame
```

### Display Rotation

Change display rotation (line 27):
```cpp
#define LCD_ROTATION  0   // Portrait (240x320)
#define LCD_ROTATION  1   // Landscape (320x240) - default
#define LCD_ROTATION  2   // Portrait flipped
#define LCD_ROTATION  3   // Landscape flipped
```

## Technical Details

### Hardware JPEG Decoding

The ESP32-S3 includes a hardware JPEG decoder that can decompress JPEG images much faster than software decoding:

- **Input:** Baseline JPEG (most common format)
- **Output:** RGB565 (native display format)
- **Performance:** ~10x faster than software decoding

### Memory Layout

```
PSRAM (8MB available):
├── MJPEG Buffer: ~15KB    (compressed JPEG data)
├── Output Buffer: ~150KB   (320x240 RGB565 frame)
└── System: Remaining
```

### SPI Bus Sharing

The display and SD card share the same SPI bus (FSPI):
- **Display CS:** GPIO 45
- **SD Card CS:** GPIO 41
- Both devices use the same SCLK, MOSI, MISO pins

This is handled automatically by the code with proper CS pin management.

## Credits

- **Based on:** Arduino_GFX MJPEG examples
- **Hardware:** Waveshare ESP32-S3-LCD-2
- **Libraries:** Arduino_GFX by moononournation
- **ESP32 JPEG:** Espressif ESP32_JPEG_Library

## License

This code is provided as-is for the OAISYS25 Badge Project.
Feel free to modify and adapt for your event badge needs!

## Support

For issues or questions:
1. Check the troubleshooting section above
2. Verify all hardware connections
3. Check serial monitor output for error messages
4. Ensure ESP32 board support is >= 3.0.0

## Version History

- **v1.0** (2025-11-05): Initial release
  - Infinite loop playback
  - Hardware JPEG decoding
  - Full error handling and diagnostics
  - Performance monitoring

---

**Created for OAISYS25 Event Badge Project**
