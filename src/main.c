#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "driver/i2s_std.h"

// --- WIFI CONFIG ---
#define WIFI_SSID      "*"
#define WIFI_PASS      "*"

// --- Hardware Config (SPH0645) ---
#define I2S_SCK_PIN     14  // BCLK connected to Pin 14
#define I2S_WS_PIN      15  // LRCL connected to Pin 15
#define I2S_SD_PIN      32  // DOUT connected to Pin 32
#define SAMPLE_RATE     44100
#define READ_LEN        1024
// --- TCP Config ---
#define HOST_IP_ADDR "192.168.0.14"
#define PORT 3333

static const char *TAG = "sound_tcp_node";
static i2s_chan_handle_t rx_handle = NULL;
QueueHandle_t sound_queue;

typedef struct {
    float rms_value;
} sound_data_t;

// --- WiFi Setup Functions ---
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Retrying to connect to the AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void wifi_init_sta(void) {
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// --- Signal Processing ---
float calculate_rms(int32_t *samples, int count) {
    double sum_squares = 0;
    long long sum = 0;
    // Calculate Mean (DC Offset)
    for (int i = 0; i < count; i++) sum += samples[i];
    int32_t dc_offset = sum / count;

    // Calculate Variance
    for (int i = 0; i < count; i++) {
        int32_t val = samples[i] - dc_offset;
        sum_squares += (double)val * val;
    }
    return (float)sqrt(sum_squares / count);
}

// --- Task: I2S Recorder ---
void i2s_recorder_task(void *args) {
    int32_t *r_buf = (int32_t *)calloc(1, READ_LEN);
    size_t bytes_read = 0;
    sound_data_t data_packet;

    while (1) {
        if (i2s_channel_read(rx_handle, r_buf, READ_LEN, &bytes_read, 1000) == ESP_OK) {
            int samples_read = bytes_read / sizeof(int32_t);
            if (samples_read > 0) {
                data_packet.rms_value = calculate_rms(r_buf, samples_read);
                // Send to Queue
                xQueueSend(sound_queue, &data_packet, 0);
            }
        }
    }
    free(r_buf);
}

// --- Task: TCP Client ---
void tcp_client_task(void *pvParameters) {
    char host_ip[] = HOST_IP_ADDR;
    sound_data_t received_sound;

    while (1) {
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(host_ip);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(PORT);
        
        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            close(sock);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "Connected to TCP Server");

        while (1) {
            if (xQueueReceive(sound_queue, &received_sound, portMAX_DELAY) == pdTRUE) {
                char payload[64];
                snprintf(payload, sizeof(payload), "RMS:%.2f\n", received_sound.rms_value);
                if (send(sock, payload, strlen(payload), 0) < 0) {
                    ESP_LOGE(TAG, "Send failed, reconnecting...");
                    break; 
                }
            }
        }
        close(sock);
    }
    vTaskDelete(NULL);
}

// --- Init: I2S Hardware (Strict Debug Version) ---
void init_microphone() {
    ESP_LOGI(TAG, "Initializing I2S Microphone...");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_SCK_PIN,
            .ws = I2S_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_SD_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    // SPH0645 requires the Left slot for MSB mode
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; 

    ESP_LOGI(TAG, "Configuring I2S Channel...");
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));

    ESP_LOGI(TAG, "Enabling I2S Channel...");
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    
    ESP_LOGI(TAG, "I2S Initialization Complete.");
}
// --- Main ---
void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_sta();

    sound_queue = xQueueCreate(10, sizeof(sound_data_t));
    init_microphone();

    xTaskCreate(i2s_recorder_task, "i2s_recorder", 4096, NULL, 5, NULL);
    xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, NULL);
}