/*
 * SmartCameraManager.h
 *
 * Object-oriented wrapper around the face detection / recognition code that
 * ships with the official CameraWebServer example.
 *
 * REQUIREMENTS
 *   - Board package: "esp32" by Espressif that still ships the native face
 *     headers (fd_forward.h / fr_forward.h): 2.0.0 is confirmed working by the
 *     user, 1.0.6 is the other candidate. Much newer cores (3.x) do not have
 *     them and fail with "fd_forward.h: No such file".
 *   - Board: "AI Thinker ESP32-CAM", PSRAM enabled ("Enabled" in Tools menu).
 *   - Partition scheme: "Huge APP (3MB No OTA)". It has a SPIFFS area, which
 *     this class uses to store the registered faces permanently.
 */
#ifndef SMART_CAMERA_MANAGER_H
#define SMART_CAMERA_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <vector>

#include "esp_camera.h"
#include "img_converters.h"
#include "esp_http_server.h"   // built-in HTTP server (same one the stock example uses)
#include "freertos/semphr.h"
#include "fd_forward.h"        // native MTMN face detector (box_array_t, mtmn_config_t)
#include "fr_forward.h"        // native face recognition (face_id_list, enroll/recognize)

// Number of good samples averaged per enrolled person (same as stock example).
#ifndef ENROLL_CONFIRM_TIMES
#define ENROLL_CONFIRM_TIMES 5
#endif

// The stock example defines this itself; keep the same value if the library doesn't.
#ifndef FACE_ID_SAVE_NUMBER
#define FACE_ID_SAVE_NUMBER 7
#endif

// Maximum number of people. The native library supports FACE_ID_SAVE_NUMBER (7).
#define SCM_MAX_PEOPLE  FACE_ID_SAVE_NUMBER
#define SCM_NAME_LEN    32     // bytes per name, incl. terminating 0 (UTF-8 ok)

// ---------------------------------------------------------------------------
// Camera pin map: AI-Thinker ESP32-CAM (same as CAMERA_MODEL_AI_THINKER in
// the example's camera_pins.h). Change these for other boards.
// ---------------------------------------------------------------------------
#define SCM_PWDN_GPIO_NUM   32
#define SCM_RESET_GPIO_NUM  -1
#define SCM_XCLK_GPIO_NUM    0
#define SCM_SIOD_GPIO_NUM   26
#define SCM_SIOC_GPIO_NUM   27
#define SCM_Y9_GPIO_NUM     35
#define SCM_Y8_GPIO_NUM     34
#define SCM_Y7_GPIO_NUM     39
#define SCM_Y6_GPIO_NUM     36
#define SCM_Y5_GPIO_NUM     21
#define SCM_Y4_GPIO_NUM     19
#define SCM_Y3_GPIO_NUM     18
#define SCM_Y2_GPIO_NUM      5
#define SCM_VSYNC_GPIO_NUM  25
#define SCM_HREF_GPIO_NUM   23
#define SCM_PCLK_GPIO_NUM   22

/** Bounding box of a detected face, in pixels of the captured frame. */
struct FaceBox {
  int  x = 0;          // left
  int  y = 0;          // top
  int  w = 0;          // width
  int  h = 0;          // height
  int  id = -1;        // person ID (0..6), -1 = unknown / not evaluated
  bool known = false;  // true if matched against a registered person
};

/** User-tunable settings. Defaults work for a QVGA stream on an AI-Thinker. */
struct SmartCameraConfig {
  const char*  apSsid         = "ESP32CAM-Smart";
  const char*  apPassword     = "12345678";   // WPA2 needs >= 8 characters
  uint8_t      apChannel      = 1;
  uint8_t      apMaxClients   = 4;
  framesize_t  frameSize      = FRAMESIZE_QVGA;  // 320x240: good speed/RAM trade-off
  int          jpegQuality    = 12;              // 0..63, lower = better quality
  uint32_t     resultCacheMs  = 250;             // reuse last analysis if newer than this
  bool         persistFaces   = true;            // keep registered faces in flash
};

class SmartCameraManager {
public:
  explicit SmartCameraManager(const SmartCameraConfig& cfg = SmartCameraConfig());
  ~SmartCameraManager();

  // Non-copyable: owns raw buffers from the esp-face library.
  SmartCameraManager(const SmartCameraManager&) = delete;
  SmartCameraManager& operator=(const SmartCameraManager&) = delete;

  /** Init camera (PSRAM checked), allocate buffers, load saved faces, start the AP. */
  bool begin();

  /**
   * Detect all faces in the current frame. With withRecognition = true each
   * box also carries id/known for the registered people (one pass, one frame).
   */
  std::vector<FaceBox> getFaceCoordinates(bool withRecognition = false);

  /**
   * Enroll the face currently in front of the camera as a NEW person (adds to
   * the existing ones, up to SCM_MAX_PEOPLE) and save it to flash.
   * Needs exactly one face in view for ENROLL_CONFIRM_TIMES good frames.
   * @return the new person's ID (0..6), or -1 on failure / database full.
   */
  int registerFace(const char* name = "", uint32_t timeoutMs = 15000);

  /** True if a registered person (or the one with this id) is in the frame. */
  bool isKnownPersonPresent(int id = -1);

  /** Box of the known person (largest one, or the one matching id). */
  bool getKnownPersonCoordinates(FaceBox& out, int id = -1);

  /** Boxes of ALL registered people currently visible. */
  std::vector<FaceBox> getKnownFaces();

  /** Erase every registered person (RAM and flash). */
  void clearRegisteredFace();

  int         registeredCount() const { return count_; }
  bool        hasRegisteredFace() const { return count_ > 0; }
  const char* faceName(int id) const;          // "" if id is invalid
  bool        isReady() const { return ready_; }
  IPAddress   apIP() const    { return WiFi.softAPIP(); }

  /**
   * Start the web server (call after begin()):
   *   http://192.168.4.1/          -> live video + face boxes and names
   *   http://192.168.4.1:81/stream -> raw MJPEG stream (VLC / OpenCV)
   *   http://192.168.4.1/faces     -> JSON with the latest analysis
   * Purely a viewer: detection/recognition runs in your loop() whether or not
   * a browser is connected.
   */
  bool startStreamServer();

  /** Latest cached analysis (thread-safe). ageMs = how old it is. */
  std::vector<FaceBox> getLastFaces(uint32_t* ageMs = nullptr);

private:
  bool initCamera();
  bool startAccessPoint();

  // Grab a frame, convert JPEG->RGB888 and run the MTMN detector.
  // On success the caller owns *img and *boxes (may be NULL if no faces)
  // and must release them with freeDetection().
  bool detectFrame(dl_matrix3du_t** img, box_array_t** boxes);
  void freeDetection(dl_matrix3du_t* img, box_array_t* boxes);

  // Align face #index into aligned_ (56x56). Returns true on success.
  bool alignFace(box_array_t* boxes, int index, dl_matrix3du_t* img);

  // Full analysis (detect + optional recognition) with short-lived cache.
  bool analyze(bool recognize, std::vector<FaceBox>& out);

  static FaceBox toFaceBox(const box_array_t* boxes, int index);

  // --- recognition database + flash persistence ---
  void freeList();          // release all id matrices and the list itself
  void resetList();         // freeList() + fresh empty list, count_ = 0
  void rebuildFromFlash();  // used after a failed enrollment
  bool initStorage();       // mount SPIFFS
  bool loadFaces();         // flash -> idList_
  bool saveFace(int slot);  // idList_[slot] -> flash
  void eraseStorage();
  static void facePath(int slot, char* out, size_t n);
  static void sanitizeName(char* dst, const char* src);

  // HTTP handlers (user_ctx of each URI points back to the manager).
  static esp_err_t handleIndex(httpd_req_t* req);
  static esp_err_t handleStream(httpd_req_t* req);
  static esp_err_t handleFaces(httpd_req_t* req);

  SmartCameraConfig cfg_;
  bool              ready_       = false;
  bool              storageOk_   = false;

  mtmn_config_t     mtmnConfig_;            // detector tuning (from mtmn_init_config())
  face_id_list      idList_;                // native recognition database
  dl_matrix3du_t*   aligned_     = nullptr; // reusable 56x56x3 buffer for alignment
  int               count_       = 0;       // registered people; ids are 0..count_-1
  char              names_[SCM_MAX_PEOPLE][SCM_NAME_LEN];

  // Result cache so back-to-back calls don't re-run the heavy pipeline.
  std::vector<FaceBox> cache_;
  bool                 cacheRecognized_ = false;
  uint32_t             cacheStampMs_    = 0;
  bool                 cacheValid_      = false;
  SemaphoreHandle_t    cacheMutex_      = nullptr;  // cache is read by the HTTP task

  httpd_handle_t       httpd_           = nullptr;  // port 80: page + /faces
  httpd_handle_t       streamHttpd_     = nullptr;  // port 81: MJPEG stream
};

#endif  // SMART_CAMERA_MANAGER_H
