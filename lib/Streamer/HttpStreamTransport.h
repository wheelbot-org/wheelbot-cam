#ifndef HTTP_STREAM_TRANSPORT_H
#define HTTP_STREAM_TRANSPORT_H

#include "Arduino.h"
#include "StreamTransport.h"
#include "HttpClient.h"
#include "StreamConfig.h"
#include "esp_camera.h"

class HttpStreamTransport : public StreamTransport {
public:
    HttpStreamTransport(const StreamConfig& config);
    ~HttpStreamTransport();

    bool connect(const char* url) override;
    void disconnect() override;
    bool isConnected() const override;
    bool send(const uint8_t* data, size_t len) override;
    uint64_t getBytesSent() const override;
    const char* getLastError() const override;

    bool sendFrame(camera_fb_t* fb);
    bool sendChunked(const uint8_t* data, size_t len, size_t chunkSize);
    esp_http_client_handle_t getHttpClient() const override {
        return _httpClient ? _httpClient->getHandle() : nullptr;
    }
    void formatMultipartHeader(camera_fb_t* fb, char* buf, size_t bufSize, size_t* outLen);

private:
    StreamConfig _config;
    HttpClient* _httpClient;
    char _lastError[256];
};

#endif
