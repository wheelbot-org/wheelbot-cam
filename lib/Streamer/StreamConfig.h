#ifndef STREAM_CONFIG_H
#define STREAM_CONFIG_H

#include <cstddef>
#include <cstdint>

struct StreamConfig {
    const char* boundary = "wheelbot";
    const char* contentType = "multipart/x-mixed-replace";
    const char* frameRate = "60";

    size_t bufferSize = 32768;
    size_t txBufferSize = 32768;

    uint64_t maxDataSize = 100000000LL;

    uint32_t reconnectInterval = 5000;
    uint32_t maxReconnectInterval = 60000;
    float reconnectMultiplier = 2.0f;

    uint32_t metricsUpdateInterval = 1000;
    uint32_t slowChunkThreshold = 50;

    uint32_t maxFPS = 30;
    size_t taskQueueSize = 16;
      size_t taskStackDepth = 8192;
      uint32_t taskPriority = 5;
    uint32_t taskDelayMs = 1;
    const char* defaultJpegQuality = "10";
    size_t chunkSize = 4096;
    uint32_t sendErrorDelayMs = 100;
    uint32_t maxSendFailures = 3;
};

#endif
