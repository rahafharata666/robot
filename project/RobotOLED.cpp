#include "RobotOLED.h"
#include <math.h>

namespace {
    const int EYE_R = 18;
    const int PUPIL_R = 8;
}

void RobotOLED::begin() {
  oled.init();
  oled.clear();
  oled.update();
}
// set mode
void RobotOLED::setMode(RobotOLED::Mode newMode) {

  if (currentMode == newMode) {
    return;
  }

  stopCurrentAnimation();
  currentMode = newMode;

  if (newMode == RobotOLED::Mode::Timer) {
    timerStartMs = millis();
    lastTimerSecond = 0xFFFFFFFFUL;
  }

  // Do not erase a message currently requested from the web-control page.
  if (!messageActive) {
    oled.clear();
    oled.update();
  }
}

void RobotOLED::stopCurrentAnimation() {
  switch (currentMode) {
    case Mode::Normal:
      animateNaturalEyes(false);
      break;
    case Mode::Sad:
      drawSadFace(false);
      break;
    case Mode::Angry:
      drawAngryEyes(false);
      break;
    case Mode::Happy:
      drawHappyEyes(false);
      break;
    case Mode::Confused:
      drawConfusedEyes(false);
      break;
    case Mode::Timer:
      lastTimerSecond = 0xFFFFFFFFUL;
      break;
  }
}

void RobotOLED::showMessage(const String& message, unsigned long durationMs) {
  // Reset the selected face's animation state so it redraws after the message.
  stopCurrentAnimation();

  messageStartMs = millis();
  messageDurationMs = durationMs;
  messageActive = true;

  drawMessageText(message);
}

void RobotOLED::drawMessageText(const String& message) {
  const uint8_t MAX_COLUMNS = 21;  // Approx. 21 characters at text scale 1.
  const uint8_t MAX_ROWS = 8;

  oled.clear();
  oled.setScale(1);

  String line = "";
  uint8_t row = 0;
  int i = 0;

  while (i < message.length() && row < MAX_ROWS) {
    char ch = message[i++];

    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      oled.setCursor(0, row++);
      oled.print(line);
      line = "";
      continue;
    }

    if (line.length() >= MAX_COLUMNS) {
      // Prefer wrapping at a space rather than splitting a word.
      int breakAt = line.lastIndexOf(' ');
      if (breakAt >= 0) {
        String remainder = line.substring(breakAt + 1);
        line = line.substring(0, breakAt);
        oled.setCursor(0, row++);
        oled.print(line);
        line = remainder;
      } else {
        oled.setCursor(0, row++);
        oled.print(line);
        line = "";
      }

      if (row >= MAX_ROWS) {
        break;
      }
    }

    line += ch;
  }

  if (row < MAX_ROWS && line.length() > 0) {
    oled.setCursor(0, row);
    oled.print(line);
  }

  oled.update();
}

void RobotOLED::update() {

  // While a message is being shown, pause face animation so it cannot erase the text.
  if (messageActive) {
    if ((unsigned long)(millis() - messageStartMs) < messageDurationMs) {
      return;
    }

    // Message duration has ended. Clear the text and resume the selected face.
    messageActive = false;
    oled.clear();
    oled.update();

    // Force the timer screen to redraw if Timer is the selected mode.
    if (currentMode == Mode::Timer) {
      lastTimerSecond = 0xFFFFFFFFUL;
    }
  }

  switch (currentMode) {

    case RobotOLED::Mode::Normal:
      animateNaturalEyes(true);
      break;

    case RobotOLED::Mode::Sad:
      drawSadFace(true);
      break;

    case RobotOLED::Mode::Angry:
      drawAngryEyes(true);
      break;

    case RobotOLED::Mode::Happy:
      drawHappyEyes(true);
      break;
      
    case RobotOLED::Mode::Confused:
      drawConfusedEyes(true);
      break;
    case RobotOLED::Mode::Timer: {

  unsigned long totalSeconds =
      (millis() - timerStartMs) / 1000UL;

  if (totalSeconds != lastTimerSecond) {

    lastTimerSecond = totalSeconds;

    unsigned long hours = totalSeconds / 3600UL;
    unsigned long minutes = (totalSeconds / 60UL) % 60UL;
    unsigned long seconds = totalSeconds % 60UL;

    oled.clear();

    oled.setScale(1);
    oled.setCursor(5, 1);
    oled.print("Study Time");

    // عرض HH:MM:SS
    oled.setScale(2);
    oled.setCursor(1, 3);

    if (hours < 10) oled.print('0');
    oled.print(hours);
    oled.print(':');

    if (minutes < 10) oled.print('0');
    oled.print(minutes);
    oled.print(':');

    if (seconds < 10) oled.print('0');
    oled.print(seconds);

    oled.update();
  }

  break;
 }
  }
}
//natural mode
void RobotOLED::drawEyes(int pupilOffsetX, bool closed) {

  oled.clear();

  const int LEFT_X  = 33;
  const int RIGHT_X = 92;

  const int EYE_Y  = 28;  
  const int EYE_RX = 16;  
  const int EYE_RY = 12;  

  const int PUPIL_R = 10; 

  if (!closed) {

    for (int dy = -EYE_RY; dy <= EYE_RY; dy++) {

      float t = (float)dy / EYE_RY;

      int halfWidth = (int)(
        EYE_RX * sqrt(1.0 - t * t)
      );

      int y = EYE_Y + dy;

      oled.line(
        LEFT_X - halfWidth, y,
        LEFT_X + halfWidth, y
      );

      oled.line(
        RIGHT_X - halfWidth, y,
        RIGHT_X + halfWidth, y
      );
    }

    int leftPupilX  = LEFT_X + pupilOffsetX;
    int rightPupilX = RIGHT_X + pupilOffsetX;

    oled.circle(leftPupilX, EYE_Y, PUPIL_R, OLED_CLEAR);
    oled.circle(rightPupilX, EYE_Y, PUPIL_R, OLED_CLEAR);

    oled.dot(leftPupilX - 3, EYE_Y - 3, 1);
    oled.dot(rightPupilX - 3, EYE_Y - 3, 1);

  } else {

    oled.line(17, 29, 25, 31);
    oled.line(25, 31, 33, 32);
    oled.line(33, 32, 41, 31);
    oled.line(41, 31, 49, 29);

    oled.line(17, 30, 25, 32);
    oled.line(25, 32, 33, 33);
    oled.line(33, 33, 41, 32);
    oled.line(41, 32, 49, 30);

    oled.line(76, 29, 84, 31);
    oled.line(84, 31, 92, 32);
    oled.line(92, 32, 100, 31);
    oled.line(100, 31, 108, 29);

    oled.line(76, 30, 84, 32);
    oled.line(84, 32, 92, 33);
    oled.line(92, 33, 100, 32);
    oled.line(100, 32, 108, 30);
  }

  oled.line(18, 16, 24, 12);
  oled.line(24, 12, 31, 10);
  oled.line(31, 10, 38, 12);
  oled.line(38, 12, 44, 16);

  oled.line(18, 17, 24, 13);
  oled.line(24, 13, 31, 11);
  oled.line(31, 11, 38, 13);
  oled.line(38, 13, 44, 17);

 oled.line(81, 16, 87, 12);
 oled.line(87, 12, 94, 10);
 oled.line(94, 10, 101, 12);
 oled.line(101, 12, 107, 16);

 oled.line(81, 17, 87, 13);
 oled.line(87, 13, 94, 11);
 oled.line(94, 11, 101, 13);
 oled.line(101, 13, 107, 17);

  const byte mouthX[] = {
    53, 57, 61, 65, 69, 73, 77
  };

  const byte mouthY[] = {
    53, 55, 57, 58, 57, 55, 53
  };

  for (byte i = 0; i < 6; i++) {

    oled.line(
      mouthX[i], mouthY[i],
      mouthX[i + 1], mouthY[i + 1]
    );

    oled.line(
      mouthX[i], mouthY[i] + 1,
      mouthX[i + 1], mouthY[i + 1] + 1
    );
  }

  oled.update();
}
void RobotOLED::drawEyesRight() {
  drawEyes(5, false);
}
void RobotOLED::drawEyesLeft() {
  drawEyes(-5, false);
}
void RobotOLED::drawBlink() {
  drawEyes(0, true);
}
void RobotOLED::animateNaturalEyes(bool enabled) {

  static bool started = false;
  static byte state = 0;
  static byte blinkCount = 0;

  static unsigned long lastBlinkTime = 0;
  static unsigned long stateTime = 0;

  unsigned long now = millis();

  if (!enabled) {
    started = false;
    return;
  }

  if (!started) {
    started = true;
    state = 0;
    blinkCount = 0;

    drawEyes(0, false);
    lastBlinkTime = now;
    return;
  }

  switch (state) {

    case 0:

      if (now - lastBlinkTime >= 4000UL) {
        drawBlink();

        state = 1;
        stateTime = now;
      }

      break;

    case 1:

      if (now - stateTime >= 140UL) {

        blinkCount++;
        lastBlinkTime = now;

        if (blinkCount >= 3) {
          drawEyesRight();

          state = 2;
          stateTime = now;
        }
        else {
          drawEyes(0, false);
          state = 0;
        }
      }

      break;

    case 2:

      if (now - stateTime >= 450UL) {
        drawEyesLeft();

        state = 3;
        stateTime = now;
      }

      break;

    case 3:

      if (now - stateTime >= 450UL) {
        drawEyes(0, false);

        blinkCount = 0;
        lastBlinkTime = now;
        state = 0;
      }

      break;
  }
}

void RobotOLED::drawHappyEyes(bool enabled) {
  static bool drawn = false;

  if (!enabled) {
    drawn = false;
    return;
  }

  if (drawn) {
    return;
  }

  static unsigned long lastFrame = 0;
  static bool firstFrame = true;
  static byte cheekPhase = 0;

  unsigned long now = millis();

 if (!firstFrame && now - lastFrame < 180UL) {
  return;
 }

 firstFrame = false;
 lastFrame = now;

  oled.clear();

  const byte xL[] = {
    18, 23, 28, 33, 38, 43, 48
  };

  const byte yL[] = {
    35, 31, 28, 26, 28, 31, 35
  };

  for (byte t = 0; t < 3; t++) {

    for (byte i = 0; i < 6; i++) {

      oled.line(
        xL[i], yL[i] + t,
        xL[i + 1], yL[i + 1] + t
      );

    }
  }


  const byte xR[] = {
    77, 82, 87, 92, 97, 102, 107
  };

  const byte yR[] = {
    35, 31, 28, 26, 28, 31, 35
  };

  for (byte t = 0; t < 3; t++) {

    for (byte i = 0; i < 6; i++) {

      oled.line(
        xR[i], yR[i] + t,
        xR[i + 1], yR[i + 1] + t
      );

    }
  }

  oled.line(53, 50, 77, 50);
  oled.line(53, 51, 77, 51);


  oled.line(53, 51, 54, 54);
  oled.line(54, 54, 57, 57);
  oled.line(57, 57, 61, 59);
  oled.line(61, 59, 65, 60);

  oled.line(53, 52, 54, 55);
  oled.line(54, 55, 57, 58);
  oled.line(57, 58, 61, 60);

  oled.line(65, 60, 69, 59);
  oled.line(69, 59, 73, 57);
  oled.line(73, 57, 76, 54);
  oled.line(76, 54, 77, 51);

  oled.line(65, 61, 69, 60);
  oled.line(69, 60, 73, 58);
  oled.line(73, 58, 76, 55);
  oled.line(76, 55, 77, 52);


  oled.line(61, 57, 69, 57);
  oled.line(61, 58, 69, 58);

 const byte cheekRadius[4] = {1, 2, 3, 2};

 oled.circle(
  22, 44,
  cheekRadius[cheekPhase],
  OLED_FILL
 );

 oled.circle(
  103, 44,
  cheekRadius[cheekPhase],
  OLED_FILL
 );


  oled.update();
  cheekPhase++;

 if (cheekPhase >= 4) {
  cheekPhase = 0;
 }
}
//angry mode
void RobotOLED::drawAngryEyes(bool enabled) {

  static bool drawn = false;

  if (!enabled) {
    drawn = false;
    return;
  }

  if (drawn) {
    return;
  }

  drawn = true;

  oled.clear();

  const int LEFT_X  = 33;
  const int RIGHT_X = 92;

  const int EYE_Y  = 32;
  const int EYE_RX = 16;
  const int EYE_RY = 9;

  for (int dy = -EYE_RY; dy <= EYE_RY; dy++) {

    float t = (float)dy / EYE_RY;

    int halfWidth = (int)(
      EYE_RX * sqrt(1.0 - t * t)
    );

    int y = EYE_Y + dy;

    oled.line(
      LEFT_X - halfWidth, y,
      LEFT_X + halfWidth, y
    );

    oled.line(
      RIGHT_X - halfWidth, y,
      RIGHT_X + halfWidth, y
    );
  }

  const int PUPIL_R = 6;
  const int PUPIL_Y = 34;

  int leftPupilX  = LEFT_X + 4;
  int rightPupilX = RIGHT_X - 4;

  oled.circle(
    leftPupilX, PUPIL_Y,
    PUPIL_R, OLED_CLEAR
  );

  oled.circle(
    rightPupilX, PUPIL_Y,
    PUPIL_R, OLED_CLEAR
  );

  oled.dot(leftPupilX - 2, PUPIL_Y - 3, 1);
  oled.dot(rightPupilX - 2, PUPIL_Y - 3, 1);

 oled.line(17, 13, 48, 25);
  oled.line(17, 14, 48, 26);

 oled.line(111, 13, 80, 25);
 oled.line(111, 14, 80, 26);


  const byte mouthX[] = {
    53, 58, 63, 68, 73, 78
  };
 
  const byte mouthY[] = {
    52, 53, 54, 56, 58, 59
  };

  for (byte i = 0; i < 5; i++) {

    oled.line(
      mouthX[i], mouthY[i],
      mouthX[i + 1], mouthY[i + 1]
    );

    oled.line(
      mouthX[i], mouthY[i] + 1,
      mouthX[i + 1], mouthY[i + 1] + 1
    );
  }

  oled.update();
}
//sad mode
void RobotOLED::drawSadFace(bool enabled) {

  static unsigned long lastFrame = 0;
  static bool firstFrame = true;
  static byte mouthPhase = 0;

  if (!enabled) {
    firstFrame = true;
    return;
  }
  unsigned long now = millis();
   
  if (!firstFrame && now - lastFrame < 180UL) {
    return;
  }

  firstFrame = false;
  lastFrame = now;

  oled.clear();


 for (int dy = -9; dy <= 9; dy++) {

  float t = (float)dy / 9.0;

  int halfWidth =
      (int)(16.0 * sqrt(1.0 - t * t));

  int leftCenterX = 33 - (dy + 9) / 4;

  int rightCenterX = 92 + (dy + 9) / 4;

  int y = 32 + dy;

  oled.line(
    leftCenterX - halfWidth, y,
    leftCenterX + halfWidth, y
  );

  oled.line(
    rightCenterX - halfWidth, y,
    rightCenterX + halfWidth, y
  );
 }



 oled.circle(32, 35, 5, OLED_CLEAR);
 oled.circle(93, 35, 5, OLED_CLEAR);

 oled.dot(30, 33, 1);
 oled.dot(91, 33, 1);


  oled.line(18, 23, 42, 18);
  oled.line(18, 24, 42, 19);

  oled.line(85, 18, 109, 23);
  oled.line(85, 19, 109, 24);

  const byte mouthX[] = {
    53, 57, 61, 65, 69, 73, 77
  };

  const byte mouthY[4][7] = {

    {58, 53, 57, 51, 56, 53, 58},
    {58, 55, 51, 56, 52, 55, 58},
    {58, 52, 56, 53, 50, 56, 58},
    {58, 54, 50, 55, 51, 54, 58}
  };

  for (byte i = 0; i < 6; i++) {

    int x1 = mouthX[i];
    int x2 = mouthX[i + 1];

    int y1 = mouthY[mouthPhase][i];
    int y2 = mouthY[mouthPhase][i + 1];

    oled.line(x1, y1, x2, y2);

    oled.line(x1, y1 + 1, x2, y2 + 1);
  }

  oled.update();

  mouthPhase++;

  if (mouthPhase >= 4) {
    mouthPhase = 0;
  }
}
// Confused mode
void RobotOLED::drawConfusedEyes(bool enabled) {

  static unsigned long lastFrame = 0;
  unsigned long now = millis();

  if (!enabled) {
    lastFrame = 0;
    return;
  }

  if (now - lastFrame < 25) return;
  lastFrame = now;

  const int leftX  = 30;
  const int rightX = 98;
  const int eyeY   = 30;

  const int eyeR   = 16;
  const int pupilR = 5;

  float t = now / 1000.0f;

  int pupilLX = leftX + (int)(7 * cos(t * 12));
  int pupilLY = eyeY  + (int)(5 * sin(t * 16));

  int pupilRX = rightX + (int)(7 * cos(t * 15 + 1.7f));
  int pupilRY = eyeY   + (int)(5 * sin(t * 19 + 0.8f));

  oled.clear();

  oled.circle(leftX, eyeY, eyeR, OLED_FILL);
  oled.circle(rightX, eyeY, eyeR, OLED_FILL);

  oled.circle(pupilLX, pupilLY, pupilR, OLED_CLEAR);
  oled.dot(pupilLX - 2, pupilLY - 2, 1);

  oled.circle(pupilRX, pupilRY, pupilR, OLED_CLEAR);
  oled.dot(pupilRX - 2, pupilRY - 2, 1);

 const int mouthX = 64;
 const int mouthY = 52;
 const int radiusX = 10;
 const int radiusY = 6;

 for (int deg = 0; deg < 360; deg += 5) {
  float a1 = deg * 0.0174533f;
  float a2 = (deg + 5) * 0.0174533f;

  int x1 = mouthX + (int)(radiusX * cos(a1));
  int y1 = mouthY + (int)(radiusY * sin(a1));

  int x2 = mouthX + (int)(radiusX * cos(a2));
  int y2 = mouthY + (int)(radiusY * sin(a2));

  oled.line(x1, y1, x2, y2);
 }
  oled.update();
}
