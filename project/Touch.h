#ifndef TOUCH_H
#define TOUCH_H

#include <Arduino.h>

class Touch {

private:
    int touchPin;

    unsigned long holdDuration;
    unsigned long lastTouchTime;

    bool touchDetected;

public:
    Touch(int pin, unsigned long holdMs);

    void begin();

    void update(unsigned long now);

    bool isActive(unsigned long now) const;
};

#endif