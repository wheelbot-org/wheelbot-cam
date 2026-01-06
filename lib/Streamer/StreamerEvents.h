#ifndef STREAMER_EVENTS_H
#define STREAMER_EVENTS_H

#include <cstddef>
#include <cstdint>

class StreamerEvents {
public:
    virtual ~StreamerEvents() = default;
    virtual void onConnected() {}
    virtual void onDisconnected() {}
    virtual void onError(const char* message) {}
    virtual void onFrameSent(size_t size) {}
    virtual void onMetricsUpdate(uint32_t fps, uint64_t bytesSent) {}
    virtual void onSendError(const char* message) {}
};

#endif
