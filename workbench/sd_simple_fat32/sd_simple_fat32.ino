/*
 * Robust FAT32 SD Card Implementation for ESP32-S3-LCD-2
 *
 * This implementation was built to work with older/slower SD cards that fail
 * with standard Arduino SD library due to timing issues.
 *
 * CRITICAL TIMING REQUIREMENTS DISCOVERED:
 * 1. 200ms power-up delay after dummy clocks before CMD0
 * 2. 10ms delays after CMD0, CMD8, and successful ACMD41
 * 3. 20ms delays between ACMD41 retry attempts
 * 4. 250ms recovery delay after write operations before next command
 * 5. 50 dummy clock cycles (0xFF) after completing a write
 * 6. 8 dummy bytes before data token in write operations
 *
 * These delays prevent card state corruption and ensure compatibility
 * with cards that need more time for internal operations.
 */

#include "SPI.h"

#define SD_CS   41
#define SD_MOSI 38
#define SD_MISO 40
#define SD_SCK  39

// SD state
bool sdReady = false;
bool isSDHC = false;

// FAT32 filesystem state
uint32_t fatStart = 0;           // First sector of FAT table
uint32_t dataStart = 0;          // First sector of data area
uint32_t rootDirCluster = 0;    // Root directory cluster number
uint32_t sectorsPerFAT = 0;     // Sectors per FAT table
uint8_t sectorsPerCluster = 0;  // Sectors per cluster
uint8_t numFATs = 0;            // Number of FAT tables

uint8_t sectorBuffer[512];      // Primary buffer for FAT32 operations
uint8_t workBuffer[512];        // Secondary buffer for multi-step operations

int testFileNum = 0;            // Current test file number for stress testing

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n=== FAT32 Stress Test ===");

  if (!initSD()) {
    Serial.println("✗ SD init failed");
    return;
  }

  Serial.println("✓ SD ready");

  if (!mountFAT32()) {
    Serial.println("✗ FAT32 mount failed");
    return;
  }

  Serial.println("✓ FAT32 mounted");

  // Find the highest numbered test file
  testFileNum = findHighestTestFile();
  Serial.printf("Last test file: TEST%04d.TXT\n", testFileNum);

  // Append to previous file if it exists
  if (testFileNum > 0) {
    char prevFilename[13];
    sprintf(prevFilename, "TEST%04d.TXT", testFileNum);

    Serial.printf("\n--- Appending to %s ---\n", prevFilename);

    char appendText[32];
    sprintf(appendText, "Edit #%d\n", testFileNum + 1);

    if (appendToFile(prevFilename, appendText, strlen(appendText))) {
      Serial.printf("✓ Appended %d bytes to %s\n", strlen(appendText), prevFilename);
    } else {
      Serial.printf("✗ Failed to append to %s\n", prevFilename);
    }
  }

  // Create next test file
  testFileNum++;
  char filename[13];
  sprintf(filename, "TEST%04d.TXT", testFileNum);

  Serial.printf("\n--- Creating %s ---\n", filename);

  char content[64];
  sprintf(content, "Test file #%d\nReset count: %d\nStress test\n",
          testFileNum, testFileNum);

  if (createFile(filename, content, strlen(content))) {
    Serial.printf("✓ Created %s (%d bytes)\n", filename, strlen(content));
  } else {
    Serial.printf("✗ Failed to create %s\n", filename);
  }

  Serial.println("\n--- All Files ---");
  listDirectory(rootDirCluster);

  Serial.println("\n✓ Ready for continuous testing");
}

void loop() {
  // Periodic health check every 10 seconds
  delay(10000);

  if (!sdReady) {
    Serial.println("Card not ready");
    return;
  }

  // Verify card is still responsive
  if (!readSector(0, workBuffer)) {
    Serial.println("✗ Card health check failed");
    sdReady = false;
    return;
  }

  Serial.printf("✓ Card healthy - %d files created\n", testFileNum);
}

// ============ SD Low-Level Functions ============

bool initSD() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  SPI.setFrequency(400000);

  delay(200);
  for (int i = 0; i < 10; i++) SPI.transfer(0xFF);
  delay(10);

  for (int retry = 0; retry < 3; retry++) {
    uint8_t r1 = sendCmd(0, 0, 0x95);
    if (r1 == 0x01) {
      delay(10);

      r1 = sendCmd(8, 0x1AA, 0x87);
      for (int i = 0; i < 4; i++) SPI.transfer(0xFF);
      bool isV2 = (r1 == 0x01);
      delay(10);

      int attempts = 0;
      while (attempts < 200) {
        sendCmd(55, 0, 0xFF);
        r1 = sendCmd(41, isV2 ? 0x40000000 : 0, 0xFF);
        if (r1 == 0x00) break;
        attempts++;
        delay(20);
      }

      if (r1 == 0x00) {
        delay(10);

        uint8_t ocr[5];
        ocr[0] = sendCmd(58, 0, 0xFF);
        for (int i = 1; i < 5; i++) ocr[i] = SPI.transfer(0xFF);
        isSDHC = (ocr[1] & 0x40);

        SPI.setFrequency(10000000);

        if (!isSDHC) {
          sendCmd(16, 512, 0xFF);
        }

        sdReady = true;
        return true;
      }
    }

    delay(500);
  }

  return false;
}

uint8_t sendCmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
  digitalWrite(SD_CS, HIGH);
  SPI.transfer(0xFF);
  delayMicroseconds(100);
  digitalWrite(SD_CS, LOW);
  delayMicroseconds(100);

  SPI.transfer(0x40 | cmd);
  SPI.transfer((arg >> 24) & 0xFF);
  SPI.transfer((arg >> 16) & 0xFF);
  SPI.transfer((arg >> 8) & 0xFF);
  SPI.transfer(arg & 0xFF);
  SPI.transfer(crc);

  uint8_t response;
  for (int i = 0; i < 20; i++) {
    response = SPI.transfer(0xFF);
    if (!(response & 0x80)) break;
    delayMicroseconds(100);
  }

  digitalWrite(SD_CS, HIGH);
  SPI.transfer(0xFF);
  return response;
}

// Read a single 512-byte sector from SD card
// Uses CMD17 (READ_SINGLE_BLOCK)
bool readSector(uint32_t sector, uint8_t *buffer) {
  if (!sdReady) return false;

  // Convert sector to address (SDHC uses sector addressing, SD uses byte addressing)
  uint32_t addr = isSDHC ? sector : (sector * 512);

  // CS toggle with timing for card to be ready
  digitalWrite(SD_CS, HIGH);
  SPI.transfer(0xFF);
  delayMicroseconds(100);
  digitalWrite(SD_CS, LOW);
  delayMicroseconds(100);

  // Send CMD17 (READ_SINGLE_BLOCK)
  SPI.transfer(0x40 | 17);
  SPI.transfer((addr >> 24) & 0xFF);
  SPI.transfer((addr >> 16) & 0xFF);
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);
  SPI.transfer(0xFF);  // CRC (ignored in SPI mode)

  // Wait for R1 response (not 0xFF, MSB=0)
  uint8_t r1;
  for (int i = 0; i < 20; i++) {
    r1 = SPI.transfer(0xFF);
    if (!(r1 & 0x80)) break;
  }

  if (r1 != 0x00) {
    digitalWrite(SD_CS, HIGH);
    SPI.transfer(0xFF);
    return false;
  }

  // Wait for data start token (0xFE)
  uint8_t token;
  int timeout = 100000;
  while (timeout-- > 0) {
    token = SPI.transfer(0xFF);
    if (token == 0xFE) break;
  }

  if (token != 0xFE) {
    digitalWrite(SD_CS, HIGH);
    SPI.transfer(0xFF);
    return false;
  }

  // Read 512 bytes of data
  for (int i = 0; i < 512; i++) {
    buffer[i] = SPI.transfer(0xFF);
  }

  // Read 16-bit CRC (ignored but required by protocol)
  SPI.transfer(0xFF);
  SPI.transfer(0xFF);

  // Deselect card
  digitalWrite(SD_CS, HIGH);
  SPI.transfer(0xFF);

  return true;
}

// Write a single 512-byte sector to SD card
// Uses CMD24 (WRITE_SINGLE_BLOCK)
// CRITICAL: Requires 250ms recovery delay after completion
bool writeSector(uint32_t sector, const uint8_t *buffer) {
  if (!sdReady) return false;

  // Convert sector to address
  uint32_t addr = isSDHC ? sector : (sector * 512);

  // CS toggle with timing
  digitalWrite(SD_CS, HIGH);
  SPI.transfer(0xFF);
  delayMicroseconds(100);
  digitalWrite(SD_CS, LOW);
  delayMicroseconds(100);

  // Send CMD24 (WRITE_SINGLE_BLOCK)
  SPI.transfer(0x40 | 24);
  SPI.transfer((addr >> 24) & 0xFF);
  SPI.transfer((addr >> 16) & 0xFF);
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);
  SPI.transfer(0xFF);

  // Wait for R1 response
  uint8_t r1;
  for (int i = 0; i < 20; i++) {
    r1 = SPI.transfer(0xFF);
    if (!(r1 & 0x80)) break;
  }

  if (r1 != 0x00) {
    digitalWrite(SD_CS, HIGH);
    SPI.transfer(0xFF);
    return false;
  }

  // CRITICAL: Send 8 dummy bytes (Nac) before data token
  // Some cards need this delay to prepare for data reception
  for (int i = 0; i < 8; i++) SPI.transfer(0xFF);

  // Send data start token
  SPI.transfer(0xFE);

  // Write 512 bytes of data
  for (int i = 0; i < 512; i++) {
    SPI.transfer(buffer[i]);
  }

  // Write 16-bit CRC (ignored but required)
  SPI.transfer(0xFF);
  SPI.transfer(0xFF);

  // Wait for data response token (not 0xFF)
  uint8_t resp;
  int respTimeout = 100;
  do {
    resp = SPI.transfer(0xFF);
  } while (resp == 0xFF && respTimeout-- > 0);

  // Check data response (xxx0_0101 = accepted)
  if ((resp & 0x1F) != 0x05) {
    // Data rejected - must still wait for busy to clear!
    int timeout = 100000;
    while (timeout-- > 0) {
      if (SPI.transfer(0xFF) != 0x00) break;
    }
    digitalWrite(SD_CS, HIGH);
    SPI.transfer(0xFF);
    return false;
  }

  // Wait for card busy signal to clear (card programming flash)
  // Card sends 0x00 while busy, non-zero when done
  uint32_t busyStart = millis();
  while (millis() - busyStart < 1000) {
    if (SPI.transfer(0xFF) != 0x00) break;
  }

  if (millis() - busyStart >= 1000) {
    digitalWrite(SD_CS, HIGH);
    SPI.transfer(0xFF);
    return false;
  }

  // CRITICAL: Send 50 dummy clocks after busy clears
  digitalWrite(SD_CS, HIGH);
  for (int i = 0; i < 50; i++) SPI.transfer(0xFF);

  // CRITICAL: 250ms delay before next operation
  // Without this, the card enters an error state and all subsequent
  // operations fail with R1=0xFF or R1=0x40 (parameter error)
  delay(250);

  return true;
}

// ============ FAT32 File Management Functions ============

// Convert cluster number to first sector of that cluster
uint32_t clusterToSector(uint32_t cluster) {
  return dataStart + ((cluster - 2) * sectorsPerCluster);
}

// Find a free cluster by scanning FAT table
// Returns 0 if no free cluster found
uint32_t findFreeCluster() {
  // Start searching from cluster 3 (cluster 0,1,2 are reserved/root)
  for (uint32_t cluster = 3; cluster < 10000; cluster++) {
    uint32_t fatSector = fatStart + (cluster * 4) / 512;
    uint32_t fatOffset = (cluster * 4) % 512;

    if (!readSector(fatSector, workBuffer)) return 0;

    uint32_t fatEntry = *((uint32_t*)&workBuffer[fatOffset]) & 0x0FFFFFFF;

    if (fatEntry == 0) {
      return cluster;
    }
  }
  return 0;
}

// Mark a cluster as end-of-chain in FAT table
bool markClusterEOC(uint32_t cluster) {
  uint32_t fatSector = fatStart + (cluster * 4) / 512;
  uint32_t fatOffset = (cluster * 4) % 512;

  // Read FAT sector
  if (!readSector(fatSector, workBuffer)) return false;

  // Write end-of-chain marker (0x0FFFFFFF)
  *((uint32_t*)&workBuffer[fatOffset]) = 0x0FFFFFFF;

  // Write back to both FAT tables
  if (!writeSector(fatSector, workBuffer)) return false;
  if (numFATs > 1) {
    if (!writeSector(fatSector + sectorsPerFAT, workBuffer)) return false;
  }

  return true;
}

// Mount FAT32 filesystem and extract filesystem parameters
bool mountFAT32() {
  // Read MBR (sector 0)
  if (!readSector(0, sectorBuffer)) return false;

  uint8_t partType = sectorBuffer[446 + 4];

  uint32_t partStart;
  if (partType == 0xEE) {
    // GPT partition - read partition table
    if (!readSector(2, sectorBuffer)) return false;
    partStart = *((uint32_t*)&sectorBuffer[32]);

    if (partStart == 0) {
      if (!readSector(1, sectorBuffer)) return false;
      uint64_t partArrayLBA = *((uint64_t*)&sectorBuffer[72]);
      if (!readSector(partArrayLBA, sectorBuffer)) return false;
      partStart = *((uint32_t*)&sectorBuffer[32]);
    }
  } else {
    // MBR partition
    partStart = *((uint32_t*)&sectorBuffer[446 + 8]);
  }

  // Read FAT32 boot sector
  if (!readSector(partStart, sectorBuffer)) return false;

  // Extract filesystem parameters
  uint16_t bytesPerSector = *((uint16_t*)&sectorBuffer[11]);
  sectorsPerCluster = sectorBuffer[13];
  uint16_t reservedSectors = *((uint16_t*)&sectorBuffer[14]);
  numFATs = sectorBuffer[16];
  sectorsPerFAT = *((uint32_t*)&sectorBuffer[36]);
  rootDirCluster = *((uint32_t*)&sectorBuffer[44]);

  // Calculate start sectors
  fatStart = partStart + reservedSectors;
  dataStart = fatStart + (numFATs * sectorsPerFAT);

  return (bytesPerSector == 512 && sectorsPerCluster > 0);
}

// Find the highest numbered TEST####.TXT file in root directory
int findHighestTestFile() {
  int highest = 0;
  uint32_t sector = clusterToSector(rootDirCluster);

  for (int s = 0; s < sectorsPerCluster; s++) {
    if (!readSector(sector + s, sectorBuffer)) return highest;

    for (int i = 0; i < 512; i += 32) {
      uint8_t *entry = &sectorBuffer[i];

      if (entry[0] == 0x00) return highest;  // End of directory
      if (entry[0] == 0xE5) continue;        // Deleted
      if (entry[11] == 0x0F) continue;       // Long filename

      // Check for TEST####.TXT pattern
      if (memcmp(entry, "TEST", 4) == 0 &&
          memcmp(entry + 8, "TXT", 3) == 0) {
        // Parse number from entry[4:7]
        int num = 0;
        for (int j = 4; j < 8; j++) {
          if (entry[j] >= '0' && entry[j] <= '9') {
            num = num * 10 + (entry[j] - '0');
          }
        }
        if (num > highest) highest = num;
      }
    }
  }
  return highest;
}

// Append data to an existing file
// LIMITATION: File must fit in single cluster (< 512 bytes after append)
bool appendToFile(const char *filename, const char *appendData, uint16_t appendSize) {
  // Convert filename to FAT32 8.3 format
  char fatName[11];
  memset(fatName, ' ', 11);

  int nameLen = 0;
  int extLen = 0;
  bool inExt = false;

  for (int i = 0; filename[i] && i < 13; i++) {
    if (filename[i] == '.') {
      inExt = true;
      nameLen = i;
      continue;
    }
    if (inExt) {
      if (extLen < 3) fatName[8 + extLen++] = filename[i];
    } else {
      if (nameLen < 8) fatName[nameLen++] = filename[i];
    }
  }

  // Find file in root directory
  uint32_t dirSector = clusterToSector(rootDirCluster);

  for (int s = 0; s < sectorsPerCluster; s++) {
    if (!readSector(dirSector + s, sectorBuffer)) return false;

    for (int i = 0; i < 512; i += 32) {
      uint8_t *entry = &sectorBuffer[i];

      if (entry[0] == 0x00) return false;  // Not found
      if (entry[0] == 0xE5) continue;      // Deleted
      if (entry[11] == 0x0F) continue;     // Long filename

      // Check if this is the file we're looking for
      if (memcmp(entry, fatName, 11) == 0) {
        // Found the file - get its cluster and size
        uint16_t clusterHigh = *((uint16_t*)&entry[20]);
        uint16_t clusterLow = *((uint16_t*)&entry[26]);
        uint32_t fileCluster = ((uint32_t)clusterHigh << 16) | clusterLow;
        uint32_t currentSize = *((uint32_t*)&entry[28]);

        // Check if append would overflow single cluster
        if (currentSize + appendSize > 512) {
          return false;  // File too large for this simple implementation
        }

        // Read current file content
        uint32_t fileSector = clusterToSector(fileCluster);
        if (!readSector(fileSector, workBuffer)) return false;

        // Append new data
        memcpy(workBuffer + currentSize, appendData, appendSize);

        // Write back to file
        if (!writeSector(fileSector, workBuffer)) return false;

        // Update file size in directory entry
        *((uint32_t*)&entry[28]) = currentSize + appendSize;

        // Write directory sector back
        return writeSector(dirSector + s, sectorBuffer);
      }
    }
  }

  return false;  // File not found
}

// Create a new file with given name and content
// Filename must be in 8.3 format (e.g., "TEST0001.TXT")
bool createFile(const char *filename, const char *content, uint16_t size) {
  // Convert filename to FAT32 8.3 format
  char fatName[11];
  memset(fatName, ' ', 11);

  int nameLen = 0;
  int extLen = 0;
  bool inExt = false;

  for (int i = 0; filename[i] && i < 13; i++) {
    if (filename[i] == '.') {
      inExt = true;
      nameLen = i;
      continue;
    }
    if (inExt) {
      if (extLen < 3) fatName[8 + extLen++] = filename[i];
    } else {
      if (nameLen < 8) fatName[nameLen++] = filename[i];
    }
  }

  // Find free cluster for file data
  uint32_t fileCluster = findFreeCluster();
  if (fileCluster == 0) return false;

  // Mark cluster as end-of-chain in FAT
  if (!markClusterEOC(fileCluster)) return false;

  // Write file content to cluster
  uint32_t fileSector = clusterToSector(fileCluster);
  memset(workBuffer, 0, 512);
  memcpy(workBuffer, content, size < 512 ? size : 512);
  if (!writeSector(fileSector, workBuffer)) return false;

  // Find empty directory entry in root
  uint32_t dirSector = clusterToSector(rootDirCluster);

  for (int s = 0; s < sectorsPerCluster; s++) {
    if (!readSector(dirSector + s, sectorBuffer)) return false;

    for (int i = 0; i < 512; i += 32) {
      uint8_t *entry = &sectorBuffer[i];

      if (entry[0] == 0x00 || entry[0] == 0xE5) {
        // Found empty slot - create directory entry
        memcpy(entry, fatName, 11);                    // Filename
        entry[11] = 0x20;                               // Archive attribute
        entry[12] = 0;                                  // Reserved
        entry[13] = 0;                                  // Creation time (tenths)
        *((uint16_t*)&entry[14]) = 0;                  // Creation time
        *((uint16_t*)&entry[16]) = 0;                  // Creation date
        *((uint16_t*)&entry[18]) = 0;                  // Last access date
        *((uint16_t*)&entry[20]) = (fileCluster >> 16) & 0xFFFF;  // Cluster high
        *((uint16_t*)&entry[22]) = 0;                  // Last write time
        *((uint16_t*)&entry[24]) = 0;                  // Last write date
        *((uint16_t*)&entry[26]) = fileCluster & 0xFFFF;  // Cluster low
        *((uint32_t*)&entry[28]) = size;               // File size

        // Write directory sector back
        return writeSector(dirSector + s, sectorBuffer);
      }
    }
  }

  return false;  // No free directory entry
}

// List all files in a directory
void listDirectory(uint32_t dirCluster) {
  uint32_t sector = clusterToSector(dirCluster);

  for (int s = 0; s < sectorsPerCluster; s++) {
    if (!readSector(sector + s, sectorBuffer)) return;

    // Parse directory entries (32 bytes each)
    for (int i = 0; i < 512; i += 32) {
      uint8_t *entry = &sectorBuffer[i];

      if (entry[0] == 0x00) return;  // End of directory
      if (entry[0] == 0xE5) continue;  // Deleted entry
      if (entry[11] == 0x0F) continue;  // Long filename entry
      if (entry[11] & 0x08) continue;  // Volume label

      // Print file or directory
      if (entry[11] & 0x10) {
        Serial.print("DIR:  ");
      } else {
        Serial.print("FILE: ");
      }

      // Print filename in 8.3 format
      for (int j = 0; j < 8; j++) {
        if (entry[j] != ' ') Serial.write(entry[j]);
      }
      if (entry[8] != ' ') {
        Serial.print(".");
        for (int j = 8; j < 11; j++) {
          if (entry[j] != ' ') Serial.write(entry[j]);
        }
      }

      uint32_t fileSize = *((uint32_t*)&entry[28]);
      Serial.printf(" (%lu bytes)\n", fileSize);
    }
  }
}

bool readFileByName(uint32_t dirCluster, const char *name) {
  uint32_t sector = clusterToSector(dirCluster);

  for (int s = 0; s < sectorsPerCluster; s++) {
    if (!readSector(sector + s, sectorBuffer)) return false;

    for (int i = 0; i < 512; i += 32) {
      uint8_t *entry = &sectorBuffer[i];

      if (entry[0] == 0x00) return false;
      if (entry[0] == 0xE5) continue;
      if (entry[11] == 0x0F) continue;

      // Compare filename
      bool match = true;
      for (int j = 0; j < 11; j++) {
        if (entry[j] != name[j]) {
          match = false;
          break;
        }
      }

      if (match) {
        uint16_t clusterHigh = *((uint16_t*)&entry[20]);
        uint16_t clusterLow = *((uint16_t*)&entry[26]);
        uint32_t fileCluster = ((uint32_t)clusterHigh << 16) | clusterLow;
        uint32_t fileSize = *((uint32_t*)&entry[28]);

        Serial.printf("Found: %s, Size: %lu, Cluster: %lu\n",
                      name, fileSize, fileCluster);

        // Read first sector of file
        uint32_t fileSector = clusterToSector(fileCluster);
        if (readSector(fileSector, sectorBuffer)) {
          Serial.println("Content (first 256 bytes):");
          for (int k = 0; k < 256 && k < fileSize; k++) {
            if (sectorBuffer[k] >= 32 && sectorBuffer[k] < 127) {
              Serial.write(sectorBuffer[k]);
            } else if (sectorBuffer[k] == '\n' || sectorBuffer[k] == '\r') {
              Serial.write(sectorBuffer[k]);
            }
          }
          Serial.println();
          return true;
        }
      }
    }
  }

  return false;
}
