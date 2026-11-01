/*
 * esp32_cam_smart.ino  -  personal-robot vision: up to 7 people, saved in flash.
 *
 * Serial Monitor: 115200 baud, line ending = "Newline".
 *   r Name   -> register the person in front of the camera (e.g. "r Ahmad")
 *   l        -> list registered people
 *   c        -> erase ALL registered people
 *
 * Build settings: Board "AI Thinker ESP32-CAM", PSRAM "Enabled",
 * Partition "Huge APP (3MB No OTA)", arduino-esp32 core 2.0.0 (or 1.0.6).
 */
#include "SmartCameraManager.h"

SmartCameraConfig camCfg;               // edit SSID / password / resolution here
SmartCameraManager camera(camCfg);

void handleSerialCommands() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  char cmd = line[0];
  String arg = line.substring(1);
  arg.trim();

  if (cmd == 'r') {
    Serial.println("Registering... look straight at the camera and keep still.");
    int id = camera.registerFace(arg.c_str());
    if (id >= 0) Serial.printf("Registered \"%s\" as ID %d (saved to flash).\n", camera.faceName(id), id);
    else         Serial.println("Registration failed.");
  } else if (cmd == 'l') {
    Serial.printf("%d registered:\n", camera.registeredCount());
    for (int i = 0; i < camera.registeredCount(); i++)
      Serial.printf("  ID %d: %s\n", i, camera.faceName(i));
  } else if (cmd == 'c') {
    camera.clearRegisteredFace();
    Serial.println("All registered people erased.");
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 14, 13);
  Serial.setDebugOutput(false);
  delay(500);

  if (!camera.begin()) {
    Serial.println("Camera setup failed. Halting.");
    while (true) delay(1000);
  }
  camera.startStreamServer();   // optional: live view at http://192.168.4.1/
  Serial.println("Ready. Commands: 'r Name', 'l', 'c'");
}

void loop() {
  handleSerialCommands();

  // One pass: all faces, with recognition when someone is registered.
  std::vector<FaceBox> faces = camera.getFaceCoordinates(camera.hasRegisteredFace());

  for (size_t i = 0; i < faces.size(); i++) {
    const FaceBox& f = faces[i];
    if (f.known) {
      // >>> Your robot logic here, e.g. greet / follow this person <<<
      Serial.printf("%s (ID %d) at x=%d y=%d w=%d h=%d\n",
                    camera.faceName(f.id), f.id, f.x, f.y, f.w, f.h);
      Serial2.printf("KNOWN,%s,%d\n",
               camera.faceName(f.id), f.id);
      
    } else {
      Serial.printf("Unknown face at x=%d y=%d w=%d h=%d\n", f.x, f.y, f.w, f.h);
      Serial2.println("UNKNOWN");
    }
  }

  // Other API calls (they reuse the analysis above thanks to the cache):
  //   camera.isKnownPersonPresent();      // anyone registered?
  //   camera.isKnownPersonPresent(2);     // specifically person #2?
  //   FaceBox b; camera.getKnownPersonCoordinates(b);

  delay(100);  // yield to Wi-Fi / watchdog
}
