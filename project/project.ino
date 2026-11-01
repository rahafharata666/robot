#include "gyro.h"
#include "RobotOLED.h"
#include "Touch.h"
#include "rfid.h"
#include <WiFi.h>
#include <WebServer.h>

#include <WiFi.h>
#include <WebServer.h>

const int TOUCH_PIN = 27;
const float ANGLE = 15.0f;

const unsigned long HAPPY_HOLD_MS = 2000;
const unsigned long DIZZY_HOLD_MS = 2000;

RobotOLED robot;

RobotOLED::Mode currentFace = RobotOLED::Mode::Normal;

int previousMainMood = -999;

Touch touch(TOUCH_PIN,HAPPY_HOLD_MS);

gyro gyrosensor(ANGLE,DIZZY_HOLD_MS);

RFIDController rfidReader; 

WebServer server(80);

const char* AP_SSID = "robot";
const char* AP_PASSWORD = "12345678";

// 0 = Auto, 1 = Default, 2 = Study, 3 = Sleep
int webMoodSelection = 0;

// التحكم اليدوي بالوجه
bool manualFaceActive = false;

RobotOLED::Mode webFaceSelection =
    RobotOLED::Mode::Normal;

// الرسالة القادمة من الموبايل
String webMessage = "";
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ar" dir="rtl">
<head>
  <meta charset="UTF-8">
  <meta name="viewport"
        content="width=device-width, initial-scale=1">

  <title>RE BOT Control</title>

  <style>
    body {
      font-family: Arial, sans-serif;
      background: #f1f5f9;
      margin: 0;
      padding: 20px;
      color: #172033;
    }

    .container {
      max-width: 430px;
      margin: auto;
    }

    .card {
      background: white;
      padding: 18px;
      margin-bottom: 16px;
      border-radius: 14px;
    }

    h2 {
      margin-top: 0;
    }

    select, input, button {
      width: 100%;
      box-sizing: border-box;
      padding: 12px;
      margin-top: 10px;
      border-radius: 8px;
      font-size: 16px;
    }

    select, input {
      border: 1px solid #cbd5e1;
      background: white;
    }

    button {
      border: none;
      background: #2563eb;
      color: white;
      cursor: pointer;
    }

    #status {
      overflow-wrap: anywhere;
    }
  </style>
</head>

<body>
<div class="container">

  <h1>🤖 RE BOT</h1>
  <p>لوحة التحكم بالروبوت</p>

  <div class="card">
    <h2>اختيار الوجه</h2>

    <select id="face">
      <option value="auto">تلقائي - الحساسات</option>
      <option value="normal">عادي</option>
      <option value="happy">سعيد</option>
      <option value="sad">حزين</option>
      <option value="angry">غاضب</option>
      <option value="confused">محتار</option>
      <option value="timer">Timer</option>
    </select>

    <button onclick="sendChoice('/face', 'face')">
      تطبيق الوجه
    </button>
  </div>

  <div class="card">
    <h2>وضع الروبوت</h2>

    <select id="mood">
      <option value="auto">تلقائي - RFID</option>
      <option value="default">Default</option>
      <option value="study">Study</option>
      <option value="sleep">Sleep</option>
    </select>

    <button onclick="sendChoice('/mood', 'mood')">
      تغيير الوضع
    </button>
  </div>

  <div class="card">
    <h2>إرسال رسالة</h2>

    <input id="message"
           maxlength="80"
           placeholder="اكتبي رسالة للروبوت">

    <button onclick="sendMessage()">
      إرسال الرسالة
    </button>
  </div>

  <div class="card">
    <b>حالة الاتصال:</b>
    <p id="status">جاهز</p>
  </div>

</div>

<script>
async function sendChoice(route, elementId) {
  const value = document.getElementById(elementId).value;

  try {
    const response = await fetch(
      route + '?value=' + encodeURIComponent(value)
    );

    document.getElementById('status').textContent =
      await response.text();
  } catch (error) {
    document.getElementById('status').textContent =
      'تعذر الاتصال بالروبوت';
  }
}

async function sendMessage() {
  const value = document.getElementById('message').value;

  try {
    const response = await fetch(
      '/message?value=' + encodeURIComponent(value)
    );

    document.getElementById('status').textContent =
      await response.text();
  } catch (error) {
    document.getElementById('status').textContent =
      'تعذر إرسال الرسالة';
  }
}
</script>

</body>
</html>
)rawliteral";

void handleRoot() {
    server.send(
        200,
        "text/html; charset=utf-8",
        INDEX_HTML
    );
}

void handleSetFace() {
    String value = server.arg("value");

    if (value == "auto") {
        manualFaceActive = false;
    }
    else if (value == "normal") {
        webFaceSelection = RobotOLED::Mode::Normal;
        manualFaceActive = true;
    }
    else if (value == "happy") {
        webFaceSelection = RobotOLED::Mode::Happy;
        manualFaceActive = true;
    }
    else if (value == "sad") {
        webFaceSelection = RobotOLED::Mode::Sad;
        manualFaceActive = true;
    }
    else if (value == "angry") {
        webFaceSelection = RobotOLED::Mode::Angry;
        manualFaceActive = true;
    }
    else if (value == "confused") {
        webFaceSelection = RobotOLED::Mode::Confused;
        manualFaceActive = true;
    }
    else if (value == "timer") {
        webFaceSelection = RobotOLED::Mode::Timer;
        manualFaceActive = true;
    }
    else {
        server.send(400, "text/plain", "Unknown face");
        return;
    }

    server.send(200, "text/plain; charset=utf-8",
                "تم استقبال أمر الوجه");
}

void handleSetMood() {
    String value = server.arg("value");

    if (value == "auto") {
        webMoodSelection = 0;
    }
    else if (value == "default") {
        webMoodSelection = 1;
    }
    else if (value == "study") {
        webMoodSelection = 2;
    }
    else if (value == "sleep") {
        webMoodSelection = 3;
    }
    else {
        server.send(400, "text/plain", "Unknown mode");
        return;
    }

    server.send(200, "text/plain; charset=utf-8",
                "تم استقبال أمر الوضع");
}

void handleMessage() {
    webMessage = server.arg("value");
    webMessage.trim();

    if (webMessage.length() > 80) {
        webMessage = webMessage.substring(0, 80);
    }

    Serial.print("Message from phone: ");
    Serial.println(webMessage);

    // عرض الرسالة على شاشة OLED لمدة 3 ثوانٍ
    robot.showMessage(webMessage, 3000UL);

    server.send(
        200,
        "text/plain; charset=utf-8",
        "تم إرسال الرسالة إلى الروبوت"
    );
}

void updateDefaultMode(unsigned long now) {
    // لا يوجد كود إضافي حالياً.
    // Touch و Gyro يعملان في هذا المود أيضاً.
}

void updateStudyMode(unsigned long now) {
    // ضعي كود الدراسة هنا لاحقاً.
    // هذا التابع يُستدعى باستمرار أثناء Study.
}

void updateSleepMode(unsigned long now) {
    // ضعي كود النوم هنا لاحقاً.
    // هذا التابع يُستدعى باستمرار أثناء Sleep.
}
void onMainMoodChanged(int mood) {

    switch (mood) {

        case MOOD_STUDY:
            Serial.println("Entered Study Mode");
            // كود يحدث مرة واحدة عند الدخول إلى الدراسة
            break;

        case MOOD_SLEEP:
            Serial.println("Entered Sleep Mode");
            // كود يحدث مرة واحدة عند الدخول إلى النوم
            break;

        default:
            Serial.println("Entered Default Mode");
            // كود يحدث مرة واحدة عند العودة إلى الوضع الافتراضي
            break;
    }
}
void setup() {

    Serial2.begin(115200, SERIAL_8N1, 16, 17);

    Serial.begin(115200);

    Wire.begin(21, 22);

    touch.begin();

    robot.begin();

    gyrosensor.begin();

    rfidReader.begin();

    robot.setMode(currentFace);

    WiFi.mode(WIFI_AP);

if (WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("Robot WiFi started");
    Serial.print("WiFi name: ");
    Serial.println(AP_SSID);

    Serial.print("Open this address: http://");
    Serial.println(WiFi.softAPIP());
}
else {
    Serial.println("Failed to start Robot WiFi");
}

server.on("/", handleRoot);
server.on("/face", handleSetFace);
server.on("/mood", handleSetMood);
server.on("/message", handleMessage);

server.begin();

Serial.println("Web server started");

    Serial.println("Robot Ready");

    
 }

void loop() {

    server.handleClient();

    unsigned long now = millis();

    touch.update(now);
    gyrosensor.update(now);
    rfidReader.update();

    RobotOLED::Mode nextMode;
int mainMood;

switch (webMoodSelection) {
    case 1:
        mainMood = -1;  // Default
        break;

    case 2:
        mainMood = MOOD_STUDY;
        break;

    case 3:
        mainMood = MOOD_SLEEP;
        break;

    default:
        mainMood = rfidReader.getMood();
        break;
}
    if (mainMood != previousMainMood) {
        previousMainMood = mainMood;
        onMainMoodChanged(mainMood);
    }
    switch (mainMood) {

        case MOOD_STUDY:
            updateStudyMode(now);
            break;

        case MOOD_SLEEP:
            updateSleepMode(now);
            break;

        default:
            updateDefaultMode(now);
            break;
    }

    // 5. اختيار الوجه الذي يظهر على OLED
    RobotOLED::Mode nextFace;

if (manualFaceActive) {
    nextFace = webFaceSelection;
}
else if (touch.isActive(now)) {
    nextFace = RobotOLED::Mode::Happy;
}
else if (gyrosensor.isActive(now)) {
    nextFace = RobotOLED::Mode::Confused;
}
else if (mainMood == MOOD_SLEEP) {
    nextFace = RobotOLED::Mode::Sad;
}
else {
    nextFace = RobotOLED::Mode::Normal;
}

if (nextFace != currentFace) {
    currentFace = nextFace;
    robot.setMode(currentFace);
}

    if (Serial2.available()) {
    String result = Serial2.readStringUntil('\n');
    result.trim();

    if (result.length() > 0) {
        Serial.println(result);

        // هون منربط النتيجة بتصرفات الروبوت
    }
}

    robot.update();
}
   