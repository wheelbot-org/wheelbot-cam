#include "CameraModule.h"
#include "Arduino.h"
#include "camera_pins.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "CameraModule";

#define XCLK_FREQ 20000000

CameraModule::CameraModule(const char* frame_size_str, const char* jpeg_quality_str) {
    _config.ledc_channel = LEDC_CHANNEL_0;
    _config.ledc_timer = LEDC_TIMER_0;
    _config.pin_d0 = Y2_GPIO_NUM;
    _config.pin_d1 = Y3_GPIO_NUM;
    _config.pin_d2 = Y4_GPIO_NUM;
    _config.pin_d3 = Y5_GPIO_NUM;
    _config.pin_d4 = Y6_GPIO_NUM;
    _config.pin_d5 = Y7_GPIO_NUM;
    _config.pin_d6 = Y8_GPIO_NUM;
    _config.pin_d7 = Y9_GPIO_NUM;
    _config.pin_xclk = XCLK_GPIO_NUM;
    _config.pin_pclk = PCLK_GPIO_NUM;
    _config.pin_vsync = VSYNC_GPIO_NUM;
    _config.pin_href = HREF_GPIO_NUM;
    _config.pin_sccb_sda = SIOD_GPIO_NUM;
    _config.pin_sccb_scl = SIOC_GPIO_NUM;
    _config.pin_pwdn = PWDN_GPIO_NUM;
    _config.pin_reset = RESET_GPIO_NUM;
    _config.xclk_freq_hz = XCLK_FREQ;
    
    _config.pixel_format = PIXFORMAT_JPEG;

    if (strcmp(frame_size_str, "96x96") == 0) _config.frame_size = FRAMESIZE_96X96;
    else if (strcmp(frame_size_str, "QQVGA") == 0) _config.frame_size = FRAMESIZE_QQVGA;
    else if (strcmp(frame_size_str, "QCIF") == 0) _config.frame_size = FRAMESIZE_QCIF;
    else if (strcmp(frame_size_str, "HQVGA") == 0) _config.frame_size = FRAMESIZE_HQVGA;
    else if (strcmp(frame_size_str, "240X240") == 0) _config.frame_size = FRAMESIZE_240X240;
    else if (strcmp(frame_size_str, "QVGA") == 0) _config.frame_size = FRAMESIZE_QVGA;
    else if (strcmp(frame_size_str, "CIF") == 0) _config.frame_size = FRAMESIZE_CIF;
    else if (strcmp(frame_size_str, "HVGA") == 0) _config.frame_size = FRAMESIZE_HVGA;
    else if (strcmp(frame_size_str, "VGA") == 0) _config.frame_size = FRAMESIZE_VGA;
    else if (strcmp(frame_size_str, "SVGA") == 0) _config.frame_size = FRAMESIZE_SVGA;
    else if (strcmp(frame_size_str, "XGA") == 0) _config.frame_size = FRAMESIZE_XGA;
    else if (strcmp(frame_size_str, "HD") == 0) _config.frame_size = FRAMESIZE_HD;
    else if (strcmp(frame_size_str, "SXGA") == 0) _config.frame_size = FRAMESIZE_SXGA;
    else if (strcmp(frame_size_str, "UXGA") == 0) _config.frame_size = FRAMESIZE_UXGA;
    else _config.frame_size = FRAMESIZE_VGA; // Default

     _config.fb_location = CAMERA_FB_IN_PSRAM;
     _config.jpeg_quality = atoi(jpeg_quality_str);
     _config.fb_count = 8;
    _config.grab_mode = CAMERA_GRAB_LATEST;
}

void CameraModule::setup() {
    esp_err_t err = esp_camera_init(&_config);
    if (err != ESP_OK) {
        delay(100);
        ESP_LOGE(TAG, "CRITICAL FAILURE: Camera sensor failed to initialise. %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "A full (hard, power off/on) reboot will probably be needed to recover from this.");
        ESP_LOGE(TAG, "Meanwhile; this unit will reboot in 1 minute since these errors sometime clear automatically");
        periph_module_disable(PERIPH_I2C0_MODULE);
        periph_module_disable(PERIPH_I2C1_MODULE);
        periph_module_reset(PERIPH_I2C0_MODULE);
        periph_module_reset(PERIPH_I2C1_MODULE);
        ESP.restart(); // Restart on camera init failure
        return;
    } 
    
    ESP_LOGI(TAG, "Camera init succeeded");
    ESP_LOGI(TAG, "Camera config: XCLK=%luMHz, Frame Size=%d, FB Count=%d",
             XCLK_FREQ/1000000, _config.frame_size, _config.fb_count);
}

camera_fb_t* CameraModule::get_frame() {
    return esp_camera_fb_get();
}

void CameraModule::return_frame(camera_fb_t* frame) {
    if (frame) {
        esp_camera_fb_return(frame);
    }
}
