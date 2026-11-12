#pragma once
#include <stdint.h>

// QMI8658 on the shared I2C bus. Accel only, low-power rate. Gyro stays off.
// enable=false writes the sensor-disable bit so a muted raise-to-wake costs nothing.
void imuBegin(bool enable);

// Forget the rest pose. Call when the panel has just turned off so the next
// sample becomes the pose a later lift is compared against.
void imuRearm();

// True when the board has stayed tilted from the rest pose for two samples
// (~400 ms). One noisy sample does not count. Call while the screen is off;
// it rate-limits itself to ~5 Hz.
bool imuRaised();
