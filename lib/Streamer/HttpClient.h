#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "Arduino.h"
#include "esp_http_client.h"
#include "StreamConfig.h"

class HttpClient {
public:
    HttpClient(const StreamConfig& config);
    ~HttpClient();
    
    bool startMultipartStream(const char* url, uint64_t maxDataSize);
    void stopMultipartStream();
    bool sendMultipartChunk(const uint8_t* header, size_t headerLen, 
                           const uint8_t* data, size_t dataLen);
    
    bool isConnected() const;
    uint64_t getBytesSent() const;
    const char* getLastError() const;
    
    esp_http_client_handle_t getHandle() const {
        if (_mutex) {
            xSemaphoreTake(const_cast<SemaphoreHandle_t>(_mutex), portMAX_DELAY);
        }

        esp_http_client_handle_t client = _client;

        if (_mutex) {
            xSemaphoreGive(const_cast<SemaphoreHandle_t>(_mutex));
        }

        return client;
    }
    
 private:
    const StreamConfig& _config;
    esp_http_client_handle_t _client;
    SemaphoreHandle_t _mutex;
    char _partBuf[256];
    char _lastError[256];
    char _contentType[128];
    bool _isConnected;
    uint64_t _bytesSent;
};

#endif
