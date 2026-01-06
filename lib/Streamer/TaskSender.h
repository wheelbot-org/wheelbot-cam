#ifndef TASK_SENDER_H
#define TASK_SENDER_H

#include "Arduino.h"
#include "StreamTransport.h"
#include "StreamConfig.h"
#include "esp_camera.h"
#include <atomic>

class StreamerEvents;

struct FrameChunk {
    camera_fb_t* fb;
    char header[256];
    size_t headerLen;
    uint32_t timestamp;
};

class TaskSender {
public:
    TaskSender(StreamTransport* transport, const StreamConfig& config);
    ~TaskSender();

    bool start();
    void stop();
    bool sendFrame(camera_fb_t* fb, const char* header, size_t headerLen);

    void setEventsHandler(StreamerEvents* handler) { _eventsHandler = handler; }

    bool isRunning() const;
    uint32_t getQueueCount() const;
    uint64_t getBytesSent() const;
    uint32_t getFramesSent() const;
    uint32_t getSendFailureCount() const { return _sendFailureCount.load(); }

private:
    static void taskWrapper(void* parameter);
    void taskFunction();
    void _notifySendError(const char* message);

    StreamTransport* _transport;
    const StreamConfig& _config;
    StreamerEvents* _eventsHandler = nullptr;

    TaskHandle_t _taskHandle;
    QueueHandle_t _queue;

    volatile bool _isRunning;
    volatile bool _taskEnded;
    std::atomic<uint64_t> _bytesSent;
    std::atomic<uint32_t> _framesSent;
    std::atomic<uint32_t> _sendFailureCount;

    static const char* TAG;
};

#endif
