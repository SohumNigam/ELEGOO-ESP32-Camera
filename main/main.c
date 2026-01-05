#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"

#include "esp_camera.h"


#define THRESHOLD 250 //0-255 for sensitivity of object detection

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

void print_img(bool *buf, uint8_t w, uint8_t h){

    printf("--------------------------------------start frame-----------------------------\n");
    for(int y = 0; y < h; y+=4){
        for(int x = 0; x < w; x+=4){
            
            printf("%d ", buf[y * w + x]);
        }
        printf("\n");
    }
    printf("--------------------------------------end frame-----------------------------\n");
}


void process_frame(camera_fb_t *fb){
    
    uint8_t width = fb->width;
    uint8_t height = fb->height;
    //uint8_t len = fb->len;


    //bool bin_edge[width * height];//1 = edge, 0 = background
    
    //uint8_t objects[width * height];//0 = background, >0 = object ID (object #1, #2 etc.)
    
    
    if(fb->format == 3 && width == 160 && height == 120){
    
        
        uint8_t w = 160;
        uint8_t h = 120;

        static bool bin_edge[160*120];



        uint8_t count = 0;

        int objects[32];

        for (int y = 1; y < h - 1; y++) {
            for (int x = 1; x < w - 1; x++) {

                uint8_t p0 = fb->buf[(y-1) * width + (x-1)];
                uint8_t p1 = fb->buf[(y-1) * width + x];
                uint8_t p2 = fb->buf[(y-1) * width + (x+1)];
                uint8_t p3 = fb->buf[y * width + (x-1)];
                uint8_t p4 = fb->buf[y * width + x];
                uint8_t p5 = fb->buf[y * width + (x+1)];
                uint8_t p6 = fb->buf[(y+1) * width + (x-1)];
                uint8_t p7 = fb->buf[(y+1) * width + x];
                uint8_t p8 = fb->buf[(y+1) * width + (x+1)];

                int gx = -p0 + p1 + p2 - (2*p3) + p4 + (2*p5) - p6 + p7 + p8;
                int gy = -p0 - (2*p1) - p2 + p3 + p4 + p5 + p6 + (2*p7) + p8; 

                if(abs(gx) > THRESHOLD){
                    bin_edge[y * width + x] = 1;
                }else if(abs(gy) > THRESHOLD){
                    bin_edge[y * width + x] = 1;
                }else{
                    bin_edge[y * width + x] = 0;
                }
            }
        }

        print_img(bin_edge, w, h);


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
