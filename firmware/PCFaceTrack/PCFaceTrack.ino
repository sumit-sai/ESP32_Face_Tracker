#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"
#include "img_converters.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "tracking.h"
#include "eyes.h"

void camera_init();
WebServer control(80);
httpd_handle_t frameServer = nullptr;
uint32_t bootId, nextFrameId = 0, lastAcceptedId = 0;
struct FrameRecord {uint32_t id, capturedMs;};
FrameRecord records[32] = {};
portMUX_TYPE frameMux = portMUX_INITIALIZER_UNLOCKED;

bool unsignedArg(const char *name, uint32_t &value) {
  if (!control.hasArg(name)) return false;
  String text = control.arg(name);
  if (!text.length() || text.length()>10) return false;
  for(size_t i=0;i<text.length();++i) if(text[i]<'0'||text[i]>'9') return false;
  unsigned long long parsed = strtoull(text.c_str(), nullptr, 10);
  if(parsed>UINT32_MAX) return false;
  value=(uint32_t)parsed; return true;
}
bool offsetArg(const char *name, float &value) {
  if(!control.hasArg(name)) return false;
  String text=control.arg(name);
  if(!text.length() || text.length()>24) return false;
  char *end=nullptr;
  value=strtof(text.c_str(),&end);
  return end!=text.c_str() && *end=='\0' && isfinite(value) && value>=-1 && value<=1;
}

esp_err_t serveFrame(httpd_req_t *request) {
  camera_fb_t *fb=esp_camera_fb_get();
  if(!fb || fb->format!=PIXFORMAT_RGB565) {
    if(fb) esp_camera_fb_return(fb);
    submitObservation(false,false,0,0,millis());
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,"RGB565 capture failed");
  }
  uint32_t captured=(uint32_t)(((int64_t)fb->timestamp.tv_sec*1000000LL+fb->timestamp.tv_usec)/1000LL);
  uint8_t *jpeg=nullptr;
  size_t jpegSize=0;
  const bool encoded=frame2jpg(fb,75,&jpeg,&jpegSize);
  esp_camera_fb_return(fb); // conversion owns a separate output buffer
  if(!encoded || !jpeg || !jpegSize) {
    free(jpeg);
    submitObservation(false,false,0,0,millis());
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,"Software JPEG conversion failed");
  }
  portENTER_CRITICAL(&frameMux);
  uint32_t id=++nextFrameId;
  if(id==0) id=++nextFrameId;
  records[id%32]={id,captured};
  portEXIT_CRITICAL(&frameMux);
  char frame[16],boot[16];
  snprintf(frame,sizeof(frame),"%lu",(unsigned long)id);
  snprintf(boot,sizeof(boot),"%lu",(unsigned long)bootId);
  httpd_resp_set_type(request,"image/jpeg");
  httpd_resp_set_hdr(request,"Cache-Control","no-store");
  httpd_resp_set_hdr(request,"X-Frame-Id",frame);
  httpd_resp_set_hdr(request,"X-Boot-Id",boot);
  esp_err_t result=httpd_resp_send(request,(const char *)jpeg,jpegSize);
  free(jpeg); // also release on client disconnect/send failure
  return result;
}

void setup() {
  Serial.begin(115200);
  if(!psramFound()){Serial.println("Enable OPI PSRAM");while(true)delay(1000);}
  bootId=esp_random();
  camera_init();
  setupEyes();
  setupTracking(); // 90/90 startup; paused until the PC explicitly starts control
  WiFi.mode(WIFI_AP);
  if(!WiFi.softAP("RobotArm-Face","FaceCentre32")) {
    Serial.println("AP failed");while(true)delay(1000);
  }
  control.on("/",HTTP_GET,[](){control.send(200,"text/html",
    "<h1>PC Face Tracker</h1><p>Camera and servo bridge ready. Start the Python tracker on your PC.</p>"
    "<p>Pan: GPIO14, 15-165 degrees. Tilt: GPIO21, 65-165 degrees.</p><a href='/status'>Status</a>");});
  control.on("/status",HTTP_GET,[](){
    MotionSnapshot m=getMotion();
    control.sendHeader("Cache-Control","no-store");
    control.send(200,"application/json",String("{\"firmware\":\"PCFaceTrack\",\"protocol\":1,\"boot_id\":")+bootId+
      ",\"pan\":"+String(m.pan,1)+",\"tilt\":"+String(m.tilt,1)+",\"state\":\""+Motion::name(m.state)+"\"}");
  });
  control.on("/tracking",HTTP_POST,[](){
    if(control.arg("enabled")!="0" && control.arg("enabled")!="1") {control.send(400,"text/plain","enabled=0 or 1 required");return;}
    setTrackingEnabled(control.arg("enabled")=="1");
    control.send(200,"text/plain","OK");
  });
  control.on("/search",HTTP_POST,[](){
    if(control.arg("enabled")!="0" && control.arg("enabled")!="1") {control.send(400,"text/plain","enabled=0 or 1 required");return;}
    setSearchEnabled(control.arg("enabled")=="1");
    control.send(200,"text/plain","OK");
  });
  control.on("/observation",HTTP_POST,[](){
    uint32_t boot,id; float dx=0,dy=0;
    String found=control.arg("found");
    if(!unsignedArg("boot_id",boot) || !unsignedArg("frame_id",id) || (found!="0"&&found!="1") ||
       (found=="1" && (!offsetArg("dx",dx) || !offsetArg("dy",dy)))) {
      control.send(400,"text/plain","Invalid observation");return;
    }
    if(boot!=bootId || (int32_t)(id-lastAcceptedId)<=0) {control.send(409,"text/plain","Wrong boot or old frame");return;}
    portENTER_CRITICAL(&frameMux);
    FrameRecord frame=records[id%32];
    portEXIT_CRITICAL(&frameMux);
    if(frame.id!=id || millis()-frame.capturedMs>Motion::FRESH_MS) {control.send(409,"text/plain","Expired frame");return;}
    lastAcceptedId=id;
    // Normalize any camera resolution back to the original 320x240 controller units.
    submitObservation(true,found=="1",dx*159.5f,dy*119.5f,frame.capturedMs);
    observeEyesFace(found=="1",frame.capturedMs);
    control.send(200,"text/plain","OK");
  });
  control.begin();
  httpd_config_t config=HTTPD_DEFAULT_CONFIG();
  config.server_port=81; config.ctrl_port=32769; config.max_uri_handlers=2;
  config.stack_size=6144; config.recv_wait_timeout=2; config.send_wait_timeout=2;
  config.lru_purge_enable=true;
  ESP_ERROR_CHECK(httpd_start(&frameServer,&config));
  httpd_uri_t uri={}; uri.uri="/frame"; uri.method=HTTP_GET; uri.handler=serveFrame;
  ESP_ERROR_CHECK(httpd_register_uri_handler(frameServer,&uri));
  Serial.println("PCFaceTrack ready: RobotArm-Face / FaceCentre32");
  Serial.println("Control http://192.168.4.1 ; frames http://192.168.4.1:81/frame");
}
void loop(){
  control.handleClient();
  MotionSnapshot motion = getMotion();
  updateEyes(motion.pan, motion.tilt);
  delay(2);
}
