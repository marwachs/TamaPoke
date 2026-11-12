#pragma once
#include <stdint.h>

// QMI8658 on the shared I2C bus. Accel only, low-power rate. Gyro stays off.
// enable=false writes the sensor-disable bit so a muted raise-to-wake costs nothing.
void imuBegin(bool enable);

// True when the board has tilted enough since the previous sample (picked up).
// Call while the screen is off; it rate-limits itself to ~5 Hz.
bool imuRaised();
