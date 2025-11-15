#!/bin/bash
set -e

echo "ESP32-S3 LLM Model Upload Tool"
echo "==============================="
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

echo "Model files to upload:"
find data -type f -exec bash -c 'FILE="{}"; SIZE=$(stat -c%s "$FILE" 2>/dev/null || stat -f%z "$FILE"); echo "  $(basename "$FILE"): $(($SIZE / 1024)) KB"' \;
echo "Total: $FILE_COUNT files"
echo

# Find mkfatfs
MKFATFS=$(find ~/.arduino15/packages/esp32/tools/mkfatfs -name "mkfatfs" -type f 2>/dev/null | head -1)
if [ -z "$MKFATFS" ]; then
    MKFATFS="./temp/mkfatfs"
    if [ ! -f "$MKFATFS" ]; then
        echo "ERROR: mkfatfs not found!"
        echo "Install it via Arduino IDE or ESP-IDF"
        exit 1
    fi
fi

# FFat partition configuration (9MB partition)
PARTITION_SIZE="10354688"   # 9MB + header
FLASH_OFFSET="0x611000"     # FFat partition offset

echo "Creating filesystem image..."
$MKFATFS -c data -t fatfs -s $PARTITION_SIZE ffat.bin

if [ ! -f "ffat.bin" ]; then
    echo "ERROR: Failed to create filesystem image!"
    exit 1
fi

# Find esptool
ESPTOOL=$(which esptool.py 2>/dev/null || which esptool 2>/dev/null || find ~/.arduino15/packages/esp32/tools -name "esptool.py" -type f 2>/dev/null | head -1)
if [ -z "$ESPTOOL" ]; then
    echo "ERROR: esptool not found!"
    exit 1
fi

# Find ESP32 port
PORT=""
for p in /dev/ttyUSB* /dev/ttyACM* /dev/cu.usbserial* /dev/cu.usbmodem*; do
    if [ -e "$p" ]; then
        PORT="$p"
        break
    fi
done

if [ -z "$PORT" ]; then
    echo "ERROR: No ESP32 device found!"
    echo "Available ports:"
    ls /dev/tty* 2>/dev/null | grep -E "(USB|ACM|usbserial|usbmodem)" || echo "  None"
    exit 1
fi

echo "Device: $PORT"
echo "Partition: FFat @ $FLASH_OFFSET ($((PARTITION_SIZE / 1024 / 1024))MB)"
echo
read -p "Upload model files to flash? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    rm -f ffat.bin
    exit 0
fi

echo
echo "Uploading..."
python3 $ESPTOOL --chip esp32s3 --port $PORT --baud 460800 write_flash $FLASH_OFFSET ffat.bin

echo
echo "========================================="
echo "SUCCESS! Model files uploaded to FFat"
echo "========================================="
echo
echo "Next steps:"
echo "1. Open llm_inference.ino in Arduino IDE"
echo "2. Select board: ESP32S3 Dev Module"
echo "3. Configure: PSRAM OPI, 240MHz CPU"
echo "4. Upload sketch"
echo "5. Open Serial Monitor (115200 baud)"
echo

rm -f ffat.bin
