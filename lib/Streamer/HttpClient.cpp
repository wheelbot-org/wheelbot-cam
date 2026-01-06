#include "HttpClient.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "HttpClient";

HttpClient::HttpClient(const StreamConfig& config)
    : _config(config),
      _client(nullptr),
      _mutex(nullptr),
      _isConnected(false),
      _bytesSent(0)
{
    memset(_lastError, 0, sizeof(_lastError));
    snprintf(_contentType, sizeof(_contentType), "%s; boundary=%s",
             _config.contentType, _config.boundary);

    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
    }
}

HttpClient::~HttpClient() {
    stopMultipartStream();

    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

bool HttpClient::startMultipartStream(const char* url, uint64_t maxDataSize) {
    size_t adaptiveBufferSize = _config.bufferSize;
    size_t adaptiveTxBufferSize = _config.txBufferSize;

    if (_mutex) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
    }

    if (psramFound()) {
        size_t psramSize = ESP.getPsramSize();
        if (psramSize >= 4 * 1024 * 1024) {
            adaptiveBufferSize = 65536;
            adaptiveTxBufferSize = 65536;
        } else if (psramSize >= 2 * 1024 * 1024) {
            adaptiveBufferSize = 49152;
            adaptiveTxBufferSize = 49152;
        }
    }

    esp_http_client_config_t config = {0};
    config.url = url;
    config.buffer_size = (int)adaptiveBufferSize;
    config.buffer_size_tx = (int)adaptiveTxBufferSize;
    config.timeout_ms = 5000;
    config.method = HTTP_METHOD_POST;
    config.disable_auto_redirect = false;
    config.max_redirection_count = 5;

    _client = esp_http_client_init(&config);
    if (!_client) {
        snprintf(_lastError, sizeof(_lastError), "Failed to init HTTP client");

        if (_mutex) {
            xSemaphoreGive(_mutex);
        }
        return false;
    }

    esp_http_client_set_header(_client, "Content-Type", _contentType);
    esp_http_client_set_header(_client, "X-Framerate", _config.frameRate);

    ESP_LOGI(TAG, "HTTP: Connecting to %s with %llu bytes buffer", url, maxDataSize);
    ESP_LOGI(TAG, "HTTP: Content-Type: %s", _contentType);
    esp_err_t err = esp_http_client_open(_client, (int)maxDataSize);

    if (err != ESP_OK) {
        snprintf(_lastError, sizeof(_lastError), "Failed to open connection: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "HTTP: Could not connect to server: %s", _lastError);
        esp_http_client_cleanup(_client);
        _client = nullptr;

        if (_mutex) {
            xSemaphoreGive(_mutex);
        }
        return false;
    }

    _isConnected = true;
    _bytesSent = 0;
    ESP_LOGI(TAG, "HTTP: Connection established. Ready to send chunks");

    if (_mutex) {
        xSemaphoreGive(_mutex);
    }
    return true;
}

void HttpClient::stopMultipartStream() {
    if (_mutex) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
    }

    if (_client) {
        esp_err_t err = esp_http_client_close(_client);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "HTTP: Connection closed.");
        } else {
            ESP_LOGE(TAG, "HTTP: Could not close connection: %s", esp_err_to_name(err));
        }

        err = esp_http_client_cleanup(_client);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "HTTP: Allocated resources released.");
        } else {
            ESP_LOGE(TAG, "HTTP: Could not release allocated resources: %s", esp_err_to_name(err));
        }

        _client = nullptr;
    }
    _isConnected = false;

    if (_mutex) {
        xSemaphoreGive(_mutex);
    }
}

bool HttpClient::sendMultipartChunk(const uint8_t* header, size_t headerLen,
                                     const uint8_t* data, size_t dataLen) {
    if (_mutex) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
    }

    if (!_client || !_isConnected) {
        snprintf(_lastError, sizeof(_lastError), "Client not connected");

        if (_mutex) {
            xSemaphoreGive(_mutex);
        }
        return false;
    }

    long start_time = millis();

    int header_result = esp_http_client_write(_client, (const char*)header, headerLen);
    if (header_result != (int)headerLen) {
        _isConnected = false;
        esp_http_client_close(_client);
        snprintf(_lastError, sizeof(_lastError), "Header write incomplete: %d/%u bytes", header_result, headerLen);
        ESP_LOGE(TAG, "HTTP: %s", _lastError);

        if (_mutex) {
            xSemaphoreGive(_mutex);
        }
        return false;
    }

    int data_result = esp_http_client_write(_client, (const char*)data, dataLen);
    if (data_result != (int)dataLen) {
        _isConnected = false;
        esp_http_client_close(_client);
        snprintf(_lastError, sizeof(_lastError), "Data write incomplete: %d/%u bytes", data_result, dataLen);
        ESP_LOGE(TAG, "HTTP: %s", _lastError);

        if (_mutex) {
            xSemaphoreGive(_mutex);
        }
        return false;
    }

    long duration = millis() - start_time;
    if (duration > (long)_config.slowChunkThreshold) {
        ESP_LOGW(TAG, "HTTP: Slow chunk send: %lums for %u bytes", duration, dataLen);
    }

    _bytesSent += data_result;

    if (_mutex) {
        xSemaphoreGive(_mutex);
    }
    return true;
}

bool HttpClient::isConnected() const {
    if (_mutex) {
        xSemaphoreTake(const_cast<SemaphoreHandle_t>(_mutex), portMAX_DELAY);
    }

    bool connected = _isConnected;

    if (_mutex) {
        xSemaphoreGive(const_cast<SemaphoreHandle_t>(_mutex));
    }

    return connected;
}

uint64_t HttpClient::getBytesSent() const {
    return _bytesSent;
}

const char* HttpClient::getLastError() const {
    return _lastError;
}
