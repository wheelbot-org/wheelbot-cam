#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Preferences.h>

class ConfigManager {
 public:
    ConfigManager();
    void setup();
    void loadServerConfig();
    void connectToWiFi();
    void loop();
    const char* get_server_ip();
    const char* get_server_port();
    const char* get_frame_size();
    const char* get_jpeg_quality();
    bool get_wifi_connected();
    void clearWiFiCredentials();

    bool get_force_captive_portal();
    void set_force_captive_portal(bool force);
    void clear_force_captive_portal();

 private:
    char _server_ip[16];
    char _server_port[6];
    char _frame_size[10];
    char _jpeg_quality[4];
    Preferences _preferences;
    bool _wifi_connected;

    static ConfigManager* _instance;
};

#endif
