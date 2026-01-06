#ifndef STREAM_TRANSPORT_H
#define STREAM_TRANSPORT_H

#include <cstddef>
#include <cstdint>
#include <esp_http_client.h>

class StreamTransport {
public:
    virtual ~StreamTransport() = default;
    
    virtual bool connect(const char* url) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    
    virtual bool send(const uint8_t* data, size_t len) = 0;

    virtual uint64_t getBytesSent() const = 0;

    virtual const char* getLastError() const = 0;

    virtual esp_http_client_handle_t getHttpClient() const = 0;
};

#endif
