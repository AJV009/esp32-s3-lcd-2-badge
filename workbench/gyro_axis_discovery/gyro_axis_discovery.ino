/*******************************************************************************
 * QMI8658 Accelerometer Axis Discovery Tool
 *
 * PURPOSE: Identify which accelerometer axis to use for orientation detection
 *
 * INSTRUCTIONS:
 * 1. Upload this sketch to your ESP32-S3-LCD-2
 * 2. Open Serial Monitor at 115200 baud
 * 3. Hold device UPRIGHT with USB connector on RIGHT side (normal viewing)
 *    - Screen facing you
 *    - Device vertical like watching a video
 * 4. Note the three axis values (X, Y, Z)
 * 5. Now ROTATE device 180 degrees (USB connector now on LEFT side)
 * 6. Note which axis value changed from ~+1.0 to ~-1.0 (or vice versa)
 * 7. That axis is what we'll use for rotation detection!
 *
 * Expected: ONE axis should flip sign significantly (near ±1g)
 *           Other axes should stay relatively stable
 ******************************************************************************/

#include <Wire.h>
#include "FastIMU.h"

#define IMU_ADDRESS 0x6B
#define I2C_SDA 48
#define I2C_SCL 47

QMI8658 IMU;
calData calib = {0};
AccelData accelData;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=== QMI8658 Axis Discovery Tool ===");
  Serial.println("Initializing I2C and IMU...");

  Wire.begin(I2C_SDA, I2C_SCL);

  int err = IMU.init(calib, IMU_ADDRESS);
  if (err != 0) {
    Serial.print("ERROR: IMU init failed with code: ");
    Serial.println(err);
    Serial.println("Check I2C connections and IMU address!");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("IMU initialized successfully!");
  Serial.println("\nREADY - Follow the instructions above");
  Serial.println("Reading accelerometer values every 500ms...\n");
  delay(2000);
}

void loop() {
  IMU.update();
  IMU.getAccel(&accelData);

  // Print in clear tabular format
  Serial.print("X: ");
  Serial.print(accelData.accelX, 2);
  Serial.print(" g  |  Y: ");
  Serial.print(accelData.accelY, 2);
  Serial.print(" g  |  Z: ");
  Serial.print(accelData.accelZ, 2);
  Serial.println(" g");

  delay(500);  // Update twice per second for easy reading
}
