#!/bin/bash
# Upload files to ESP32 FFat partition using mkfatfs

set -e

echo "ESP32-S3 FFat Flash Upload Tool"
echo "================================"
echo

# Check if data directory exists
if [ ! -d "data" ]; then
    echo "ERROR: data/ folder not found!"
    exit 1
fi

# Check if video file exists
if [ ! -f "data/output.mjpeg" ]; then
    echo "ERROR: data/output.mjpeg not found!"
    exit 1
fi

echo "Found video file: data/output.mjpeg"
FILE_SIZE=$(stat -c%s "data/output.mjpeg" 2>/dev/null || stat -f%z "data/output.mjpeg")
echo "Size: $FILE_SIZE bytes ($(($FILE_SIZE / 1024)) KB)"
echo

# Find mkfatfs tool
MKFATFS=$(find ~/.arduino15/packages/esp32/tools/mkfatfs -name "mkfatfs" -type f 2>/dev/null | head -1)

if [ -z "$MKFATFS" ]; then
    # Try local temp folder
    MKFATFS="./temp/mkfatfs"
    if [ ! -f "$MKFATFS" ]; then
        echo "ERROR: mkfatfs tool not found!"
        echo "Please run: ./install_mkfatfs.sh"
        exit 1
    fi
fi

echo "Found tool: $MKFATFS"
echo

# FFat partition details from app3M_fat9M_16MB.csv
# ffat, data, fat, 0x610000, 0x9E0000
PARTITION_OFFSET="0x610000"
PARTITION_SIZE="10354688"  # 0x9E0000 in decimal

# According to notes: image is flashed with +4096 bytes (0x1000) offset
FLASH_OFFSET="0x611000"  # 0x610000 + 0x1000

echo "Creating FAT filesystem image..."
echo "Partition size: $PARTITION_SIZE bytes"

$MKFATFS -c data -t fatfs -s $PARTITION_SIZE ffat.bin

if [ ! -f "ffat.bin" ]; then
    echo "ERROR: Failed to create filesystem image"
    exit 1
fi

IMAGE_SIZE=$(stat -c%s "ffat.bin" 2>/dev/null || stat -f%z "ffat.bin")
echo "Filesystem image created: ffat.bin ($IMAGE_SIZE bytes)"
echo

# Find esptool
ESPTOOL=$(which esptool.py 2>/dev/null || which esptool 2>/dev/null || echo "")

if [ -z "$ESPTOOL" ]; then
    ESPTOOL=$(find ~/.arduino15/packages/esp32/tools -name "esptool.py" -type f 2>/dev/null | head -1)
fi

if [ -z "$ESPTOOL" ]; then
    echo "ERROR: esptool not found!"
    exit 1
fi

echo "Found esptool: $ESPTOOL"
echo

# Find ESP32 port
echo "Looking for ESP32 device..."
PORT=""

for p in /dev/ttyUSB* /dev/ttyACM* /dev/cu.usbserial* /dev/cu.usbmodem*; do
    if [ -e "$p" ]; then
        PORT="$p"
        break
    fi
done

if [ -z "$PORT" ]; then
    echo "ERROR: No ESP32 device found!"
    exit 1
fi

echo "Found ESP32 on: $PORT"
echo

echo "Uploading to flash..."
echo "Flash offset: $FLASH_OFFSET (+0x1000 from partition start)"
echo
read -p "Continue? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Cancelled."
    exit 0
fi

# Upload to ESP32 (with 0x1000 offset from partition start)
python3 $ESPTOOL --chip esp32s3 --port $PORT --baud 460800 \
    write_flash $FLASH_OFFSET ffat.bin

echo
echo "SUCCESS! Flash upload complete"
echo
echo "Now:"
echo "1. Press RESET button on ESP32"
echo "2. Open Serial Monitor (115200 baud)"
echo "3. Video should play!"
echo

# Cleanup
rm -f ffat.bin
echo "Cleaned up temporary file (ffat.bin)"
