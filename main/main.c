#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_camera.h"


#define WIFI_SSID      "NHome"
#define WIFI_PASS      "Bright6963Dance"

#define PC_IP          "192.168.1.88"   // PC running UDP receiver
#define PC_PORT        5000
#define MAX_PAYLOAD    1200



const bool streaming = true;

typedef struct {
    uint16_t frame_id;
    uint16_t packet_id;
    uint16_t total_packets;
    uint16_t payload_size;
} __attribute__((packed)) udp_header_t;


static void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();
}


int THRESHOLD = 20; // sensitivity of corner detection
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

    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST
};


static QueueHandle_t frame_queue;



void capture_frame_task(void *arg){
    while (1) {
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


//invert image and erode to get correct effect
int8_t erode_dilate3x3[] ={ 
                1, 1, 1,
                1, 1, 1,
                1, 1, 1,
};


int8_t gaussian3x3[] ={ 
                1, 2, 1,
                2, 4, 2,
                1, 2, 1,
};

int8_t GY_3x3[] ={ 
                -1, -2, -1,
                0, 0, 0,
                1, 2, 1,
};

int8_t GX_3x3[] ={ 
                -1, 0, 1,
                -2, 0, 2,
                -1, 0, 1,
};


//NOTE: Implement multiple jernel sixe compatibility
void apply_kernel(uint8_t *input, uint8_t *output, const int8_t *kernel, int divisor, int w, int h){

    for(int y = 1; y < h - 1; y++){
        for(int x = 1; x < w - 1; x++){

            int pixel = (input[(y-1) * w + (x-1)] * kernel[0] +
                            input[(y-1) * w + (x)] * kernel[1] +
                            input[(y-1) * w + (x+1)] * kernel[2] +
                            input[(y) * w + (x-1)] * kernel[3] +
                            input[(y) * w + (x)] * kernel[4] +
                            input[(y) * w + (x+1)] * kernel[5] +
                            input[(y+1) * w + (x-1)] * kernel[6] +
                            input[(y+1) * w + (x)] * kernel[7] +
                            input[(y+1) * w + (x+1)] * kernel[8]
            ); 
            
                            
            pixel /= divisor;

            if(pixel < 0) pixel = 0;
            if(pixel > 255) pixel = 255;

            output[y * w + x] = pixel;

        }
    }

}

int8_t circle[16][2] = {
    { 0,-3}, { 1,-3}, { 2,-2}, { 3,-1},
    { 3, 0}, { 3, 1}, { 2, 2}, { 1, 3},
    { 0, 3}, {-1, 3}, {-2, 2}, {-3, 1},
    {-3, 0}, {-3,-1}, {-2,-2}, {-1,-3}
};

int max_diff = 0;

uint8_t fast_corner(uint8_t *input, int x, int y, int w, int8_t offsets[][2]){

        uint8_t contiguous_count = 0;
        uint8_t best_count = 0;


        uint8_t center = input[y * w + x];

        for(int i = 0; i < 16; i++){

            int dx = offsets[i][0];
            int dy = offsets[i][1];
            
            uint8_t target = input[(y + dy) * w + (x + dx)];

            if(abs(center - target) > max_diff){
                max_diff = abs(center - target);
            }

            if(abs(center - target) > THRESHOLD){
                contiguous_count += 1;
            }else{
                best_count = contiguous_count;
                contiguous_count = 0;
            }

        }

        return best_count;

}

int sad_score(uint8_t *curr, uint8_t *prev, int x, int y, int dx, int dy, uint8_t w){
    
    int score = 0;

    for(int j = -2; j < 2; j++){
        for(int i = -2; i < 2; i++){
            
            uint8_t p1 = prev[(y + j) * w + (x + i)];
            uint8_t p2 = curr[(y + dy + j) * w + (x + dx + i)];

            score += abs(p2 - p1);
        }
    }


    return score;
}


typedef struct{
    int x;
    int y;

    int dx;
    int dy;
}feature_t;

typedef struct{
    int x;
    int y;

    int id;

    int dx;
    int dy;

    int feature_count;
}object_t;




//for visualization only

void draw_pixel(uint8_t *img, int w, int h, int x, int y, uint8_t color){

    if(x < 0 || x >= w) return;
    if(y < 0 || y >= h) return;

    img[y * w + x] = color;
}

void draw_cross(uint8_t *img, int w, int h, int x, int y){

    for(int i = -3; i <= 3; i++){

        draw_pixel(img, w, h, x + i, y, 255);
        draw_pixel(img, w, h, x, y + i, 255);
    }
}


//define buffers once globally on startup to avoid repeated memory allocation


static uint8_t *blurred = NULL;
static uint8_t *prev_blurred = NULL;

static int16_t *gx = NULL;
static int16_t *gy = NULL;
static uint8_t *corners = NULL;

uint8_t feature_count = 0;
#define MAX_FEATURES 100

static feature_t *points = NULL;

#define MAX_OBJECTS 20
static object_t *objects = NULL;

static uint8_t *visual = NULL;






bool allocated_buffers = false;


void process_frame(camera_fb_t *fb)
{
    int w = fb->width;
    int h = fb->height;

     // allocate buffers
    if (!allocated_buffers) {

        free(blurred);
        free(gx);
        free(gy);
        free(corners);

        
        blurred = malloc(w * h);
        prev_blurred = malloc(w * h);
        gx = malloc(w * h * sizeof(int16_t));
        gy = malloc(w * h * sizeof(int16_t));
        corners = malloc(w * h);
        points = malloc(MAX_FEATURES * sizeof(feature_t));
        objects = malloc(MAX_OBJECTS * sizeof(object_t));
        visual = malloc(w * h);


        if (!blurred || !gx || !gy || !corners) {
            ESP_LOGE("MEMORY", "Allocation failed");
            return;
        }

        allocated_buffers = true;
    }


    feature_count = 0;
    
    if (fb->format == PIXFORMAT_GRAYSCALE && w == 160 && h == 120) {
        
        apply_kernel(fb->buf, blurred, gaussian3x3, 16, w, h);

        memcpy(visual, blurred, w * h);
        
        max_diff = 0;

        for(int y = 8; y < h-8; y++){
            for(int x = 8; x < w-8; x++){

                int idx = y * w + x;

                uint8_t largest_arc = fast_corner(blurred, x, y, w, circle);


                if(largest_arc > 9){//play with this value and add maximum suppresion if noisy
                    corners[idx] = 255;

                    if(feature_count < MAX_FEATURES){
                        points[feature_count].x = x;
                        points[feature_count].y = y;

                        feature_count += 1;
                    }else{
                        ESP_LOGE("FEATURE DETECTION:", "TOO NOISY");
                    }

                }else{
                    corners[idx] = 0;
                }


            }
        }

        THRESHOLD = max_diff * 0.18;//tune this



        for(int p = 0; p < feature_count; p++){
                    
            int min_score = 1e9;
            int best_dx = 4;
            int best_dy = 4;
            
            for(int dy = -4; dy < 4; dy++){
                for(int dx = -4; dx < 4; dx++){

                    if(points[p].x + dx - 2 < 0) continue;
                    if(points[p].x + dx + 2 >= w) continue;

                    if(points[p].y + dy - 2 < 0) continue;
                    if(points[p].y + dy + 2 >= h) continue;
                        
                    int score = sad_score(blurred, prev_blurred, points[p].x, points[p].y, dx, dy, w);
                    if(score < min_score){

                        min_score = score;
                        best_dx = dx;
                        best_dy = dy;
                        
                    }
                    
                }   
            }
            
            
            if(min_score < 500){//tune
                
                points[p].dx = best_dx;
                points[p].dy = best_dy;
                
            }else{

                //remove the feature from the list if it is lost
                points[p] = points[feature_count - 1];
                feature_count -= 1;
                p -= 1;
            }
            
        }


    memset(objects, 0, MAX_OBJECTS * sizeof(object_t));
    int object_count = 0;
    
    for(int p = 0; p < feature_count; p++) {

        bool found = false;

        for(int o = 0; o < object_count; o++) {

            //object centroid
            int ox = objects[o].x / objects[o].feature_count;
            int oy = objects[o].y / objects[o].feature_count;


            //feature's distance from object centroid
            int dx = points[p].x - ox;
            int dy = points[p].y - oy;

            int dist2 = dx*dx + dy*dy;


            //checking if the feature and object have similar motion
            int ddx =  abs(objects[o].dx - points[p].dx);
            int ddy =  abs(objects[o].dy - points[p].dy);

            int avg = (ddx + ddy)/2;

            if(dist2 < 50*50 && avg < 16) {//within 50 pixels radius and similar mvt within 16 pixels

                // merge into object
                objects[o].x += points[p].x;
                objects[o].y += points[p].y;

                objects[o].dx += points[p].dx;
                objects[o].dy += points[p].dy;

                objects[o].feature_count++;

                found = true;
                break;
            }
        }

    
        if(!found && object_count < MAX_OBJECTS) {

            objects[object_count].x = points[p].x;
            objects[object_count].y = points[p].y;

            objects[object_count].dx = points[p].dx;
            objects[object_count].dy = points[p].dy;

            objects[object_count].feature_count = 1;

            objects[object_count].id = object_count;

            object_count++;
        }


    }

    for(int o = 0; o < object_count; o++) {

        int n = objects[o].feature_count;

        int avg_x = objects[o].x / n;
        int avg_y = objects[o].y / n;

        int avg_dx = objects[o].dx / n;
        int avg_dy = objects[o].dy / n;

        objects[o].x = avg_x;
        objects[o].y = avg_y;

        objects[o].dx = avg_dx;
        objects[o].dy = avg_dy;
    }

    for(int o = 0; o < object_count; o++){

        if(objects[o].feature_count < 3)
            continue;

        draw_cross(
            visual,
            w,
            h,
            objects[o].x,
            objects[o].y
        );
    }




    memcpy(prev_blurred, blurred, w * h);
        
    } else {
        ESP_LOGE("FRAME PROCESSING", "unexpected image format!");
    }
}


// Frame processing task
uint8_t *stream_buffer;
uint32_t stream_size;

void frame_processing_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PC_PORT),
        .sin_addr.s_addr = inet_addr(PC_IP),
    };

    uint16_t frame_id = 0;

    while (1) {

        camera_fb_t *fb = NULL;

        if (xQueueReceive(frame_queue, &fb, pdMS_TO_TICKS(100)) == pdPASS) {
            process_frame(fb);
        } else {
            ESP_LOGW("QUEUE", "No frame available");
        }

        if (streaming && fb) {

            frame_id++;  

            stream_buffer = NULL;
            stream_size = 0;

            //chage to whatever buffer and size i want
            stream_buffer = visual;
            stream_size = fb->height * fb->width;

            uint16_t total_packets =
                (stream_size + MAX_PAYLOAD - 1) / MAX_PAYLOAD;

            for (uint16_t i = 0; i < total_packets; i++) {

                udp_header_t header;

                header.frame_id = frame_id;
                header.packet_id = i;
                header.total_packets = total_packets;

                uint32_t offset = i * MAX_PAYLOAD;

                header.payload_size =
                    (stream_size - offset > MAX_PAYLOAD)
                        ? MAX_PAYLOAD
                        : stream_size - offset;

                static uint8_t buffer[sizeof(udp_header_t) + MAX_PAYLOAD];

                memcpy(buffer, &header, sizeof(header));
                memcpy(buffer + sizeof(header),
                       stream_buffer + offset,
                       header.payload_size);

                sendto(sock,
                       buffer,
                       sizeof(header) + header.payload_size,
                       0,
                       (struct sockaddr *)&dest_addr,
                       sizeof(dest_addr));
            }
        }

        if (fb) {
            esp_camera_fb_return(fb);
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}


void app_main(void)
{
    nvs_flash_init();
    wifi_init();

    vTaskDelay(pdMS_TO_TICKS(5000));

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