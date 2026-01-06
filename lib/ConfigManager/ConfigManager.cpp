#include "ConfigManager.h"
#include "esp_log.h"
#include "WiFi.h"
#include "../WiFiPortal/WiFiPortal.h"

static const char *TAG = "ConfigManager";

ConfigManager* ConfigManager::_instance = nullptr;

const char* wifiReasonToString(uint8_t reason) {
    switch (reason) {
        case 1: return "UNSPECIFIED";
        case 2: return "AUTH_EXPIRE";
        case 3: return "AUTH_LEAVE";
        case 4: return "ASSOC_EXPIRE";
        case 5: return "ASSOC_TOOMANY";
        case 6: return "NOT_AUTHED";
        case 7: return "NOT_ASSOCED";
        case 8: return "ASSOC_LEAVE";
        case 9: return "ASSOC_NOT_AUTHED";
        case 10: return "DISASSOC_PWRCAP_BAD";
        case 11: return "DISASSOC_SUPCHAN_BAD";
        case 12: return "BSS_TRANSITION";
        case 13: return "IE_INVALID";
        case 14: return "MIC_FAILURE";
        case 15: return "4WAY_HANDSHAKE_TIMEOUT";
        case 16: return "GROUP_KEY_UPDATE_TIMEOUT";
        case 17: return "IE_IN_4WAY_DIFFERS";
        case 18: return "GROUP_CIPHER_INVALID";
        case 19: return "PAIRWISE_CIPHER_INVALID";
        case 20: return "AKMP_INVALID";
        case 21: return "UNSUPP_RSN_IE_VERSION";
        case 22: return "INVALID_RSN_IE_CAP";
        case 23: return "802_1X_AUTH_FAILED";
        case 24: return "CIPHER_SUITE_REJECTED";
        case 200: return "BEACON_TIMEOUT";
        case 201: return "NO_AP_FOUND";
        case 202: return "AUTH_FAIL";
        case 203: return "ASSOC_FAIL";
        case 204: return "HANDSHAKE_TIMEOUT";
        case 205: return "CONNECTION_FAIL";
        default: return "UNKNOWN";
    }
}

ConfigManager::ConfigManager() : _wifi_connected(false) {
    _instance = this;
    strcpy(_frame_size, "VGA");
    strcpy(_jpeg_quality, "10");
}

void ConfigManager::loadServerConfig() {
    _preferences.begin("wheelbot-cam", true);
    String server_ip_pref = _preferences.getString("server_ip", "192.168.0.2");
    String server_port_pref = _preferences.getString("server_port", "8080");
    String frame_size_pref = _preferences.getString("frame_size", "VGA");
    String jpeg_quality_pref = _preferences.getString("jpeg_quality", "10");
    server_ip_pref.toCharArray(_server_ip, sizeof(_server_ip));
    server_port_pref.toCharArray(_server_port, sizeof(_server_port));
    frame_size_pref.toCharArray(_frame_size, sizeof(_frame_size));
    jpeg_quality_pref.toCharArray(_jpeg_quality, sizeof(_jpeg_quality));
    _preferences.end();

    ESP_LOGI(TAG, "Server configuration loaded.");
}

void ConfigManager::connectToWiFi() {
    if (!_preferences.begin("wheelbot-cam", true)) {
        ESP_LOGE(TAG, "Failed to open NVS namespace 'wheelbot-cam' for reading");
        _wifi_connected = false;
        return;
    }

    String ssid = _preferences.getString("ssid", "");
    String password = _preferences.getString("password", "");
    _preferences.end();

    if (ssid != "") {
        ESP_LOGI(TAG, "Loaded credentials - SSID: '%s', Password length: %u",
                 ssid.c_str(), password.length());

        if (ssid.length() == 0 || password.length() == 0) {
            ESP_LOGE(TAG, "Invalid credentials - SSID or password empty");
        } else if (ssid.length() > 32) {
            ESP_LOGE(TAG, "Invalid SSID length: %u (max 32)", ssid.length());
        } else if (password.length() > 64) {
            ESP_LOGE(TAG, "Invalid password length: %u (max 64)", password.length());
        }

        ESP_LOGI(TAG, "Resetting WiFi before connection...");
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_STA);
        delay(100);

        WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
            if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
                ESP_LOGE(TAG, "WiFi Disconnected - Reason: %d (%s)",
                         info.wifi_sta_disconnected.reason,
                         wifiReasonToString(info.wifi_sta_disconnected.reason));
            }
        });

        ESP_LOGI(TAG, "Found saved credentials. Trying to connect to '%s'...", ssid.c_str());

        WiFi.setSleep(false);
        ESP_LOGI(TAG, "WiFi power management disabled for maximum throughput");

        WiFi.setHostname("wheelbot-cam");
        WiFi.begin(ssid.c_str(), password.c_str());

        int timeout = 0;
        while (WiFi.status() != WL_CONNECTED && timeout < 40) {
            delay(500);
            timeout++;
            if (timeout % 2 == 0) {
                ESP_LOGI(TAG, ".");
            }
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        _wifi_connected = true;
        ESP_LOGI(TAG, "WiFi Connected.");
    } else {
        _wifi_connected = false;
        ESP_LOGE(TAG, "WiFi connection failed.");
    }
}

void ConfigManager::setup() {
    loadServerConfig();
    connectToWiFi();
}

void ConfigManager::loop() {
    // Nothing to do here for now
}

const char* ConfigManager::get_server_ip() {
    return _server_ip;
}

const char* ConfigManager::get_server_port() {
    return _server_port;
}

const char* ConfigManager::get_frame_size() {
    return _frame_size;
}

const char* ConfigManager::get_jpeg_quality() {
    return _jpeg_quality;
}

bool ConfigManager::get_wifi_connected() {
    return _wifi_connected;
}

void ConfigManager::clearWiFiCredentials() {
    if (!_preferences.begin("wheelbot-cam", false)) {
        ESP_LOGE(TAG, "Failed to open NVS namespace 'wheelbot-cam' for writing");
        return;
    }

    _preferences.remove("ssid");
    _preferences.remove("password");
    _preferences.end();
    ESP_LOGI(TAG, "WiFi credentials cleared");
}

bool ConfigManager::get_force_captive_portal() {
    if (!_preferences.begin("wheelbot-cam", true)) {
        ESP_LOGE(TAG, "Failed to open NVS namespace 'wheelbot-cam' for reading");
        return false;
    }

    bool force = _preferences.getBool("force_captive", false);
    _preferences.end();
    return force;
}

void ConfigManager::set_force_captive_portal(bool force) {
    if (!_preferences.begin("wheelbot-cam", false)) {
        ESP_LOGE(TAG, "Failed to open NVS namespace 'wheelbot-cam' for writing");
        return;
    }

    _preferences.putBool("force_captive", force);
    _preferences.end();
    ESP_LOGI(TAG, "Force captive portal flag set to: %s", force ? "true" : "false");
}

void ConfigManager::clear_force_captive_portal() {
    if (!_preferences.begin("wheelbot-cam", false)) {
        ESP_LOGE(TAG, "Failed to open NVS namespace 'wheelbot-cam' for writing");
        return;
    }

    _preferences.remove("force_captive");
    _preferences.end();
    ESP_LOGI(TAG, "Force captive portal flag cleared");
}
