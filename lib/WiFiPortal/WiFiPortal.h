#ifndef WIFI_PORTAL_H
#define WIFI_PORTAL_H

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>


class WiFiPortal {
public:
    WiFiPortal(const char* ap_ssid = "Wheelbot-Cam-Setup");
    ~WiFiPortal();
    bool run(); // The main blocking method

private:
    void handle_root();
    void handle_save();
    void handle_not_found();
    void handle_required_pages();
    void handle_clear_credentials();
    void send_error_page(const String& error_message);

    bool is_captive_portal();
    void setup_ap();

    WebServer _server;
    DNSServer _dns_server;
    Preferences _preferences;

    const char* _ap_ssid;
    bool _portal_running;
    char* _cachedTemplate;
    size_t _cachedTemplateSize;
    bool _psramAllocated = false;
    bool _settingsSaved = false;
    unsigned long _settingsSavedTime = 0;
    static const unsigned long SETTINGS_SAVE_DELAY_MS = 3000;
};

#endif
