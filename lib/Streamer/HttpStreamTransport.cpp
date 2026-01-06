#include "HttpStreamTransport.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "HttpStreamTransport";

HttpStreamTransport::HttpStreamTransport(const StreamConfig& config)
    : _config(config)
{
    _httpClient = new HttpClient(config);
    memset(_lastError, 0, sizeof(_lastError));
}

HttpStreamTransport::~HttpStreamTransport() {
    disconnect();
    delete _httpClient;
}

bool HttpStreamTransport::connect(const char* url) {
    if (_httpClient->startMultipartStream(url, _config.maxDataSize)) {
        return true;
    }
    snprintf(_lastError, sizeof(_lastError), "%s", _httpClient->getLastError());
    return false;
}

void HttpStreamTransport::disconnect() {
    _httpClient->stopMultipartStream();
}

bool HttpStreamTransport::isConnected() const {
    return _httpClient->isConnected();
}

bool HttpStreamTransport::send(const uint8_t* data, size_t len) {
    if (!_httpClient->isConnected()) {
        snprintf(_lastError, sizeof(_lastError), "Client not connected");
        return false;
    }

    esp_http_client_handle_t client = _httpClient->getHandle();
    if (!client) {
        snprintf(_lastError, sizeof(_lastError), "HTTP client handle is null");
        return false;
    }

    int result = esp_http_client_write(client, (const char*)data, len);
    if (result != (int)len) {
        _httpClient->stopMultipartStream();
        snprintf(_lastError, sizeof(_lastError), "Write incomplete: %d/%u bytes", result, len);
        ESP_LOGE(TAG, "HTTP: %s", _lastError);
        return false;
    }

    return true;
}

uint64_t HttpStreamTransport::getBytesSent() const {
    return _httpClient->getBytesSent();
}

const char* HttpStreamTransport::getLastError() const {
    return _lastError;
}

void HttpStreamTransport::formatMultipartHeader(camera_fb_t* fb, char* buf, size_t bufSize, size_t* outLen) {
    const char* PART_HEADER = "\r\n--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";
    *outLen = snprintf(buf, bufSize, PART_HEADER, _config.boundary, fb->len, fb->timestamp.tv_sec, fb->timestamp.tv_usec);
}

bool HttpStreamTransport::sendFrame(camera_fb_t* fb) {
    if (!fb) {
        snprintf(_lastError, sizeof(_lastError), "Frame buffer is null");
        return false;
    }

    if (fb->format != PIXFORMAT_JPEG) {
        snprintf(_lastError, sizeof(_lastError), "Non-JPEG frame format");
        return false;
    }

    char headerBuf[256];
    size_t headerLen = 0;
    formatMultipartHeader(fb, headerBuf, sizeof(headerBuf), &headerLen);

    if (_httpClient->sendMultipartChunk((uint8_t*)headerBuf, headerLen, fb->buf, fb->len)) {
        uint64_t bytesSent = _httpClient->getBytesSent();
        if (bytesSent % 1000000 < fb->len) {
            ESP_LOGI(TAG, "Total sent: %llu MB", bytesSent / 1000000);
        }

        if (_config.maxDataSize < (fb->len * 2 + bytesSent)) {
            ESP_LOGI(TAG, "Stream restart required (max data limit)...");
            return false;
        }
        return true;
    }

    snprintf(_lastError, sizeof(_lastError), "%s", _httpClient->getLastError());
    return false;
}

bool HttpStreamTransport::sendChunked(const uint8_t* data, size_t len, size_t chunkSize) {
    char headerBuf[256];
    size_t headerLen = snprintf(headerBuf, sizeof(headerBuf), "\r\n--%s\r\nContent-Type: application/octet-stream\r\nContent-Length: %u\r\n\r\n", _config.boundary, len);

    if (!_httpClient->sendMultipartChunk((uint8_t*)headerBuf, headerLen, data, len)) {
        snprintf(_lastError, sizeof(_lastError), "Chunked send failed");
        return false;
    }
    return true;
}
