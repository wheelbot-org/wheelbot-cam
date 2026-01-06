#include "Streamer.h"
#include "HttpStreamTransport.h"
#include "TaskSender.h"
#include "../ConfigManager/ConfigManager.h"
#include <algorithm>
#include "esp_log.h"

static const char *TAG = "Streamer";

Streamer::Streamer(const char* stream_url, const char* frame_size_str, const char* jpeg_quality_str)
    : _cameraModule(nullptr),
      _transport(nullptr),
      _taskSender(nullptr),
      _eventsHandler(nullptr),
      _state(State::IDLE),
      _lastReconnectAttempt(0),
      _currentReconnectInterval(5000),
      _lastMetricsUpdate(0),
      _lastFrameTime(0),
      _frameDelayMs(0),
      _currentFPS(0),
      _totalBytesSent(0),
      _totalFramesSent(0)
{
    size_t url_len = strlen(stream_url);
    if (url_len >= sizeof(_stream_url)) {
        ESP_LOGW(TAG, "Stream URL truncated (len=%zu, max=%zu): %s",
                url_len, sizeof(_stream_url) - 1, stream_url);
    }
    snprintf(_stream_url, sizeof(_stream_url), "%s", stream_url);

    if (strncmp(_stream_url, "http://", 7) != 0 && strncmp(_stream_url, "https://", 8) != 0) {
        ESP_LOGE(TAG, "Invalid URL format (must start with http:// or https://): %s", _stream_url);
    }

    snprintf(_frame_size_str, sizeof(_frame_size_str), "%s", frame_size_str);
    snprintf(_jpeg_quality_str, sizeof(_jpeg_quality_str), "%s", jpeg_quality_str);

    ESP_LOGI(TAG, "Streamer initialized - URL: %s, Size: %s, Quality: %s",
             _stream_url, _frame_size_str, _jpeg_quality_str);

    if (_config.maxFPS > 0) {
        _frameDelayMs = 1000 / _config.maxFPS;
    } else {
        _frameDelayMs = 0;
    }

    _cameraModule = new CameraModule(_frame_size_str, _jpeg_quality_str);
    _initializeTransport();
}

Streamer::~Streamer() {
    _cleanupTransport();

    if (_cameraModule) {
        delete _cameraModule;
        _cameraModule = nullptr;
    }
    }

void Streamer::_initializeTransport() {
    _cleanupTransport();

    _transport = new HttpStreamTransport(_config);
    _taskSender = new TaskSender(_transport, _config);
    _taskSender->setEventsHandler(this);
    _taskSender->start();
}

void Streamer::_cleanupTransport() {
    if (_taskSender) {
        _taskSender->stop();
        vTaskDelay(pdMS_TO_TICKS(500));
        delete _taskSender;
        _taskSender = nullptr;
    }

    if (_transport) {
        _transport->disconnect();
        delete _transport;
        _transport = nullptr;
    }
}

void Streamer::setup() {
    pinMode(LED_PIN, OUTPUT);
    _cameraModule->setup();
    _state = State::IDLE;
    _attemptReconnect();
}

void Streamer::_attemptReconnect() {
    if (_state == State::CONNECTING) {
        return;
    }

    ESP_LOGI(TAG, "Streamer attempting to connect to %s...", _stream_url);
    _state = State::CONNECTING;
    _updateLED();

    if (_transport->connect(_stream_url)) {
        _state = State::STREAMING;
        _currentReconnectInterval = _config.reconnectInterval;
        _reconnectFailureCount = 0;
        ESP_LOGI(TAG, "Streamer connected successfully");
        _notifyConnected();
        _updateLED();
    } else {
        _state = State::ERROR;
        _handleStreamError(_transport->getLastError());
    }

    _lastReconnectAttempt = millis();
}

void Streamer::loop() {
    _updateLED();
    
    uint32_t now = millis();
    if (_frameDelayMs > 0 && (now - _lastFrameTime < _frameDelayMs)) {
        vTaskDelay(pdMS_TO_TICKS(1));
        return;
    }

    if (_state ==     State::IDLE || _state == State::ERROR) {
        if (now - _lastReconnectAttempt >= (long)_currentReconnectInterval) {
            _attemptReconnect();
            _currentReconnectInterval = std::min(_config.maxReconnectInterval,
                                            (uint32_t)(_currentReconnectInterval * _config.reconnectMultiplier));
        }
        return;
    }

    if (!_transport->isConnected()) {
        _state =     State::IDLE;
        _updateLED();
        _notifyDisconnected();
        return;
    }

    camera_fb_t* fb = _cameraModule->get_frame();
    if (fb) {
        char headerBuf[256];
        size_t headerLen = 0;

        if (!_transport) {
            ESP_LOGW(TAG, "Transport not available");
            _cameraModule->return_frame(fb);
            return;
        }

        esp_http_client_handle_t client = _transport->getHttpClient();
        if (!client) {
            ESP_LOGW(TAG, "HTTP client not available");
            _cameraModule->return_frame(fb);
            return;
        }

        HttpStreamTransport* httpTransport = static_cast<HttpStreamTransport*>(_transport);
        httpTransport->formatMultipartHeader(fb, headerBuf, sizeof(headerBuf), &headerLen);

        if (_taskSender->sendFrame(fb, headerBuf, headerLen)) {
            _totalBytesSent += fb->len;
            _totalFramesSent++;
            _currentFPS++;
            _notifyFrameSent(fb->len);
            _lastFrameTime = millis();
        } else {
            ESP_LOGW(TAG, "Queue full, dropping frame");
            _cameraModule->return_frame(fb);
        }
    } else {
        ESP_LOGE(TAG, "Failed to get frame for streaming.");
    }

    _updateMetrics();
}

void Streamer::_updateMetrics() {
    if (_lastMetricsUpdate == 0) {
        _lastMetricsUpdate = millis();
        return;
    }

    long now = millis();
    long elapsed = now - _lastMetricsUpdate;

    if (elapsed < 900) {
        return;
    }

    if (elapsed >= (long)_config.metricsUpdateInterval) {
        uint32_t fps = _currentFPS;
        ESP_LOGI(TAG, "FPS: %u, Bytes: %llu", fps, _totalBytesSent);
        _notifyMetricsUpdate();
        _currentFPS = 0;
        _lastMetricsUpdate = now;
    }
}

void Streamer::_handleStreamError(const char* error) {
    ESP_LOGE(TAG, "STREAM: %s", error);
    _state = State::ERROR;
    _transport->disconnect();
    _notifyError(error);
    
    _reconnectFailureCount++;
    uint32_t remaining = _config.maxSendFailures - _reconnectFailureCount;
    
    ESP_LOGW(TAG, "Reconnect failure %u/%u (%u attempts remaining)",
             _reconnectFailureCount, _config.maxSendFailures, remaining);
    
    _updateLED();
    
    if (_reconnectFailureCount >= _config.maxSendFailures) {
        ESP_LOGW(TAG, "Maximum reconnect failures reached (%u). Setting force captive portal flag and restarting...",
                 _reconnectFailureCount);

        _reconnectFailureCount = 0;

        // Получить доступ к глобальному экземпляру ConfigManager
        extern ConfigManager configManager;
        configManager.set_force_captive_portal(true);

        ESP_LOGI(TAG, "System will restart into captive portal mode...");
        delay(1000);
        ESP.restart();
    }
}

void Streamer::_handleSendError(const char* error) {
    ESP_LOGE(TAG, "STREAM: Send error - %s", error);
    _state = State::ERROR;
    _transport->disconnect();
    _notifyError(error);

    _updateLED();
}

void Streamer::onSendError(const char* message) {
    _handleSendError(message);
}

void Streamer::_updateLED() {
    uint32_t now = millis();

    switch (_state) {
        case State::STREAMING:
            digitalWrite(LED_PIN, LOW);
            break;
            
        case State::CONNECTING:
            if (now - _lastLedUpdate >= LED_BLINK_CONNECTING) {
                _ledState = !_ledState;
                digitalWrite(LED_PIN, _ledState ? HIGH : LOW);
                _lastLedUpdate = now;
            }
            break;

        case State::IDLE:
            if (now - _lastLedUpdate >= LED_BLINK_IDLE) {
                _ledState = !_ledState;
                digitalWrite(LED_PIN, _ledState ? HIGH : LOW);
                _lastLedUpdate = now;
            }
            break;
            
        case State::ERROR:
            if (_isInCaptivePortal) {
                if (_blinkPhase == 0) {
                    if (now - _lastLedUpdate >= LED_BLINK_CAPTIVE) {
                        _ledState = !_ledState;
                        digitalWrite(LED_PIN, _ledState ? HIGH : LOW);
                        _lastLedUpdate = now;

                        if (!_ledState) {
                            _blinkCount++;
                            if (_blinkCount >= 5) {
                                _blinkCount = 0;
                                _blinkPhase = 1;
                                _lastLedUpdate = now;
                            }
                        }
                    }
                } else {
                    if (now - _lastLedUpdate >= 2000) {
                        _blinkPhase = 0;
                        _lastLedUpdate = now;
                    }
                }
            } else {
                if (now - _lastLedUpdate >= LED_BLINK_ERROR) {
                    _ledState = !_ledState;
                    digitalWrite(LED_PIN, _ledState ? HIGH : LOW);
                    _lastLedUpdate = now;
                }
            }
            break;
    }
}

void Streamer::_notifyConnected() {
    if (_eventsHandler) {
        _eventsHandler->onConnected();
    }
}

void Streamer::_notifyDisconnected() {
    if (_eventsHandler) {
        _eventsHandler->onDisconnected();
    }
}

void Streamer::_notifyError(const char* message) {
    if (_eventsHandler) {
        _eventsHandler->onError(message);
    }
}

void Streamer::_notifyFrameSent(size_t size) {
    if (_eventsHandler) {
        _eventsHandler->onFrameSent(size);
    }
}

void Streamer::_notifyMetricsUpdate() {
    if (_eventsHandler) {
        _eventsHandler->onMetricsUpdate(_currentFPS, _totalBytesSent);
    }
}

esp_http_client_handle_t Streamer::get_stream_client() {
    if (_transport) {
        return _transport->getHttpClient();
    }
    return nullptr;
}

void Streamer::setEventsHandler(StreamerEvents* handler) {
    _eventsHandler = handler;
}

uint32_t Streamer::getCurrentFPS() const {
    return _currentFPS;
}

uint64_t Streamer::getBytesSent() const {
    return _totalBytesSent;
}

uint32_t Streamer::getFramesSent() const {
    return _totalFramesSent;
}

uint32_t Streamer::getQueueCount() const {
    if (_taskSender) {
        return _taskSender->getQueueCount();
    }
    return 0;
}
