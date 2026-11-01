#ifndef ROBOT_OLED_H
#define ROBOT_OLED_H

#include <Arduino.h>
#include <GyverOLED.h>

class RobotOLED {


public:

  enum class Mode : uint8_t {
    Normal,
    Sad,
    Angry,
    Happy,
    Confused,
    Timer
  };

  void setMode(Mode newMode);
  void update();
  void begin();

  void showMessage(const String& message, unsigned long durationMs = 3000UL);

  void drawEyes();
  void drawEyesRight();
  void drawEyesLeft();
  void drawBlink();
  void animateNaturalEyes(bool enabled);

  void drawGlassesFace(int pupilOffsetX = 0);
  void drawGlassesBlink(int pupilOffsetX = 0);
  void animateGlassesEyes();

  void drawAngryEyes(bool enabled);
  void drawHappyEyes(bool enabled);
  void drawSadFace(bool enabled);
  void drawConfusedEyes(bool enabled);
  
  private:

  GyverOLED<SSH1106_128x64> oled;

  void drawGlassesFrame();
  
  void drawEyes(int pupilOffsetX, bool closed);
  void stopCurrentAnimation();
  void drawMessageText(const String& message);

  Mode currentMode = Mode::Normal;

  unsigned long timerStartMs = 0;
  unsigned long lastTimerSecond = 0xFFFFFFFFUL;

  bool messageActive = false;
  unsigned long messageStartMs = 0;
  unsigned long messageDurationMs = 3000UL;

};


#endif
