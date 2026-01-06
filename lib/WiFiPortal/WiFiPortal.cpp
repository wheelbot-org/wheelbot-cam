#include "WiFiPortal.h"
#include "esp_log.h"
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>

#define ERROR_LED_GPIO 33

static const char *TAG = "WiFiPortal";

#define WIFI_CHANNEL 6
#define MAX_CLIENTS 4

// DNS server
const byte DNS_PORT = 53;

bool isValidIP(String ip) {
    if (ip.length() < 7 || ip.length() > 15) return false;
    int octets = 0;
    int current = 0;
    for (int i = 0; i < ip.length(); i++) {
        if (ip.charAt(i) == '.') {
            octets++;
            if (current > 255) return false;
            current = 0;
        } else if (ip.charAt(i) >= '0' && ip.charAt(i) <= '9') {
            current = current * 10 + (ip.charAt(i) - '0');
        } else {
            return false;
        }
    }
    return octets == 3 && current <= 255;
}

bool isValidPort(String port) {
    if (port.length() == 0 || port.length() > 5) return false;
    for (int i = 0; i < port.length(); i++) {
        if (port.charAt(i) < '0' || port.charAt(i) > '9') return false;
    }
    int portNum = port.toInt();
    return portNum > 0 && portNum <= 65535;
}

WiFiPortal::WiFiPortal(const char* ap_ssid) : _server(80) {
    _ap_ssid = ap_ssid;
    _cachedTemplate = nullptr;
    _cachedTemplateSize = 0;
}

WiFiPortal::~WiFiPortal() {
    if (_cachedTemplate != nullptr && _psramAllocated) {
        heap_caps_free(_cachedTemplate);
        _cachedTemplate = nullptr;
        _cachedTemplateSize = 0;
        _psramAllocated = false;
    }
}

bool WiFiPortal::run() {
    delay(1000);
    ESP_LOGI(TAG, "Starting WiFi Portal...");

    pinMode(ERROR_LED_GPIO, OUTPUT);
    for (int i = 0; i < 10; i++) {
        digitalWrite(ERROR_LED_GPIO, LOW);
        delay(400);
        digitalWrite(ERROR_LED_GPIO, HIGH);
        delay(200);
    }

    _portal_running = true;

    if (!LittleFS.begin()) {
        ESP_LOGE(TAG, "Failed to mount LittleFS. Halting.");
        return false;
    }

    ESP_LOGI(TAG, "LittleFS mounted.");

    setup_ap();

    // Start DNS server
    _dns_server.start(DNS_PORT, "*", WiFi.softAPIP());
    delay(1000);
    ESP_LOGI(TAG, "DNS server started 1");

    // Configure web server routes
    _server.on("/", HTTP_GET, std::bind(&WiFiPortal::handle_root, this));
    _server.on("/save", HTTP_POST, std::bind(&WiFiPortal::handle_save, this));
    _server.on("/clear", HTTP_POST, std::bind(&WiFiPortal::handle_clear_credentials, this));

    // Add handlers for common captive portal detection routes
    _server.on("/fwlink", std::bind(&WiFiPortal::handle_root, this));       // Windows

    // Priority 1: Missing critical routes
    _server.on("/connecttest.txt", std::bind(&WiFiPortal::handle_required_pages, this)); // Windows 11 - Critical!
    _server.on("/wpad.dat", std::bind(&WiFiPortal::handle_not_found, this)); // Windows PAC file - prevent loops
    _server.on("/redirect", std::bind(&WiFiPortal::handle_root, this)); // Microsoft Edge
    _server.on("/canonical.html", std::bind(&WiFiPortal::handle_root, this)); // Firefox
    _server.on("/success.txt", std::bind(&WiFiPortal::handle_root, this)); // Firefox
    _server.on("/favicon.ico", std::bind(&WiFiPortal::handle_not_found, this)); // Explicit favicon handler

    // Priority 2: Fix HTTP responses for captive portal detection
    _server.on("/generate_204", [this]() {
        _server.send(204);
    }); // Android - send 204 No Content

    _server.on("/hotspot-detect.html", [this]() {
        _server.sendHeader("Location", "/", true);
        _server.send(302, "text/plain", "");
    }); // Apple iOS - send 302 redirect to /

    _server.on("/ncsi.txt", [this]() {
        _server.sendHeader("Content-Type", "text/plain");
        _server.send(200);
    }); // Windows NCSI - send 200 with text/plain

    _server.on("/connecttest.txt", [this]() {
        _server.sendHeader("Location", "/", true);
        _server.send(302, "text/plain", "");
    }); // Windows 11 - send 302 redirect to /
    
    _server.serveStatic("/favicon.svg", LittleFS, "/favicon.svg");
    _server.serveStatic("/favicon.png", LittleFS, "/favicon.png");
    _server.serveStatic("/style.css", LittleFS, "/style.css");
    _server.serveStatic("/script.js", LittleFS, "/script.js");
    _server.serveStatic("/success.js", LittleFS, "/success.js");

    _server.on("/scan", HTTP_GET, [&]() {
        int n = WiFi.scanNetworks();
        DynamicJsonDocument doc(4096);
        JsonArray arr = doc.to<JsonArray>();
        for (int i = 0; i < n; i++) {
            arr.add(WiFi.SSID(i));
        }
        String json;
        serializeJson(doc, json);
        _server.send(200, "application/json", json);

        ESP_LOGI(TAG, "Served WiFi scan results (%d networks).", n);
    });

    _server.onNotFound(std::bind(&WiFiPortal::handle_not_found, this));

    _server.begin();
    ESP_LOGI(TAG, "Web server started 2");

    // Main portal loop
    while (_portal_running) {
        _dns_server.processNextRequest();
        _server.handleClient();

        // Check if WiFi connected in station mode
        if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
            ESP_LOGI(TAG, "WiFi Connected! IP: %s", WiFi.localIP().toString().c_str());
            _portal_running = false;
        }

        // Check if settings were saved and timeout reached
        if (_settingsSaved && (millis() - _settingsSavedTime >= SETTINGS_SAVE_DELAY_MS)) {
            ESP_LOGI(TAG, "Settings saved and timeout reached. Stopping portal...");
            _portal_running = false;
        }

        delay(10);
    }

    // Cleanup
    _server.stop();
    _dns_server.stop();
    WiFi.mode(WIFI_STA);
    ESP_LOGI(TAG, "Portal stopped. WiFi connected.");

    return true; // Should return true on success
}

void WiFiPortal::setup_ap() {
    WiFi.mode(WIFI_AP);
    IPAddress apIP(4, 3, 2, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(_ap_ssid, "", WIFI_CHANNEL, 0, MAX_CLIENTS);

    // Android AMPDU RX disable workaround
    esp_wifi_stop();
    esp_wifi_deinit();
    wifi_init_config_t my_config = WIFI_INIT_CONFIG_DEFAULT();
    my_config.ampdu_rx_enable = false;
    esp_wifi_init(&my_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "Access Point '%s' started with IP %s", _ap_ssid, WiFi.softAPIP().toString().c_str());
}

String makeOption(const String& value, const String& current) {
  String s = "<option value=\"" + value + "\"";
  if (value == current) {
    s += " selected";
  }
  s += ">" + value + "</option>";
  return s;
}

void WiFiPortal::handle_not_found() {
    ESP_LOGI(TAG, "Handling not found: %s", _server.uri().c_str());

    if (is_captive_portal()) {
        _server.sendHeader("Location", "/", true);
        _server.send(302, "text/plain", "");
    } else {
        _server.send(404, "text/plain", "Not Found");
    }
}

void WiFiPortal::handle_required_pages() {
    // This function can be used to handle any required pages for captive portal detection
    _server.send(200, "text/html", "<html><body><h1>WheelB⚙t</h1></body></html>");
}

void WiFiPortal::handle_root() {
    ESP_LOGI(TAG, "Handling root request...");

    String portalContent;

    if (_cachedTemplate == nullptr) {
        File portalFile = LittleFS.open("/index.html", "r");
        if (!portalFile) {
            ESP_LOGE(TAG, "Failed to open index.html");
            _server.send(500, "text/plain", "ERROR: Could not load portal page.");
            return;
        }

        String tempContent = portalFile.readString();
        portalFile.close();

        ESP_LOGI(TAG, "Loaded portal page.");

        if (psramFound()) {
            _cachedTemplateSize = tempContent.length() + 1;
            _cachedTemplate = (char*)heap_caps_malloc(_cachedTemplateSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (_cachedTemplate != nullptr) {
                memcpy(_cachedTemplate, tempContent.c_str(), _cachedTemplateSize);
                _psramAllocated = true;
                ESP_LOGI(TAG, "Template cached in PSRAM (%u bytes)", _cachedTemplateSize);
                portalContent = String(_cachedTemplate);
            } else {
                _psramAllocated = false;
                ESP_LOGW(TAG, "Failed to allocate PSRAM, using RAM template");
                portalContent = tempContent;
            }
        } else {
            _psramAllocated = false;
            portalContent = tempContent;
        }
    } else {
        ESP_LOGI(TAG, "Using cached template");
        portalContent = String(_cachedTemplate);
    }

    // Load current values from preferences
    _preferences.begin("wheelbot-cam", true);
    String server_ip = _preferences.getString("server_ip", "192.168.0.2");
    String server_port = _preferences.getString("server_port", "8080");
    String frame_size = _preferences.getString("frame_size", "VGA");
    String jpeg_quality = _preferences.getString("jpeg_quality", "10");
    String password = _preferences.getString("password", "");
    String ssid = _preferences.getString("ssid", "");
    _preferences.end();

    // Build frame size options
    String frame_size_options = "";
    frame_size_options += makeOption("QQVGA", frame_size);
    frame_size_options += makeOption("QVGA", frame_size);
    frame_size_options += makeOption("VGA", frame_size);
    frame_size_options += makeOption("SVGA", frame_size);
    frame_size_options += makeOption("XGA", frame_size);
    frame_size_options += makeOption("SXGA", frame_size);

    // Replace placeholders
    portalContent.replace("{ssid_val}", ssid);
    portalContent.replace("{wifi-password}", password);
    portalContent.replace("{server_ip_val}", server_ip);
    portalContent.replace("{server_port_val}", server_port);
    portalContent.replace("{frame_size_options}", frame_size_options);
    portalContent.replace("{jpeg_quality_val}", jpeg_quality);

    ESP_LOGI(TAG, "Serving portal page.");

    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.sendHeader("Pragma", "no-cache");
    _server.sendHeader("Expires", "-1");

    _server.send(200, "text/html", portalContent);

    ESP_LOGI(TAG, "Root request handled.");
}

void WiFiPortal::handle_save() {
    ESP_LOGI(TAG, "Handling save request...");

    // Extract data from form
    String ssid = _server.arg("ssid");
    String password = _server.arg("password");
    String server_ip = _server.arg("server_ip");
    String server_port = _server.arg("server_port");
    String frame_size = _server.arg("frame_size");
    String jpeg_quality = _server.arg("jpeg_quality");

    // Validate SSID
    if (ssid.length() == 0) {
        ESP_LOGE(TAG, "SSID cannot be empty");
        send_error_page("SSID cannot be empty.");
        return;
    }

    if (ssid.length() > 32) {
        ESP_LOGE(TAG, "SSID too long: %u characters (max 32)", ssid.length());
        send_error_page("SSID too long (max 32 characters).");
        return;
    }

    // Validate password
    if (password.length() == 0) {
        ESP_LOGE(TAG, "Password cannot be empty");
        send_error_page("Password cannot be empty.");
        return;
    }

    if (password.length() > 64) {
        ESP_LOGE(TAG, "Password too long: %u characters (max 64)", password.length());
        send_error_page("Password too long (max 64 characters).");
        return;
    }

    // Validate IP and port
    if (!isValidIP(server_ip)) {
        ESP_LOGE(TAG, "Invalid IP address: %s", server_ip.c_str());
        send_error_page("Invalid IP address format. Please enter a valid IPv4 address (e.g., 192.168.0.2).");
        return;
    }

    if (!isValidPort(server_port)) {
        ESP_LOGE(TAG, "Invalid port: %s", server_port.c_str());
        send_error_page("Invalid port number. Please enter a value between 1 and 65535.");
        return;
    }

    // Validate JPEG quality
    int quality = jpeg_quality.toInt();
    if (quality < 1 || quality > 31) {
        ESP_LOGE(TAG, "Invalid JPEG quality: %d (must be 1-31)", quality);
        send_error_page("Invalid JPEG quality. Please enter a value between 1 and 31.");
        return;
    }

    // Save to preferences
    _preferences.begin("wheelbot-cam", false);
    _preferences.putString("ssid", ssid);
    _preferences.putString("password", password);
    _preferences.putString("server_ip", server_ip);
    _preferences.putString("server_port", server_port);
    _preferences.putString("frame_size", frame_size);
    _preferences.putString("jpeg_quality", jpeg_quality);
    _preferences.end();

    ESP_LOGI(TAG, "Credentials saved - SSID: '%s', Password length: %u",
             ssid.c_str(), password.length());
    ESP_LOGI(TAG, "Server settings - IP: %s:%s, Frame size: %s, Quality: %s",
             server_ip.c_str(), server_port.c_str(), frame_size.c_str(), jpeg_quality.c_str());
    // Set flag that settings were saved
    _settingsSaved = true;
    _settingsSavedTime = millis();

    // Send success page
    String html;
    File successFile = LittleFS.open("/success.html", "r");
    if (successFile) {
        html = successFile.readString();
        
        // Replace {ssid} placeholder
        html.replace("{ssid}", ssid);
        
        successFile.close();
        
        _server.send(200, "text/html", html);
    } else {
        // Fallback if file not found
        String fallbackHtml = "<!DOCTYPE html><html><head><title>Success</title></head>"
            "<body style='color:#aaffaa;background:#000;display:flex;"
            "justify-content:center;align-items:center;height:100vh;"
            "font-family:monospace;font-size:18px;'>"
            "<div style='text-align:center;'>"
            "<div style='font-size:64px;'>✓</div>"
            "<h1>Settings Saved!</h1>"
            "<p>Network: <strong>" + ssid + "</strong></p>"
            "<p>Rebooting in 3 seconds...</p>"
            "</div></body></html>";
        
        _server.send(200, "text/html", fallbackHtml);
    }
}

bool WiFiPortal::is_captive_portal() {
    // A simple check to see if the request is for a specific file or the root
    if (_server.uri().endsWith(".css") || _server.uri().endsWith(".js") || _server.uri().endsWith(".ico")) {
        return false;
    }
    return true;
}

void WiFiPortal::handle_clear_credentials() {
    ESP_LOGI(TAG, "Clearing WiFi credentials...");

    _preferences.begin("wheelbot-cam", false);
    _preferences.remove("ssid");
    _preferences.remove("password");
    _preferences.end();

    WiFi.disconnect();
    delay(1000);

    ESP_LOGI(TAG, "WiFi credentials cleared. Restarting...");

    _server.send(200, "text/html", "<html><body><h1>Credentials Cleared</h1><p>WiFi credentials have been removed. Restarting...</p></body></html>");

    delay(2000);
    ESP.restart();
}

void WiFiPortal::send_error_page(const String& error_message) {
    String html;
    File errorFile = LittleFS.open("/error.html", "r");
    if (errorFile) {
        html = errorFile.readString();

        html.replace("{error_message}", error_message);

        errorFile.close();

        _server.send(400, "text/html", html);
    } else {
        String fallbackHtml = "<!DOCTYPE html><html><head><title>Error</title></head>"
            "<body style='color:#ffaaaa;background:#000;display:flex;"
            "justify-content:center;align-items:center;height:100vh;"
            "font-family:monospace;font-size:18px;'>"
            "<div style='text-align:center;'>"
            "<div style='font-size:64px;'>✗</div>"
            "<h1>Error</h1>"
            "<p>" + error_message + "</p>"
            "<button onclick='history.back()'>Back</button>"
            "</div></body></html>";

        _server.send(400, "text/html", fallbackHtml);
    }
}
