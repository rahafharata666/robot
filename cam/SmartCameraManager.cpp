/*
 * SmartCameraManager.cpp
 * See the header for build requirements (arduino-esp32 core with native face library, PSRAM on).
 */
#include "SmartCameraManager.h"
#include <SPIFFS.h>

// Flash record: one small file per person (/face_0.bin ... /face_6.bin).
//   [FaceFileHeader][float data n*w*h*c]
// Ids are always contiguous (0..count-1) because we only ever append or clear all.
static const uint32_t FACE_FILE_MAGIC = 0x53434D31;  // "SCM1"
struct FaceFileHeader {
  uint32_t magic;
  int32_t  n, w, h, c;          // dimensions of the library's feature matrix
  char     name[SCM_NAME_LEN];
};

// ===========================================================================
// Construction / destruction
// ===========================================================================
SmartCameraManager::SmartCameraManager(const SmartCameraConfig& cfg) : cfg_(cfg) {
  memset(&idList_, 0, sizeof(idList_));
  memset(&mtmnConfig_, 0, sizeof(mtmnConfig_));
  memset(names_, 0, sizeof(names_));
}

SmartCameraManager::~SmartCameraManager() {
  if (httpd_)       httpd_stop(httpd_);
  if (streamHttpd_) httpd_stop(streamHttpd_);
  freeList();
  if (aligned_) {
    dl_matrix3du_free(aligned_);
    aligned_ = nullptr;
  }
  if (ready_) esp_camera_deinit();
}

// ===========================================================================
// begin(): camera + buffers + stored faces + Access Point
// ===========================================================================
bool SmartCameraManager::begin() {
  // The MTMN detector and the RGB888 frame matrix live in PSRAM. Without it
  // there is simply not enough RAM, so fail early with a clear message.
  if (!psramFound()) {
    Serial.println("[SCM] ERROR: PSRAM not found. Enable it in Tools > PSRAM.");
    return false;
  }
  cacheMutex_ = xSemaphoreCreateMutex();   // created here, not in the constructor
  if (!cacheMutex_) return false;
  if (!initCamera()) return false;

  // Detector tuning: start from the library defaults, which are what the
  // stock CameraWebServer example uses.
  mtmnConfig_ = mtmn_init_config();

  // Empty recognition database (SCM_MAX_PEOPLE slots).
  resetList();

  // Reusable buffer for the aligned 56x56 face crop (allocated once so we
  // don't fragment PSRAM by allocating on every frame).
  aligned_ = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3);
  if (!aligned_) {
    Serial.println("[SCM] ERROR: could not allocate alignment buffer.");
    return false;
  }

  // Bring back the people saved in flash (if any).
  if (cfg_.persistFaces) {
    storageOk_ = initStorage();
    if (storageOk_) {
      loadFaces();
      Serial.printf("[SCM] %d registered person(s) loaded from flash.\n", count_);
    } else {
      Serial.println("[SCM] WARNING: no SPIFFS -> faces will NOT survive a reboot. "
                     "Use partition scheme \"Huge APP (3MB No OTA)\".");
    }
  }

  if (!startAccessPoint()) return false;

  ready_ = true;
  return true;
}

bool SmartCameraManager::initCamera() {
  camera_config_t config;
  memset(&config, 0, sizeof(config));   // newer camera libs read fields we do not set
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = SCM_Y2_GPIO_NUM;
  config.pin_d1       = SCM_Y3_GPIO_NUM;
  config.pin_d2       = SCM_Y4_GPIO_NUM;
  config.pin_d3       = SCM_Y5_GPIO_NUM;
  config.pin_d4       = SCM_Y6_GPIO_NUM;
  config.pin_d5       = SCM_Y7_GPIO_NUM;
  config.pin_d6       = SCM_Y8_GPIO_NUM;
  config.pin_d7       = SCM_Y9_GPIO_NUM;
  config.pin_xclk     = SCM_XCLK_GPIO_NUM;
  config.pin_pclk     = SCM_PCLK_GPIO_NUM;
  config.pin_vsync    = SCM_VSYNC_GPIO_NUM;
  config.pin_href     = SCM_HREF_GPIO_NUM;
  // "SSCB" is the spelling every core with the native face library understands
  // (1.0.x and 2.0.x). If your core only knows "SCCB", rename these two lines.
  config.pin_sscb_sda = SCM_SIOD_GPIO_NUM;
  config.pin_sscb_scl = SCM_SIOC_GPIO_NUM;
  config.pin_pwdn     = SCM_PWDN_GPIO_NUM;
  config.pin_reset    = SCM_RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;

  // JPEG out of the sensor is small and fast; we decode to RGB888 ourselves
  // (fmt2rgb888) exactly as the stock example does for face detection.
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = cfg_.frameSize;
  config.jpeg_quality = cfg_.jpegQuality;

  // Two frame buffers: the driver keeps capturing while we process one.
  // They are placed in PSRAM automatically when PSRAM is present.
  config.fb_count     = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[SCM] ERROR: camera init failed (0x%x)\n", err);
    return false;
  }
  return true;
}

bool SmartCameraManager::startAccessPoint() {
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(cfg_.apSsid, cfg_.apPassword, cfg_.apChannel,
                        /*hidden=*/0, cfg_.apMaxClients);
  if (!ok) {
    Serial.println("[SCM] ERROR: failed to start Access Point.");
    return false;
  }
  Serial.printf("[SCM] AP \"%s\" up, IP: %s\n", cfg_.apSsid,
                WiFi.softAPIP().toString().c_str());
  return true;
}

// ===========================================================================
// Frame pipeline
// ===========================================================================
bool SmartCameraManager::detectFrame(dl_matrix3du_t** img, box_array_t** boxes) {
  *img = nullptr;
  *boxes = nullptr;

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return false;

  // Remember the geometry, then hand the frame buffer back ASAP so the
  // driver can keep capturing while we crunch numbers.
  const int w = fb->width;
  const int h = fb->height;

  dl_matrix3du_t* m = dl_matrix3du_alloc(1, w, h, 3);   // PSRAM-backed
  if (!m) {
    esp_camera_fb_return(fb);
    return false;
  }

  bool ok = fmt2rgb888(fb->buf, fb->len, fb->format, m->item);
  esp_camera_fb_return(fb);
  if (!ok) {
    dl_matrix3du_free(m);
    return false;
  }

  *img   = m;
  *boxes = face_detect(m, &mtmnConfig_);   // NULL when no face found
  return true;
}

void SmartCameraManager::freeDetection(dl_matrix3du_t* img, box_array_t* boxes) {
  // Same cleanup sequence as the stock example.
  if (boxes) {
    dl_lib_free(boxes->score);
    dl_lib_free(boxes->box);
    dl_lib_free(boxes->landmark);
    dl_lib_free(boxes);
  }
  if (img) dl_matrix3du_free(img);
}

FaceBox SmartCameraManager::toFaceBox(const box_array_t* boxes, int i) {
  // box_p = {x1, y1, x2, y2}
  FaceBox b;
  int x1 = (int)boxes->box[i].box_p[0];
  int y1 = (int)boxes->box[i].box_p[1];
  int x2 = (int)boxes->box[i].box_p[2];
  int y2 = (int)boxes->box[i].box_p[3];
  b.x = x1 < 0 ? 0 : x1;
  b.y = y1 < 0 ? 0 : y1;
  b.w = x2 - b.x + 1;
  b.h = y2 - b.y + 1;
  return b;
}

// align_face() always works on the FIRST face of a box_array_t. To align face
// #index we hand it a one-element "view" into the same arrays (no copying).
bool SmartCameraManager::alignFace(box_array_t* boxes, int index, dl_matrix3du_t* img) {
  box_array_t single;
  single.score    = boxes->score + index;
  single.box      = boxes->box + index;
  single.landmark = boxes->landmark + index;
  single.len      = 1;
  return align_face(&single, img, aligned_) == ESP_OK;
}

// ===========================================================================
// analyze(): detection (+ optional recognition), with a short cache
// ===========================================================================
bool SmartCameraManager::analyze(bool recognize, std::vector<FaceBox>& out) {
  if (!ready_) return false;

  // Cache hit: same data is good enough if it is recent, and it already
  // contains recognition results when we need them.
  xSemaphoreTake(cacheMutex_, portMAX_DELAY);
  const bool hit = cacheValid_ && (millis() - cacheStampMs_) < cfg_.resultCacheMs &&
                   (cacheRecognized_ || !recognize);
  if (hit) out = cache_;
  xSemaphoreGive(cacheMutex_);
  if (hit) return true;

  dl_matrix3du_t* img = nullptr;
  box_array_t* boxes = nullptr;
  if (!detectFrame(&img, &boxes)) return false;

  out.clear();
  if (boxes) {
    const bool doRec = recognize && count_ > 0;
    for (int i = 0; i < boxes->len; i++) {
      FaceBox b = toFaceBox(boxes, i);
      if (doRec && alignFace(boxes, i, img)) {
        int id = recognize_face(&idList_, aligned_);   // >=0 match, <0 unknown
        if (id >= 0 && id < count_) {
          b.id = id;
          b.known = true;
        }
      }
      out.push_back(b);
    }
  }
  freeDetection(img, boxes);

  xSemaphoreTake(cacheMutex_, portMAX_DELAY);
  cache_            = out;
  cacheRecognized_  = recognize;
  cacheStampMs_     = millis();
  cacheValid_       = true;
  xSemaphoreGive(cacheMutex_);
  return true;
}

// ===========================================================================
// Recognition database: memory + flash
// ===========================================================================
void SmartCameraManager::freeList() {
  if (idList_.id_list) {
    for (int i = 0; i < SCM_MAX_PEOPLE; i++) {
      if (idList_.id_list[i]) dl_matrix3d_free(idList_.id_list[i]);
    }
    dl_lib_free(idList_.id_list);
  }
  memset(&idList_, 0, sizeof(idList_));
}

// Rebuilding the list from scratch (instead of poking its indices) keeps the
// library's internal bookkeeping consistent whatever happened before.
void SmartCameraManager::resetList() {
  freeList();
  face_id_init(&idList_, SCM_MAX_PEOPLE, ENROLL_CONFIRM_TIMES);
  count_ = 0;
  memset(names_, 0, sizeof(names_));
  cacheValid_ = false;
}

void SmartCameraManager::rebuildFromFlash() {
  resetList();
  if (storageOk_) loadFaces();
}

bool SmartCameraManager::initStorage() {
  return SPIFFS.begin(/*formatOnFail=*/true);
}

void SmartCameraManager::facePath(int slot, char* out, size_t n) {
  snprintf(out, n, "/face_%d.bin", slot);
}

// Names come from Serial/user input: keep UTF-8 (Arabic etc.) but strip
// characters that could break the JSON/HTML viewer.
void SmartCameraManager::sanitizeName(char* dst, const char* src) {
  size_t j = 0;
  for (size_t i = 0; src && src[i] && j < SCM_NAME_LEN - 1; i++) {
    unsigned char ch = (unsigned char)src[i];
    if (ch < 0x20 || ch == '"' || ch == '\\' || ch == '<' || ch == '>' || ch == '&')
      ch = '_';
    dst[j++] = (char)ch;
  }
  dst[j] = '\0';
}

bool SmartCameraManager::saveFace(int slot) {
  if (!storageOk_ || slot < 0 || slot >= SCM_MAX_PEOPLE) return false;
  dl_matrix3d_t* m = idList_.id_list[slot];
  if (!m) return false;

  FaceFileHeader hdr;
  memset(&hdr, 0, sizeof(hdr));
  hdr.magic = FACE_FILE_MAGIC;
  hdr.n = m->n; hdr.w = m->w; hdr.h = m->h; hdr.c = m->c;
  strncpy(hdr.name, names_[slot], SCM_NAME_LEN - 1);

  char path[24];
  facePath(slot, path, sizeof(path));
  File f = SPIFFS.open(path, FILE_WRITE);
  if (!f) return false;
  const size_t bytes = (size_t)m->n * m->w * m->h * m->c * sizeof(float);
  bool ok = f.write((const uint8_t*)&hdr, sizeof(hdr)) == sizeof(hdr) &&
            f.write((const uint8_t*)m->item, bytes) == bytes;
  f.close();
  if (!ok) SPIFFS.remove(path);
  return ok;
}

bool SmartCameraManager::loadFaces() {
  int loaded = 0;
  for (int slot = 0; slot < SCM_MAX_PEOPLE; slot++) {
    char path[24];
    facePath(slot, path, sizeof(path));
    if (!SPIFFS.exists(path)) break;              // ids are contiguous

    File f = SPIFFS.open(path, FILE_READ);
    if (!f) break;
    FaceFileHeader hdr;
    bool good = f.read((uint8_t*)&hdr, sizeof(hdr)) == sizeof(hdr) &&
                hdr.magic == FACE_FILE_MAGIC &&
                hdr.n > 0 && hdr.w > 0 && hdr.h > 0 && hdr.c > 0;
    const size_t total = good ? (size_t)hdr.n * hdr.w * hdr.h * hdr.c : 0;
    good = good && total <= 4096;                 // sanity: expected size is 512 floats

    dl_matrix3d_t* m = nullptr;
    if (good) {
      m = dl_matrix3d_alloc(hdr.n, hdr.w, hdr.h, hdr.c);
      good = m && f.read((uint8_t*)m->item, total * sizeof(float)) == total * sizeof(float);
    }
    f.close();
    if (!good) {
      if (m) dl_matrix3d_free(m);
      Serial.printf("[SCM] WARNING: %s is corrupt, stopping load.\n", path);
      break;
    }

    idList_.id_list[slot] = m;
    hdr.name[SCM_NAME_LEN - 1] = '\0';
    strncpy(names_[slot], hdr.name, SCM_NAME_LEN - 1);
    loaded++;
  }

  // Tell the library how many ids are enrolled (ring buffer: head=0, next
  // free slot = tail).
  count_        = loaded;
  idList_.count = loaded;
  idList_.head  = 0;
  idList_.tail  = loaded % SCM_MAX_PEOPLE;
  cacheValid_   = false;
  return loaded > 0;
}

void SmartCameraManager::eraseStorage() {
  if (!storageOk_) return;
  for (int slot = 0; slot < SCM_MAX_PEOPLE; slot++) {
    char path[24];
    facePath(slot, path, sizeof(path));
    if (SPIFFS.exists(path)) SPIFFS.remove(path);
  }
}

// ===========================================================================
// Public API
// ===========================================================================
std::vector<FaceBox> SmartCameraManager::getFaceCoordinates(bool withRecognition) {
  std::vector<FaceBox> faces;
  analyze(withRecognition, faces);
  return faces;
}

const char* SmartCameraManager::faceName(int id) const {
  return (id >= 0 && id < count_) ? names_[id] : "";
}

int SmartCameraManager::registerFace(const char* name, uint32_t timeoutMs) {
  if (!ready_) return -1;
  if (count_ >= SCM_MAX_PEOPLE) {
    Serial.printf("[SCM] Database full (%d people). Clear it first.\n", SCM_MAX_PEOPLE);
    return -1;
  }
  cacheValid_ = false;

  bool startedSampling = false;
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    dl_matrix3du_t* img = nullptr;
    box_array_t* boxes = nullptr;

    if (!detectFrame(&img, &boxes)) {
      delay(50);
      continue;
    }

    // Require exactly one face so we never enroll the wrong person.
    if (boxes && boxes->len == 1 && alignFace(boxes, 0, img)) {
      // Returns how many samples are still needed; 0 = enrollment finished,
      // negative = error.
      int left = enroll_face(&idList_, aligned_);
      startedSampling = true;
      Serial.printf("[SCM] Enroll: %d sample(s) left\n", left);

      freeDetection(img, boxes);
      if (left == 0) {
        // New person lives in slot count_ (ids are contiguous).
        const int id = count_;
        if (name && name[0]) sanitizeName(names_[id], name);
        else snprintf(names_[id], SCM_NAME_LEN, "Person %d", id + 1);
        count_++;
        if (cfg_.persistFaces && !saveFace(id)) {
          Serial.println("[SCM] WARNING: could not save face to flash.");
        }
        cacheValid_ = false;
        return id;
      }
      if (left < 0) break;
      delay(150);               // small pause -> slightly different samples
      continue;
    }

    freeDetection(img, boxes);
    delay(50);
  }

  Serial.println("[SCM] Enrollment failed/timed out.");
  // Drop any half-finished enrollment: restore the list as it was before.
  if (startedSampling) rebuildFromFlash();
  return -1;
}

bool SmartCameraManager::isKnownPersonPresent(int id) {
  FaceBox tmp;
  return getKnownPersonCoordinates(tmp, id);
}

std::vector<FaceBox> SmartCameraManager::getKnownFaces() {
  std::vector<FaceBox> known;
  if (count_ == 0) return known;
  std::vector<FaceBox> faces;
  if (!analyze(true, faces)) return known;
  for (size_t i = 0; i < faces.size(); i++)
    if (faces[i].known) known.push_back(faces[i]);
  return known;
}

bool SmartCameraManager::getKnownPersonCoordinates(FaceBox& out, int id) {
  std::vector<FaceBox> known = getKnownFaces();

  // Several matches are possible; report the largest (closest) one.
  int best = -1, bestArea = 0;
  for (size_t i = 0; i < known.size(); i++) {
    if (id >= 0 && known[i].id != id) continue;
    int area = known[i].w * known[i].h;
    if (area > bestArea) {
      bestArea = area;
      best = (int)i;
    }
  }
  if (best < 0) return false;
  out = known[best];
  return true;
}

void SmartCameraManager::clearRegisteredFace() {
  resetList();
  eraseStorage();
}

// ===========================================================================
// Web viewer (optional): page + MJPEG stream + JSON with face boxes
// ===========================================================================
std::vector<FaceBox> SmartCameraManager::getLastFaces(uint32_t* ageMs) {
  std::vector<FaceBox> copy;
  xSemaphoreTake(cacheMutex_, portMAX_DELAY);
  if (cacheValid_) copy = cache_;
  if (ageMs) *ageMs = cacheValid_ ? (millis() - cacheStampMs_) : 0xFFFFFFFF;
  xSemaphoreGive(cacheMutex_);
  return copy;
}

// The page is stored in flash (PROGMEM). The video is an <img> pointing at the
// stream on port 81; a transparent <canvas> on top draws the boxes and names
// that the browser polls from /faces. No pixels are modified on the ESP32.
static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-CAM Smart</title>
<style>body{margin:0;background:#111;color:#eee;font-family:sans-serif;text-align:center}
#w{position:relative;display:inline-block;margin-top:10px}
#v{display:block;max-width:100vw}
#o{position:absolute;left:0;top:0;pointer-events:none}</style></head>
<body><div id="w"><img id="v"><canvas id="o"></canvas></div>
<p>green = known person, red = unknown</p>
<script>
const img=document.getElementById('v'),cv=document.getElementById('o'),ctx=cv.getContext('2d');
img.src='http://'+location.hostname+':81/stream';
async function poll(){
  try{
    const d=await (await fetch('/faces')).json();
    cv.width=img.clientWidth;cv.height=img.clientHeight;
    ctx.clearRect(0,0,cv.width,cv.height);
    if(img.naturalWidth&&d.age<1500){
      const sx=img.clientWidth/img.naturalWidth,sy=img.clientHeight/img.naturalHeight;
      ctx.lineWidth=2;ctx.font='16px sans-serif';
      for(const f of d.faces){
        ctx.strokeStyle=ctx.fillStyle=f.known?'#0f0':'#f33';
        ctx.strokeRect(f.x*sx,f.y*sy,f.w*sx,f.h*sy);
        if(f.known)ctx.fillText(f.name,f.x*sx+4,Math.max(14,f.y*sy-4));
      }
    }
  }catch(e){}
  setTimeout(poll,300);
}
poll();
</script></body></html>)rawliteral";

esp_err_t SmartCameraManager::handleIndex(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

esp_err_t SmartCameraManager::handleFaces(httpd_req_t* req) {
  SmartCameraManager* self = static_cast<SmartCameraManager*>(req->user_ctx);
  uint32_t age = 0;
  std::vector<FaceBox> faces = self->getLastFaces(&age);

  String json = "{\"age\":" + String(age) + ",\"faces\":[";
  for (size_t i = 0; i < faces.size(); i++) {
    if (i) json += ",";
    json += "{\"x\":" + String(faces[i].x) + ",\"y\":" + String(faces[i].y) +
            ",\"w\":" + String(faces[i].w) + ",\"h\":" + String(faces[i].h) +
            ",\"id\":" + String(faces[i].id) +
            ",\"known\":" + (faces[i].known ? "true" : "false") +
            ",\"name\":\"" + String(self->faceName(faces[i].id)) + "\"}";
  }
  json += "]}";

  httpd_resp_set_type(req, "application/json; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, json.c_str(), json.length());
}

// MJPEG: the sensor already outputs JPEG, so frames are forwarded as-is
// (no decoding, almost no CPU). Runs only while a client is connected.
esp_err_t SmartCameraManager::handleStream(httpd_req_t* req) {
  static const char* BOUNDARY = "\r\n--frame\r\n";
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  esp_err_t res = ESP_OK;
  char part[64];
  while (res == ESP_OK) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { res = ESP_FAIL; break; }

    size_t hl = snprintf(part, sizeof(part),
                         "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                         (unsigned)fb->len);
    res = httpd_resp_send_chunk(req, BOUNDARY, strlen(BOUNDARY));
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, part, hl);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);

    vTaskDelay(pdMS_TO_TICKS(60));   // ~15 fps cap, leaves CPU for face detection
  }
  return res;
}

bool SmartCameraManager::startStreamServer() {
  if (!ready_) return false;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;

  // Server #1 (port 80): page + /faces
  config.server_port = 80;
  httpd_uri_t indexUri = { "/",      HTTP_GET, handleIndex, this };
  httpd_uri_t facesUri = { "/faces", HTTP_GET, handleFaces, this };
  if (httpd_start(&httpd_, &config) != ESP_OK) {
    Serial.println("[SCM] ERROR: web server failed to start.");
    return false;
  }
  httpd_register_uri_handler(httpd_, &indexUri);
  httpd_register_uri_handler(httpd_, &facesUri);

  // Server #2 (port 81): the endless stream gets its own server so it
  // never blocks the page or /faces requests (same trick as the stock example).
  config.server_port += 1;
  config.ctrl_port   += 1;
  httpd_uri_t streamUri = { "/stream", HTTP_GET, handleStream, this };
  if (httpd_start(&streamHttpd_, &config) != ESP_OK) {
    Serial.println("[SCM] ERROR: stream server failed to start.");
    return false;
  }
  httpd_register_uri_handler(streamHttpd_, &streamUri);

  Serial.printf("[SCM] Viewer: http://%s/   Stream: http://%s:81/stream\n",
                WiFi.softAPIP().toString().c_str(),
                WiFi.softAPIP().toString().c_str());
  return true;
}
