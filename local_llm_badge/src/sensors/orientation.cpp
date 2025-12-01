/*******************************************************************************
 * OAISYS Badge - Orientation Manager Implementation
 ******************************************************************************/

#include "orientation.h"
#include "../../config.h"

#include <Wire.h>
#include <FastIMU.h>

// Static constants
const float OrientationManager::THRESHOLD = 0.5f;
const unsigned long OrientationManager::DEBOUNCE_MS = ORIENTATION_DEBOUNCE_MS;
const unsigned long OrientationManager::POLL_MS = ORIENTATION_POLL_MS;

// IMU instance and data
static QMI8658 imu;
static calData calibration = {0};
static AccelData accelData;

OrientationManager::OrientationManager()
    : _currentRotation(1)
    , _pendingRotation(0)
    , _debounceStart(0)
    , _lastPoll(0)
    , _rotationChanged(false)
    , _imuAvailable(false)
{
}

bool OrientationManager::begin() {
    Wire.begin(I2C_SDA, I2C_SCL);

    int err = imu.init(calibration, IMU_ADDRESS);
    _imuAvailable = (err == 0);

    return _imuAvailable;
}

void OrientationManager::update() {
    if (!_imuAvailable) return;

    unsigned long now = millis();
    if (now - _lastPoll < POLL_MS) return;
    _lastPoll = now;

    // Read accelerometer
    imu.update();
    imu.getAccel(&accelData);
    float accelY = accelData.accelY;

    // Determine desired rotation based on accelY
    uint8_t desiredRotation = 0;
    if (accelY > THRESHOLD) {
        desiredRotation = 1;  // USB on right (normal)
    } else if (accelY < -THRESHOLD) {
        desiredRotation = 3;  // USB on left (180 flip)
    } else {
        desiredRotation = _currentRotation;  // Hysteresis zone - keep current
    }

    // State machine logic
    if (desiredRotation == _currentRotation) {
        // Orientation matches current stable state
        _pendingRotation = 0;  // Cancel any pending transition
    } else if (_pendingRotation == 0) {
        // Start new transition
        _pendingRotation = desiredRotation;
        _debounceStart = now;
    } else if (_pendingRotation == desiredRotation) {
        // Transition in progress, check if debounce time elapsed
        if (now - _debounceStart >= DEBOUNCE_MS) {
            // Commit rotation change
            _currentRotation = _pendingRotation;
            _pendingRotation = 0;
            _rotationChanged = true;
        }
    } else {
        // Desired rotation changed during debounce - restart
        _pendingRotation = desiredRotation;
        _debounceStart = now;
    }
}

bool OrientationManager::hasChanged() {
    if (_rotationChanged) {
        _rotationChanged = false;
        return true;
    }
    return false;
}
