#ifndef GYRO_H
#define GYRO_H

#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>


class gyro {

private:

    Adafruit_MPU6050 mpu;

    float thresholdAngle;

    unsigned long holdDuration;
    unsigned long lastTiltTime;

    bool gyroReady;
    bool tiltDetected;

    float filteredAngle;
    bool angleInitialized;

    float readTiltAngle();


public:

    gyro(float tiltAngle,unsigned long holdMs);

    void begin();

    void update(unsigned long now);

    bool isActive(unsigned long now) const;
};

#endif