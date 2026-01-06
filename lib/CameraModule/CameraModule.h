#ifndef CAMERA_MODULE_H
#define CAMERA_MODULE_H

#include "esp_camera.h"

class CameraModule {
public:
    CameraModule(const char* frame_size, const char* jpeg_quality);
    void setup();
    camera_fb_t* get_frame();
    void return_frame(camera_fb_t* frame);

private:
    camera_config_t _config;
};

#endif
