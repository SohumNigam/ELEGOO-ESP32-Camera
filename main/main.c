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
#define WIFI_PASS      "PASSWD"

#define PC_IP          "192.168.1.88"   // PC running UDP receiver
#define PC_PORT        5000

#define MAX_PAYLOAD    1200

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
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_QQVGA,
    .jpeg_quality = 25,
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST
};

static void udp_stream_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PC_PORT),
        .sin_addr.s_addr = inet_addr(PC_IP),
    };

    uint16_t frame_id = 0;

    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) continue;

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

            uint8_t buffer[sizeof(header) + MAX_PAYLOAD];
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

        esp_camera_fb_return(fb);

        vTaskDelay(pdMS_TO_TICKS(10)); // ~30–40 FPS
    }
}
void app_main(void)
{
    nvs_flash_init();
    wifi_init();
    esp_camera_init(&camera_config);

    xTaskCreatePinnedToCore(
        udp_stream_task,
        "udp_stream",
        8192,
        NULL,
        5,
        NULL,
        1
    );
}
