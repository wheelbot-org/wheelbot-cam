#include "TaskSender.h"
#include "StreamerEvents.h"
#include "esp_log.h"
#include <cstring>

const char* TaskSender::TAG = "TaskSender";

TaskSender::TaskSender(StreamTransport* transport, const StreamConfig& config)
    : _transport(transport),
      _config(config),
      _taskHandle(nullptr),
      _queue(nullptr),
      _isRunning(false),
      _taskEnded(false),
      _bytesSent(0),
      _framesSent(0),
      _sendFailureCount(0)
{
}

TaskSender::~TaskSender() {
    stop();
}

bool TaskSender::start() {
    _queue = xQueueCreate(_config.taskQueueSize, sizeof(FrameChunk));
    if (!_queue) {
        ESP_LOGE(TAG, "Failed to create queue");
        return false;
    }

    BaseType_t result = xTaskCreate(
        TaskSender::taskWrapper,
        "TaskSender",
        _config.taskStackDepth,
        this,
        _config.taskPriority,
        &_taskHandle
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        vQueueDelete(_queue);
        _queue = nullptr;
        return false;
    }

    _isRunning = true;
    ESP_LOGI(TAG, "TaskSender started (queue: %u, stack: %u, priority: %u)",
             _config.taskQueueSize, _config.taskStackDepth, _config.taskPriority);
    return true;
}

void TaskSender::stop() {
    if (!_isRunning) {
        return;
    }

    _isRunning = false;

    vTaskDelay(pdMS_TO_TICKS(200));

    if (_taskHandle) {
        int timeout = 0;
        while (!_taskEnded && timeout < 50) {
            vTaskDelay(pdMS_TO_TICKS(100));
            timeout++;
        }

        if (_taskEnded) {
            ESP_LOGI(TAG, "Task ended gracefully");
            vTaskDelete(_taskHandle);
        } else {
            ESP_LOGW(TAG, "Task did not end gracefully, forcing deletion");
            vTaskDelete(_taskHandle);
        }

        _taskHandle = nullptr;
    }

    if (_queue) {
        FrameChunk chunk;
        while (uxQueueMessagesWaiting(_queue) > 0) {
            if (xQueueReceive(_queue, &chunk, 0) == pdPASS) {
                if (chunk.fb) {
                    esp_camera_fb_return(chunk.fb);
                }
            }
        }
        vQueueDelete(_queue);
        _queue = nullptr;
    }

    ESP_LOGI(TAG, "TaskSender stopped (sent: %llu bytes, %u frames)",
             _bytesSent.load(), _framesSent.load());
}

bool TaskSender::sendFrame(camera_fb_t* fb, const char* header, size_t headerLen) {
    if (!_isRunning || !_queue || !fb) {
        return false;
    }

    if (headerLen > 255) {
        ESP_LOGE(TAG, "Header too large: %u", headerLen);
        esp_camera_fb_return(fb);
        return false;
    }

    FrameChunk chunk;
    chunk.fb = fb;
    chunk.headerLen = headerLen;
    chunk.timestamp = millis();
    memcpy(chunk.header, header, headerLen);

    BaseType_t result = xQueueSend(_queue, &chunk, pdMS_TO_TICKS(10));
    if (result != pdPASS) {
        ESP_LOGW(TAG, "Queue full, dropping frame");
        esp_camera_fb_return(fb);
        return false;
    }

    return true;
}

bool TaskSender::isRunning() const {
    return _isRunning;
}

uint32_t TaskSender::getQueueCount() const {
    if (!_queue) {
        return 0;
    }
    return uxQueueMessagesWaiting(_queue);
}

uint64_t TaskSender::getBytesSent() const {
    return _bytesSent.load();
}

uint32_t TaskSender::getFramesSent() const {
    return _framesSent.load();
}

void TaskSender::taskWrapper(void* parameter) {
    TaskSender* sender = static_cast<TaskSender*>(parameter);
    sender->taskFunction();
}

void TaskSender::taskFunction() {
    ESP_LOGI(TAG, "Send task started");

    while (_isRunning) {
        FrameChunk chunk;

        BaseType_t result = xQueueReceive(_queue, &chunk, pdMS_TO_TICKS(100));

        if (result == pdPASS && _isRunning) {
            bool success = true;

            if (chunk.headerLen > 0) {
                success = _transport->send((uint8_t*)chunk.header, chunk.headerLen);
                if (!success) {
                    uint32_t failCount = ++_sendFailureCount;
                    uint32_t remaining = _config.maxSendFailures - failCount;
                    if (failCount == 1) {
                        ESP_LOGE(TAG, "Failed to send header");
                    } else if (failCount <= _config.maxSendFailures) {
                        ESP_LOGW(TAG, "Failed to send header (attempt %u/%u, %u remaining)",
                                failCount, _config.maxSendFailures, remaining);
                    }
                    _notifySendError(_transport->getLastError());
                }
            }

            if (success && chunk.fb) {
                success = _transport->send(chunk.fb->buf, chunk.fb->len);
                if (!success) {
                    uint32_t failCount = ++_sendFailureCount;
                    uint32_t remaining = _config.maxSendFailures - failCount;
                    if (failCount == 1) {
                        ESP_LOGE(TAG, "Failed to send frame data");
                    } else if (failCount <= _config.maxSendFailures) {
                        ESP_LOGW(TAG, "Failed to send frame data (attempt %u/%u, %u remaining)",
                                failCount, _config.maxSendFailures, remaining);
                    }
                    _notifySendError(_transport->getLastError());
                }
            }

            if (success) {
                _bytesSent += chunk.fb->len;
                _framesSent++;
                _sendFailureCount = 0;
            }

            if (chunk.fb) {
                esp_camera_fb_return(chunk.fb);
            }

            if (!success) {
                vTaskDelay(pdMS_TO_TICKS(_config.sendErrorDelayMs));
            } else if (_config.taskDelayMs > 0) {
                vTaskDelay(pdMS_TO_TICKS(_config.taskDelayMs));
            }
        }
    }

    _taskEnded = true;
    ESP_LOGI(TAG, "Send task ended");
}

void TaskSender::_notifySendError(const char* message) {
    if (_eventsHandler) {
        _eventsHandler->onSendError(message);
    }
}
