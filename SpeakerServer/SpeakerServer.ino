#include <M5StickCPlus.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>

#include "Config.h"

#define LIGHTGRAY 0xC618
#define FONT_SIZE 10

/* TCP server at port 80 will respond to HTTP requests */
WiFiServer server(80);

void ensureWifi() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(LIGHTGRAY);
  M5.Lcd.drawString("Ensuring WiFi...", 0, 30);
  if (WiFi.waitForConnectResult() == WL_DISCONNECTED) {
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(RED);
    M5.Lcd.drawString("Failed! rebooting...", 0, 70);
    delay(500);
    esp_restart();
  }
}

#define SPEAKER_PIN 26
#define SPEAKER_FREQ 50
#define SPEAKER_CHANNEL 0
#define SPEAKER_RESOLUTION 10
int gToneData[] = {1661, 1865, 2093, 0, 1661, 1865, 2093, 0};

#define RESET_TIMER_INTERVAL 1800000 // 30min
unsigned long gLastLoopAt = 0;
long gDisableTimer = 0;
long gResetTimer = RESET_TIMER_INTERVAL;

///////////////////////////
void setup() {
  M5.begin();
  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(BLACK);
  WiFi.begin(SSID, PASSWORD);
  ensureWifi();
  M5.Lcd.fillScreen(BLACK);

  Serial.begin(115200);
  Serial.println();
  Serial.print("Configuring access point...");

  IPAddress myIP = WiFi.localIP();
  Serial.print("IP address: ");
  Serial.println(myIP);
  //
  //サーバーセットアップ
  //
  /* Set up mDNS */
  if (!MDNS.begin("esp32")) {
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");
  /* Start Web Server server */
  server.begin();
  Serial.println("Web server started");

  /* Add HTTP service to MDNS-SD */
  MDNS.addService("http", "tcp", 80);

  M5.Lcd.drawString("DoorBellExtensionServer", FONT_SIZE, FONT_SIZE);

  ledcSetup(SPEAKER_CHANNEL, SPEAKER_FREQ, SPEAKER_RESOLUTION);
  ledcAttachPin(SPEAKER_PIN, SPEAKER_CHANNEL);
  ledcWrite(SPEAKER_CHANNEL, 0);

  gLastLoopAt = millis();
}

void playMusic() {
  uint32_t length = sizeof(gToneData) / sizeof(gToneData[0]);
  uint16_t delayInterval = 250;
  for(int i = 0; i < length; i++) {
    ledcWriteTone(SPEAKER_CHANNEL, gToneData[i]);
    delay(delayInterval );
  } 
  ledcWriteTone(SPEAKER_CHANNEL, 0);
}

void loop() {
  M5.update();
  showStates();

  unsigned long currentTime = millis();
  int deltaMs = currentTime - gLastLoopAt;
  gLastLoopAt = currentTime;

  // 一定期間アクセスが無いときはWiFi周りのエラーとしてとりあえずリセットする
  gResetTimer -= deltaMs;
  if (gResetTimer < 0) {
    esp_restart();
  }

  // ボタンを押すと30秒間無効化する処理
  long lastDisableTimer = gDisableTimer;
  gDisableTimer -= deltaMs;
  if (gDisableTimer < 0) {
    gDisableTimer = 0;
  }
  if (M5.BtnA.wasReleased()) {
    gDisableTimer = 30000;
  }
  if (lastDisableTimer != gDisableTimer) {
    M5.Lcd.fillRect(FONT_SIZE, FONT_SIZE * 3, FONT_SIZE * 15, FONT_SIZE * 4, BLACK);
    if (gDisableTimer > 0) {
      char buf[256];
      sprintf(buf, "Disabled: %4d", gDisableTimer);
      M5.Lcd.drawString(buf, FONT_SIZE, FONT_SIZE * 3);
    }
  }

  // TCPの接続があればHTTPサーバーとしての動きをする
  WiFiClient client = server.available();
  if (client && client.connected()) {
    if (client.available()) {
      String req = client.readStringUntil('\r');
      int addr_start = req.indexOf(' ');
      int addr_end = req.indexOf(' ', addr_start + 1);
      if (addr_start == -1 || addr_end == -1) {
        Serial.print("Invalid request: ");
        Serial.println(req);
        return;
      }
      req = req.substring(addr_start + 1, addr_end);
      Serial.print("Request: ");
      Serial.println(req);
      //client.flush();

      String s = "";
        IPAddress ip = client.remoteIP();   // クライアント側のIPアドレス
      if (req == "/") {
        String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Hello from ESP32 at ";
        s += ipStr;
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");
      } else if (req == "/ring") {
        String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Ring from ESP32 at ";
        s += ipStr;
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");
        if (gDisableTimer == 0) {
          playMusic();
        }
      } else if (req == "/ping") {
        String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Pong from ESP32 at ";
        s += ipStr;
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");
      } else {
        s = "HTTP/1.1 404 Not Found\r\n\r\n";
        Serial.println("Sending 404");
      }
      client.print(s);
      client.flush();
      client.stop();
      gResetTimer = RESET_TIMER_INTERVAL;
    }
  }
  delay(10);
}

void showStates() {
  char buf[256];
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress myIP = WiFi.localIP();
    sprintf(buf, "IP = %s", myIP.toString().c_str());
    M5.Lcd.drawString(buf, FONT_SIZE, FONT_SIZE * 2);
  } else {
    char* message = "UNKNOWN";
    switch (WiFi.status()) {
      case WL_IDLE_STATUS:
        message = "IDLE_STATUS";
        break;
      case WL_NO_SSID_AVAIL:
        message = "NO_SSID_AVAIL";
        break;
      case WL_SCAN_COMPLETED:
        message = "SCAN_COMPLETED";
        break;
      case WL_CONNECTED:
        message = "CONNECTED";
        break;
      case WL_CONNECT_FAILED:
        message = "CONNECT_FAILED";
        break;
      case WL_CONNECTION_LOST:
        message = "CONNECTION_LOST";
        break;
      case WL_DISCONNECTED:
        message = "DISCONNECTED";
        break;
    }
    M5.Lcd.drawString(message, 0, FONT_SIZE * 14);
  }
}
