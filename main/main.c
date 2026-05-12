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

#define WIFI_SSID      "SSID"
#define WIFI_PASS      "PSWD"

#define PC_IP          "192.168.111.111"   // PC running UDP receiver
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


#define THRESHOLD 100 // 0–255 sensitivity of object detection
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
static int center_offset[2];


void capture_frame_task(void *arg)
{
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


void process_frame(camera_fb_t *fb)
{
    uint8_t width = fb->width;
    uint8_t height = fb->height;

    if (fb->format == PIXFORMAT_GRAYSCALE && width == 160 && height == 120) {

        uint8_t w = 160;
        uint8_t h = 120;

        static uint8_t prev[160 * 120];

        int sum_x = 0;
        int sum_y = 0;
        int count = 0;

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {

                uint8_t p  = fb->buf[y * w + x];
                uint8_t pp = prev[y * w + x];

                if (abs(p - pp) > THRESHOLD) {
                    sum_x += x;
                    sum_y += y;
                    count++;
                }

                prev[y * w + x] = p;
            }
        }

        if (count != 0) {
            uint8_t cx = sum_x / count;
            uint8_t cy = sum_y / count;

            center_offset[0] = (w / 2) - cx;
            center_offset[1] = (h / 2) - cy;

            printf("CENTROID OFFSET DATA--> X: %d, Y: %d\n",
                   center_offset[0],
                   center_offset[1]);
        } else {
            printf("NO OBJECTS DETECTED\n");
        }

    } else {
        ESP_LOGE("FRAME PROCESSING", "unexpected image format!");
    }
}


// Frame processing task
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

            uint16_t total_packets =
                (fb->len + MAX_PAYLOAD - 1) / MAX_PAYLOAD;

            for (uint16_t i = 0; i < total_packets; i++) {

                udp_header_t header;

                header.frame_id = frame_id;
                header.packet_id = i;
                header.total_packets = total_packets;

                uint32_t offset = i * MAX_PAYLOAD;

                header.payload_size =
                    (fb->len - offset > MAX_PAYLOAD)
                        ? MAX_PAYLOAD
                        : fb->len - offset;

                static uint8_t buffer[sizeof(udp_header_t) + MAX_PAYLOAD];

                memcpy(buffer, &header, sizeof(header));
                memcpy(buffer + sizeof(header),
                       fb->buf + offset,
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