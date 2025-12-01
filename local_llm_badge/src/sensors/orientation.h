/*******************************************************************************
 * OAISYS Badge - Orientation Manager
 * Debounced rotation detection using IMU accelerometer
 ******************************************************************************/

#pragma once

#include <Arduino.h>

class OrientationManager {
public:
    OrientationManager();

    // Initialize IMU, returns false if IMU not available
    bool begin();

    // Call in loop() - polls IMU at configured interval
    void update();

    // Get current stable rotation (1 = USB right, 3 = USB left)
    uint8_t getRotation() const { return _currentRotation; }

    // Check if rotation just changed (one-shot, auto-clears)
    bool hasChanged();

    // Check if IMU is available
    bool isAvailable() const { return _imuAvailable; }

private:
    static const float THRESHOLD;           // Hysteresis threshold (g)
    static const unsigned long DEBOUNCE_MS; // Stability time required
    static const unsigned long POLL_MS;     // Poll interval

    uint8_t _currentRotation;     // Stable rotation
    uint8_t _pendingRotation;     // Candidate during debounce (0 = none)
    unsigned long _debounceStart;
    unsigned long _lastPoll;
    bool _rotationChanged;        // One-shot flag
    bool _imuAvailable;
};
