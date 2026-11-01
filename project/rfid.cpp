#include "rfid.h"
#include <SPI.h>

namespace {

    struct Card {
        byte uid[4];
        int mood;
    };

    const Card cards[] = {
        {{0x9E, 0x7B, 0x0F, 0x05}, MOOD_STUDY},
        {{0xF1, 0x5B, 0x88, 0x01}, MOOD_SLEEP}
    };

    const int CARD_COUNT =
        sizeof(cards) / sizeof(cards[0]);

    const unsigned long CARD_LOST_TIMEOUT_MS = 300;
}


RFIDController::RFIDController(byte ssPin, byte rstPin)
    : reader(ssPin, rstPin),
      currentMood(MOOD_NONE),
      lastReportedMood(MOOD_NONE),
      lastCardSeenAt(0) {
}


void RFIDController::begin() {

    SPI.begin();
    reader.PCD_Init();

    Serial.println("RFID ready");
}


void RFIDController::readCard() {

    byte atqa[2];
    byte atqaSize = sizeof(atqa);

    MFRC522::StatusCode status =
        reader.PICC_WakeupA(atqa, &atqaSize);

    if (status != MFRC522::STATUS_OK &&
        status != MFRC522::STATUS_COLLISION) {
        return;
    }

    if (!reader.PICC_ReadCardSerial()) {
        return;
    }

    int found = MOOD_NONE;

    if (reader.uid.size == 4) {

        for (int c = 0; c < CARD_COUNT; c++) {

            bool match = true;

            for (byte i = 0; i < 4; i++) {
                if (reader.uid.uidByte[i] !=
                    cards[c].uid[i]) {

                    match = false;
                    break;
                }
            }

            if (match) {
                found = cards[c].mood;
                break;
            }
        }
    }

    if (found != MOOD_NONE) {
        currentMood = found;
        lastCardSeenAt = millis();
    }

    reader.PICC_HaltA();
    reader.PCD_StopCrypto1();
}


void RFIDController::reportMoodChange() {

    if (currentMood == lastReportedMood) {
        return;
    }

    lastReportedMood = currentMood;

    switch (currentMood) {

        case MOOD_STUDY:
            Serial.println("Mood: Study");
            break;

        case MOOD_SLEEP:
            Serial.println("Mood: Sleep");
            break;

        default:
            Serial.println("Mood: Default");
            break;
    }
}


void RFIDController::update() {

    readCard();

    // إذا لم تعد البطاقة تُقرأ، نعود إلى Default
    if (currentMood != MOOD_NONE &&
        millis() - lastCardSeenAt >
            CARD_LOST_TIMEOUT_MS) {

        currentMood = MOOD_NONE;
    }

    reportMoodChange();
}


int RFIDController::getMood() const {
    return currentMood;
}