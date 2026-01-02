#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"

#include "esp_camera.h"


#define THRESHOLD 100 //0-255 for sensitivity of object detection

#define FRAME_QUEUE_LENGTH 2



camera_config_t camera_config = {
    .pin_pwdn = -1,
    .pin_reset = -1,
    .pin_xclk = 15,
    .pin_sccb_sda = 4,
    .pin_sccb_scl = 5,
    .pin_d7 = 16,
    .pin_d6 = 17,
    .pin_d5 = 18,
    .pin_d4 = 12,
    .pin_d3 = 10,
    .pin_d2 = 8,
    .pin_d1 = 9,
    .pin_d0 = 11,
    .pin_vsync = 6,
    .pin_href = 7,
    .pin_pclk = 13,
    
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_GRAYSCALE,
    .frame_size = FRAMESIZE_QQVGA,
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST
};



static QueueHandle_t frame_queue;

void capture_frame_task(void *arg){

    while(1){
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE("CAPTURE", "Failed to get frame");
            vTaskDelay(pdMS_TO_TICKS(10));
            return;
        }
        
        
        if (xQueueSend(frame_queue, &fb, pdMS_TO_TICKS(10)) != pdPASS) {
        esp_camera_fb_return(fb);
        }
    }
}
      
        
        
        
static int center_offset[2];

void process_frame(camera_fb_t *fb){
    
    uint8_t width = fb->width;
    uint8_t height = fb->height;
    //uint8_t len = fb->len;


    //bool bin_edge[width * height];//1 = edge, 0 = background
    
    //uint8_t objects[width * height];//0 = background, >0 = object ID (object #1, #2 etc.)
    
    
    if(fb->format == 3 && width == 160 && height == 120){
    
        
        uint8_t w = 160;
        uint8_t h = 120;

        static uint8_t prev[160 * 120];//cant use variables here remember to update this if changing resoloution
        memset(prev, 0, w*h);

        int sum_x = 0;
        int sum_y = 0;
        int count = 0;

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint8_t p = fb->buf[y*w + x];
                uint8_t pp = prev[y*w + x];
                if (abs(p - pp) > THRESHOLD) {
                    sum_x += x;
                    sum_y += y;
                    count++;
                }
                prev[y*w + x] = p;
            }
        }

        if(count != 0){
            uint8_t cx = sum_x / count;
            uint8_t cy = sum_y / count;
            
            center_offset[0] = (w/2) - cx;
            center_offset[1] = (h/2) - cy;
            
            printf("CENTROID OFFSET DATA--> X: %d, Y: %d\n", center_offset[0], center_offset[1]);
        
        }else{
            printf("NO OBJECTS DETECTED\n");
        }

    }else{
        ESP_LOGE("FRAME PROCESSING: ", "unexpected image format!");
    }
}   
//Frame processing task
void frame_processing_task(void *arg){
    
    
    while(1){
        camera_fb_t *fb;
        
        // Wait up to 100 ms for a frame
        if (xQueueReceive(frame_queue, &fb, pdMS_TO_TICKS(100)) == pdPASS) {
            process_frame(fb);
            esp_camera_fb_return(fb);
        } else {
            ESP_LOGW("QUEUE", "No frame available");
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
        
    }
}


void app_main(void)
{


    esp_camera_init(&camera_config);


    frame_queue = xQueueCreate(FRAME_QUEUE_LENGTH, sizeof(camera_fb_t *));
    if (frame_queue == NULL) {
        ESP_LOGE("QUEUE", "Failed to create frame queue");
    }


    xTaskCreatePinnedToCore(
        capture_frame_task,
        "capture",
        8192,
        NULL,
        1,
        NULL,
        0
    );

    xTaskCreatePinnedToCore(
        frame_processing_task,
        "Frame_processor",
        8192,
        NULL,
        2,
        NULL,
        1
    );

}
