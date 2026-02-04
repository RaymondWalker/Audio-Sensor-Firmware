/*
V2 - 1wIP Socket
Noise Monitoring Capstone Group
*/

//todo
// re-add microphone
// send data

#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"  
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "esp_system.h"


static const char *TAG = "socket_client";

void wifi_init() {
    nvs_flash_init();
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
    
    // WAIT for connection and print IP
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_get_ip_info(netif, &ip_info);
    
    ESP_LOGI(TAG, "ESP32 IP Address: " IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info.netmask));
    ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info.gw));
}



void socket_client_task() {
    //create socket
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)  {
        ESP_LOGE(TAG, "Socket Creation Failed");
        return;
    }

    //configure destination
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, HOST_IP, &dest_addr.sin_addr);

    int err = connect(sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Connect failed: %d", errno);
        close(sock);
        return;
    }
    ESP_LOGI(TAG, "Connected!");

    // test
    //const char *payload = "hello from ESP";
    //send to server
    //send(sock, payload, strlen(payload), 0);

    //Auth string for server
    //Goal is to be configurable in platformio.ini
    char * json_str = "{'sensor_id': 'sensor_001', 'api_key': 'key123', 'location': 'Raymond Laptop'}";

    send(sock, json_str, strlen(json_str), 0);
    
    // once authenticated, send data
    // this function will go here


    //recieve data
    char rx_buffer[128];
    int len = recv(sock, rx_buffer, sizeof(rx_buffer) -1, 0);
    if (len > 0) {
        rx_buffer[len]=0; //null terminate
        ESP_LOGI(TAG, "Recieved: %s", rx_buffer);

    }
    // fix this
    // closing too soon
    close(sock);
}

void app_main() {
    wifi_init();
    vTaskDelay(5000 / portTICK_PERIOD_MS); // wait for connection
    socket_client_task();
}

