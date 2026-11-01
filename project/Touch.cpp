#include "Touch.h"


Touch::Touch(int pin,unsigned long holdMs): 
  touchPin(pin),
  holdDuration(holdMs),
  lastTouchTime(0),
  touchDetected(false) {
}


void Touch::begin() {
  pinMode(touchPin, INPUT);
}
void Touch::update(unsigned long now) {

  bool touched = digitalRead(touchPin) == HIGH;

  if (touched) {
  touchDetected = true;
  lastTouchTime = now;
  }
}
bool Touch::isActive(unsigned long now) const {

  return touchDetected && (now - lastTouchTime < holdDuration);
}