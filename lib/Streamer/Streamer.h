#ifndef STREAMER_H
#define STREAMER_H

#include "Arduino.h"
#include "esp_http_client.h"
#include "esp_camera.h"
#include <CameraModule.h>
#include "StreamTransport.h"
#include "StreamConfig.h"
#include "StreamerEvents.h"
#include "TaskSender.h"

class Streamer : public StreamerEvents {
public:
    enum class State {
        IDLE,
        CONNECTING,
        STREAMING,
        ERROR
    };

    Streamer(const char* stream_url, const char* frame_size_str, const char* jpeg_quality_str);
    ~Streamer();
    
    void setup();
    void loop();
    esp_http_client_handle_t get_stream_client();
    
    void setEventsHandler(StreamerEvents* handler);
    
    uint32_t getCurrentFPS() const;
    uint64_t getBytesSent() const;
    uint32_t getFramesSent() const;
    uint32_t getQueueCount() const;
    void onSendError(const char* message) override;

private:
    StreamConfig _config;
    char _stream_url[256];
    char _frame_size_str[16];
    char _jpeg_quality_str[4];
    
    CameraModule* _cameraModule;
    StreamTransport* _transport;
    StreamerEvents* _eventsHandler;
    TaskSender* _taskSender;
    
    State _state;
    long _lastReconnectAttempt;
    uint32_t _currentReconnectInterval;
    long _lastMetricsUpdate;
    uint32_t _lastFrameTime;
    uint32_t _frameDelayMs;

    uint32_t _currentFPS;
    uint64_t _totalBytesSent;
    uint32_t _totalFramesSent;
    
    uint32_t _reconnectFailureCount = 0;
    bool _isInCaptivePortal = false;

    uint32_t _lastLedUpdate = 0;
    bool _ledState = false;
    uint32_t _blinkCount = 0;
    uint32_t _blinkPhase = 0;

    static const uint32_t LED_PIN = 33;
    static const uint32_t LED_BLINK_CONNECTING = 500;
    static const uint32_t LED_BLINK_IDLE = 1000;
    static const uint32_t LED_BLINK_ERROR = 100;
    static const uint32_t LED_BLINK_CAPTIVE = 200;
    
    void _initializeTransport();
    void _cleanupTransport();
    void _attemptReconnect();
    void _updateMetrics();
    void _handleStreamError(const char* error);
    void _handleSendError(const char* error);
    void _updateLED();
    void _blinkLed(uint32_t interval);
    void _notifyConnected();
    void _notifyDisconnected();
    void _notifyError(const char* message);
    void _notifyFrameSent(size_t size);
    void _notifyMetricsUpdate();
};

#endif
