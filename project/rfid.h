#ifndef RFID_H
#define RFID_H

#include <Arduino.h>
#include <MFRC522.h>

enum RFIDMood {
    MOOD_NONE  = 0,
    MOOD_STUDY = 1,
    MOOD_SLEEP = 2
};

class RFIDController {

public:
    RFIDController(byte ssPin = 5, byte rstPin = 4);

    void begin();
    void update();

    int getMood() const;

private:
    MFRC522 reader;

    int currentMood;
    int lastReportedMood;

    unsigned long lastCardSeenAt;

    void readCard();
    void reportMoodChange();
};

#endif