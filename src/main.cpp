#include <Arduino.h>
#include <esp_camera.h>
#include <WiFi.h>
#include <Preferences.h>
#include "esp_log.h"

#include <ConfigManager.h>
#include <Streamer.h>
#include <ESPmDNS.h>
#include <WiFiPortal.h>

static const char *TAG = "MAIN";

ConfigManager configManager;
Streamer* streamer;

#define ERROR_LED_GPIO 33

void handleCriticalError(const char* message) {
  ESP_LOGE(TAG, "%s", message);
  pinMode(ERROR_LED_GPIO, OUTPUT);
  for (int i = 0; i < 50; i++) {
    digitalWrite(ERROR_LED_GPIO, HIGH);
    delay(100);
    digitalWrite(ERROR_LED_GPIO, LOW);
    delay(100);
  }
  ESP_LOGE(TAG, "Force captive portal flag set. Restarting ESP32 in 5 seconds...");
  configManager.set_force_captive_portal(true);
  delay(5000);
  ESP.restart();
}

void setup() {
  delay(5000); // Stabilization delay at startup

  Serial.begin(115200);
  Serial.flush();

  Serial.println("Wheelbot Cam Firmware Starting...");
  Serial.flush();
  

  pinMode(ERROR_LED_GPIO, OUTPUT);
  digitalWrite(ERROR_LED_GPIO, HIGH);
  
  // Check PSRAM
  if (psramFound()) {
    Serial.printf("PSRAM detected: %d MB total, %d MB free\n", 
                 ESP.getPsramSize() / (1024 * 1024), 
                 ESP.getFreePsram() / (1024 * 1024));
  } else {
    Serial.println("PSRAM NOT detected!");
  }
  Serial.flush();
  
  delay(1000); // Give time for serial monitor to connect

  // Check force captive portal flag FIRST
  if (configManager.get_force_captive_portal()) {
    ESP_LOGW(TAG, "Force captive portal flag set. Starting WiFi Portal...");

    WiFiPortal portal;
    if (!portal.run()) {
      handleCriticalError("WiFi Portal failed!");
    }

    // Сбросить флаг после успешной настройки
    configManager.clear_force_captive_portal();
    ESP_LOGI(TAG, "WiFi credentials updated. Restarting...");

    delay(1000);
    ESP.restart();
  }

  // Normal boot - connect to WiFi
  configManager.setup();

  Serial.println("WiFi setup complete.");
  Serial.flush();

  if (!configManager.get_wifi_connected()) {
    handleCriticalError("WiFi connection failed!");
  }

  Serial.println("WiFi connected.");
  Serial.flush();

  char url_stream[128];
  snprintf(url_stream, sizeof(url_stream), "http://%s:%s/input", configManager.get_server_ip(), configManager.get_server_port());

  streamer = new Streamer(url_stream, configManager.get_frame_size(), configManager.get_jpeg_quality());
  streamer->setup();

  // Log initial connection state (non-blocking)
  if (streamer->get_stream_client() == NULL) {
    ESP_LOGW(TAG, "Streamer not connected initially. Will attempt to reconnect...");
  } else {
    digitalWrite(ERROR_LED_GPIO, LOW);
    ESP_LOGI(TAG, "Streamer connected successfully");
  }

  // Initialize mDNS
  if (!MDNS.begin("wheelbot-cam")) {
    ESP_LOGE(TAG, "Error setting up MDNS responder!");
  } else {
    ESP_LOGI(TAG, "mDNS responder started");
  }

  ESP_LOGI(TAG, "Camera Ready! IP -> %s", WiFi.localIP().toString().c_str());

  ESP_LOGI(TAG, "Streaming to: %s", url_stream);
}

void loop() {
  streamer->loop();
}