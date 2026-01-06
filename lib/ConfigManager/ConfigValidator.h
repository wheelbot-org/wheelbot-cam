#ifndef CONFIG_VALIDATOR_H
#define CONFIG_VALIDATOR_H

#include <Arduino.h>

class ConfigValidator {
public:
    static bool validateSSID(const String& ssid) {
        return (ssid.length() > 0 && ssid.length() <= 32);
    }

    static bool validatePassword(const String& password) {
        return (password.length() > 0 && password.length() <= 64);
    }

    static bool validateIP(const String& ip) {
        int a, b, c, d;
        return (sscanf(ip.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) == 4);
    }

    static bool validatePort(const String& port) {
        int p = port.toInt();
        return (p > 0 && p < 65536);
    }

    static bool validateJpegQuality(const String& quality) {
        int q = quality.toInt();
        return (q >= 1 && q <= 31);
    }
};

#endif
