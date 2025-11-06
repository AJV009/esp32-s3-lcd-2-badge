#!/bin/bash
set -e

echo "ESP32-S3 FFat Upload Tool"
echo "========================="
echo

if [ ! -d "data" ]; then
    echo "ERROR: data/ folder not found!"
    exit 1
fi

FILE_COUNT=$(find data -type f | wc -l)
if [ "$FILE_COUNT" -eq 0 ]; then
    echo "ERROR: data/ folder is empty!"
    exit 1
fi

echo "Files to upload:"
find data -type f -exec bash -c 'FILE="{}"; SIZE=$(stat -c%s "$FILE" 2>/dev/null || stat -f%z "$FILE"); echo "  $(basename "$FILE"): $(($SIZE / 1024)) KB"' \;
echo "Total: $FILE_COUNT files"
echo

MKFATFS=$(find ~/.arduino15/packages/esp32/tools/mkfatfs -name "mkfatfs" -type f 2>/dev/null | head -1)
if [ -z "$MKFATFS" ]; then
    MKFATFS="./temp/mkfatfs"
    if [ ! -f "$MKFATFS" ]; then
        echo "ERROR: mkfatfs not found!"
        exit 1
    fi
fi

PARTITION_SIZE="10354688"
FLASH_OFFSET="0x611000"

echo "Creating filesystem image..."
$MKFATFS -c data -t fatfs -s $PARTITION_SIZE ffat.bin

ESPTOOL=$(which esptool.py 2>/dev/null || which esptool 2>/dev/null || find ~/.arduino15/packages/esp32/tools -name "esptool.py" -type f 2>/dev/null | head -1)
if [ -z "$ESPTOOL" ]; then
    echo "ERROR: esptool not found!"
    exit 1
fi

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

echo "Device: $PORT"
read -p "Upload to flash? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    exit 0
fi

python3 $ESPTOOL --chip esp32s3 --port $PORT --baud 460800 write_flash $FLASH_OFFSET ffat.bin

echo
echo "SUCCESS! Files uploaded to FFat partition"
echo "Press RESET on ESP32"
rm -f ffat.bin
