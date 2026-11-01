#include "gyro.h"
#include <Wire.h>
#include <math.h>


gyro::gyro(float tiltAngle,unsigned long holdMs): 
  thresholdAngle(tiltAngle),
  holdDuration(holdMs),
  lastTiltTime(0),
  gyroReady(false),
  tiltDetected(false),
  filteredAngle(0.0f),
  angleInitialized(false) {
}
void gyro::begin() {

    if (!mpu.begin()) {

        Serial.println("Failed to find MPU6050 chip");

        gyroReady = false;

        return;
    }

    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);

    mpu.setGyroRange(MPU6050_RANGE_500_DEG);

    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

    delay(100);

    gyroReady = true;
}

float gyro::readTiltAngle() {

    sensors_event_t a, g, temp;

    mpu.getEvent(&a, &g, &temp);

    float horizontalAcceleration = sqrt(
        a.acceleration.x * a.acceleration.x +
        a.acceleration.y * a.acceleration.y
    );

    float angle = atan2(
        horizontalAcceleration,
        fabs(a.acceleration.z)
    ) * 180.0f / PI;

    return angle;
}

void gyro::update(unsigned long now) {

    if (!gyroReady) {
        return;
    }

    float angle = readTiltAngle();

    if (isnan(angle)) {
        return;
    }

    if (!angleInitialized) {

        filteredAngle = angle;

        angleInitialized = true;

    } else {

        filteredAngle +=
            0.25f * (angle - filteredAngle);
    }

    if (filteredAngle >= thresholdAngle) {

        tiltDetected = true;

        lastTiltTime = now;
    }
}

bool gyro::isActive(unsigned long now) const {

    return tiltDetected && (now - lastTiltTime < holdDuration);
}