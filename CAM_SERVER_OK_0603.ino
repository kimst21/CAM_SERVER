#include "esp_camera.h"       // ESP32-CAM 카메라 드라이버 라이브러리
#include <WiFi.h>             // Wi-Fi 연결을 위한 라이브러리
#include <FS.h>               // 파일 시스템 인터페이스
#include <SD_MMC.h>           // SD 카드(MMC) 인터페이스
#include <WebServer.h>        // ESP32 내장 웹서버 기능 제공

const char* ssid = "WeVO_2.4G";         // 연결할 Wi-Fi SSID
const char* password = "Toolbox";     // 연결할 Wi-Fi 비밀번호

WebServer server(80);         // 웹서버 인스턴스 생성 (포트 80)

// --- ESP32-CAM 핀맵 설정 (AI Thinker 보드 기준) ---
#define PWDN_GPIO_NUM     32  // 카메라 전원 제어 핀
#define RESET_GPIO_NUM    -1  // 리셋 핀 (-1은 사용 안 함)
#define XCLK_GPIO_NUM      0  // 외부 클럭 출력 핀
#define SIOD_GPIO_NUM     26  // SCCB 데이터 핀 (I2C SDA 역할)
#define SIOC_GPIO_NUM     27  // SCCB 클럭 핀 (I2C SCL 역할)
#define Y9_GPIO_NUM       35  // 카메라 영상 데이터 핀
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25  // 수직 동기화 신호 핀
#define HREF_GPIO_NUM     23  // 수평 동기화 신호 핀
#define PCLK_GPIO_NUM     22  // 픽셀 클럭 핀

// --- 카메라 설정 및 초기화 함수 ---
void setup_camera() {
  camera_config_t config;                   // 카메라 설정 구조체

  // LED PWM 채널 설정 (사용 안 해도 무방)
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  // 데이터 핀 연결
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  // 클럭 및 동기화 핀 연결
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  // SCCB (I2C) 설정
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;

  // 전원/리셋 핀 설정
  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  // 클럭 주파수 및 이미지 포맷 설정
  config.xclk_freq_hz = 20000000;            // 20 MHz 클럭
  config.pixel_format = PIXFORMAT_JPEG;      // JPEG 포맷

  // 해상도 및 JPEG 품질 설정
  config.frame_size = FRAMESIZE_XGA;         // 해상도: XGA (1024x768)
  config.jpeg_quality = 10;                  // 품질: 낮은 수치일수록 고화질
  config.fb_count = 2;                        // 프레임 버퍼 수 (2개 사용)

  // 카메라 초기화
  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed");    // 초기화 실패 시 오류 메시지
    return;
  }
}

// --- 파일 목록을 JSON 형식으로 응답하는 핸들러 ---
void handleList() {
  File root = SD_MMC.open("/");               // 루트 디렉토리 열기
  if (!root || !root.isDirectory()) {         // 실패하거나 디렉토리가 아니면
    server.send(500, "application/json", "[]"); // 빈 배열 반환
    return;
  }

  String json = "[";                          // JSON 배열 시작
  File file = root.openNextFile();            // 첫 번째 파일 열기
  while (file) {
    if (!file.isDirectory()) {                // 디렉토리가 아니라면
      json += "{\"name\":\"" + String(file.name()) + "\",\"size\":" + String(file.size()) + "},";
    }
    file = root.openNextFile();               // 다음 파일로 이동
  }
  if (json.endsWith(",")) json.remove(json.length() - 1); // 마지막 쉼표 제거
  json += "]";                                // JSON 닫기
  server.send(200, "application/json", json); // 응답 전송
}

// --- 특정 파일 다운로드 처리 핸들러 ---
void handleDownload() {
  if (!server.hasArg("file")) {               // 파일명 파라미터가 없을 경우
    server.send(400, "text/plain", "Missing file parameter");
    return;
  }

  String fileName = "/" + server.arg("file"); // 요청된 파일명
  File file = SD_MMC.open(fileName);          // SD에서 파일 열기
  if (!file || file.isDirectory()) {          // 파일이 없거나 디렉토리인 경우
    server.send(404, "text/plain", "File not found");
    return;
  }

  // 다운로드용 HTTP 헤더 설정
  server.sendHeader("Content-Type", "application/octet-stream");
  server.sendHeader("Content-Disposition", "attachment; filename=" + fileName);
  server.sendHeader("Content-Length", String(file.size()));
  server.streamFile(file, "application/octet-stream"); // 파일 스트림 전송
  file.close();                            // 파일 닫기
}

// --- 초기화 함수 ---
void setup() {
  Serial.begin(115200);                     // 시리얼 출력 시작
  Serial.println("Booting...");

  setup_camera();                           // 카메라 초기화

  // SD 카드 마운트 (1비트 모드)
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD Card Mount Failed");
    return;
  }

  // Wi-Fi 연결
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");                      // 연결 대기 중 표시
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());          // IP 출력

  // 웹서버 라우팅 설정
  server.on("/list", HTTP_GET, handleList);          // /list 요청 처리
  server.on("/download", HTTP_GET, handleDownload);  // /download 요청 처리

  server.begin();                           // 웹서버 시작
  Serial.println("Web server started");
}

// --- 메인 루프: 클라이언트 처리 반복 ---
void loop() {
  server.handleClient();                    // HTTP 요청 수신 처리
}
