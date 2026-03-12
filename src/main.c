// main/main.c                                                                                                                                  
  #include <string.h>                                                                                                                             
  #include "freertos/FreeRTOS.h"                                                                                                                  
  #include "freertos/task.h"                                                                                                                      
  #include "freertos/event_groups.h"                                                                                                              
  #include "esp_system.h"                                                                                                                         
  #include "esp_wifi.h"                                                                                                                           
  #include "esp_event.h"                                                                                                                          
  #include "esp_log.h"                                                                                                                            
  #include "nvs_flash.h"                                                                                                                          
  #include "esp_netif.h"                                                                                                                          
  #include "esp_http_client.h"                                                                                                                    
  #include "lwip/sockets.h"                                                                                                                       
  #include "driver/i2s_std.h"                                                                                                                     
                                                                                                                                                  
  static const char *TAG = "hospital_sensor";                                                                                                     
                                                                                                                                                  
  // ============== Configuration ==============                                                                                                                                                                                                 
  #define SERVER_HOST         HOST_IP                                                                                                   
  #define TCP_PORT            8001                                                                                                                
  #define HTTP_PORT           8000                                                                                                                
  #define SENSOR_ID           "sensor_001"                                                                                                        
  #define API_KEY             "key123"                                                                                                            
  #define LOCATION            "Room 101"                                                                                                          
                                                                                                                                                  
  // Audio settings                                                                                                                               
  #define SAMPLE_RATE         16000                                                                                                               
  #define AUDIO_BUFFER_SIZE   1024  // samples per chunk                                                                                          
                                                                                                                                                  
  // Heartbeat interval (ms)                                                                                                                      
  #define HEARTBEAT_INTERVAL_MS  30000                                                                                                            
                                                                                                                                                  
  // WiFi retry settings                                                                                                                          
  #define WIFI_MAXIMUM_RETRY  5                                                                                                                   
                                                                                                                                                  
  // ============== Global State ==============                                                                                                   
  static EventGroupHandle_t s_wifi_event_group;                                                                                                   
  #define WIFI_CONNECTED_BIT BIT0                                                                                                                 
  #define WIFI_FAIL_BIT      BIT1                                                                                                                 
                                                                                                                                                  
  static int s_retry_num = 0;                                                                                                                     
  static int tcp_sock = -1;                                                                                                                       
  static bool tcp_authenticated = false;                                                                                                          
  static i2s_chan_handle_t rx_handle = NULL;                                                                                                      
                                                                                                                                                  
  // ============== WiFi Event Handler ==============                                                                                             
  static void wifi_event_handler(void* arg, esp_event_base_t event_base,                                                                          
                                 int32_t event_id, void* event_data)                                                                              
  {                                                                                                                                               
      if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {                                                                         
          esp_wifi_connect();                                                                                                                     
      } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {                                                           
          if (s_retry_num < WIFI_MAXIMUM_RETRY) {                                                                                                 
              esp_wifi_connect();                                                                                                                 
              s_retry_num++;                                                                                                                      
              ESP_LOGI(TAG, "Retrying WiFi connection...");                                                                                       
          } else {                                                                                                                                
              xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);                                                                              
          }                                                                                                                                       
          ESP_LOGI(TAG, "WiFi disconnected");                                                                                                     
      } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {                                                                     
          ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;                                                                             
          ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));                                                                            
          s_retry_num = 0;                                                                                                                        
          xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);                                                                             
      }                                                                                                                                           
  }                                                                                                                                               
                                                                                                                                                  
  // ============== WiFi Initialization ==============                                                                                            
  static esp_err_t wifi_init_sta(void)                                                                                                            
  {                                                                                                                                               
      s_wifi_event_group = xEventGroupCreate();                                                                                                   
                                                                                                                                                  
      ESP_ERROR_CHECK(esp_netif_init());                                                                                                          
      ESP_ERROR_CHECK(esp_event_loop_create_default());                                                                                           
      esp_netif_create_default_wifi_sta();                                                                                                        
                                                                                                                                                  
      wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();                                                                                        
      ESP_ERROR_CHECK(esp_wifi_init(&cfg));                                                                                                       
                                                                                                                                                  
      esp_event_handler_instance_t instance_any_id;                                                                                               
      esp_event_handler_instance_t instance_got_ip;                                                                                               
      ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,                                                           
                      &wifi_event_handler, NULL, &instance_any_id));                                                                              
      ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,                                                          
                      &wifi_event_handler, NULL, &instance_got_ip));                                                                              
                                                                                                                                                  
      wifi_config_t wifi_config = {                                                                                                               
          .sta = {                                                                                                                                
              .ssid = WIFI_SSID,                                                                                                                  
              .password = WIFI_PASS,                                                                                                              
              .threshold.authmode = WIFI_AUTH_WPA_PSK,                                                                                           
          },                                                                                                                                      
      };                                                                                                                                          
      ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));                                                                                          
      ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));                                                                            
      ESP_ERROR_CHECK(esp_wifi_start());                                                                                                          
                                                                                                                                                  
      ESP_LOGI(TAG, "Connecting to WiFi...");                                                                                                     
                                                                                                                                                  
      EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,                                                                                  
              WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,                                                                                                 
              pdFALSE, pdFALSE, portMAX_DELAY);                                                                                                   
                                                                                                                                                  
      if (bits & WIFI_CONNECTED_BIT) {                                                                                                            
          ESP_LOGI(TAG, "Connected to WiFi SSID: %s", WIFI_SSID);                                                                                 
          return ESP_OK;                                                                                                                          
      } else {                                                                                                                                    
          ESP_LOGE(TAG, "Failed to connect to WiFi");                                                                                             
          return ESP_FAIL;                                                                                                                        
      }                                                                                                                                           
  }                                                                                                                                               
                                                                                                                                                  
  // ============== TCP Connection ==============                                                                                                 
  static int tcp_connect(void)                                                                                                                    
  {                                                                                                                                               
      struct sockaddr_in dest_addr;                                                                                                               
      dest_addr.sin_addr.s_addr = inet_addr(SERVER_HOST);                                                                                         
      dest_addr.sin_family = AF_INET;                                                                                                             
      dest_addr.sin_port = htons(TCP_PORT);                                                                                                       
                                                                                                                                                  
      int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);                                                                                        
      if (sock < 0) {                                                                                                                             
          ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);                                                                              
          return -1;                                                                                                                              
      }                                                                                                                                           
                                                                                                                                                  
      ESP_LOGI(TAG, "Connecting to TCP server %s:%d...", SERVER_HOST, TCP_PORT);                                                                  
      int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));                                                                  
      if (err != 0) {                                                                                                                             
          ESP_LOGE(TAG, "TCP connect failed: errno %d", errno);                                                                                   
          close(sock);                                                                                                                            
          return -1;                                                                                                                              
      }                                                                                                                                           
                                                                                                                                                  
      ESP_LOGI(TAG, "TCP connected");                                                                                                             
      return sock;                                                                                                                                
  }                                                                                                                                               
                                                                                                                                                  
  static bool tcp_authenticate(int sock)                                                                                                          
  {                                                                                                                                               
      // Send JSON handshake                                                                                                                      
      char handshake[256];                                                                                                                        
      snprintf(handshake, sizeof(handshake),                                                                                                      
               "{\"sensor_id\":\"%s\",\"api_key\":\"%s\",\"location\":\"%s\"}\n",                                                                 
               SENSOR_ID, API_KEY, LOCATION);                                                                                                     
                                                                                                                                                  
      int sent = send(sock, handshake, strlen(handshake), 0);                                                                                     
      if (sent < 0) {                                                                                                                             
          ESP_LOGE(TAG, "Failed to send handshake: errno %d", errno);                                                                             
          return false;                                                                                                                           
      }                                                                                                                                           
                                                                                                                                                  
      // Wait for response                                                                                                                        
      char response[256];                                                                                                                         
      struct timeval timeout = { .tv_sec = 5, .tv_usec = 0 };                                                                                     
      setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));                                                                       
                                                                                                                                                  
      int len = recv(sock, response, sizeof(response) - 1, 0);                                                                                    
      if (len <= 0) {                                                                                                                             
          ESP_LOGE(TAG, "No response from server");                                                                                               
          return false;                                                                                                                           
      }                                                                                                                                           
      response[len] = '\0';                                                                                                                       
                                                                                                                                                  
      if (strstr(response, "authenticated") != NULL) {                                                                                            
          ESP_LOGI(TAG, "TCP authenticated successfully");                                                                                        
          return true;                                                                                                                            
      } else {                                                                                                                                    
          ESP_LOGE(TAG, "Authentication failed: %s", response);                                                                                   
          return false;                                                                                                                           
      }                                                                                                                                           
  }                                                                                                                                               
                                                                                                                                                  
  static void tcp_disconnect(void)                                                                                                                
  {                                                                                                                                               
      if (tcp_sock >= 0) {                                                                                                                        
          close(tcp_sock);                                                                                                                        
          tcp_sock = -1;                                                                                                                          
      }                                                                                                                                           
      tcp_authenticated = false;                                                                                                                  
  }                                                                                                                                               
                                                                                                                                                  
  static bool tcp_ensure_connected(void)                                                                                                          
  {                                                                                                                                               
      if (tcp_sock >= 0 && tcp_authenticated) {                                                                                                   
          return true;                                                                                                                            
      }                                                                                                                                           
                                                                                                                                                  
      tcp_disconnect();                                                                                                                           
                                                                                                                                                  
      tcp_sock = tcp_connect();                                                                                                                   
      if (tcp_sock < 0) {                                                                                                                         
          return false;                                                                                                                           
      }                                                                                                                                           
                                                                                                                                                  
      tcp_authenticated = tcp_authenticate(tcp_sock);                                                                                             
      if (!tcp_authenticated) {                                                                                                                   
          tcp_disconnect();                                                                                                                       
          return false;                                                                                                                           
      }                                                                                                                                           
                                                                                                                                                  
      return true;                                                                                                                                
  }                                                                                                                                               
                                                                                                                                                  
  // ============== HTTP Heartbeat ==============                                                                                                 
  static float get_battery_percent(void)                                                                                                          
  {                                                                                                                                               
      // Implement battery reading based on your hardware                                                                                         
      // Example: ADC reading from voltage divider                                                                                                
      return 100.0f;                                                                                                                              
  }                                                                                                                                               
                                                                                                                                                  
  static void send_heartbeat(void)                                                                                                                
  {                                                                                                                                               
      wifi_ap_record_t ap_info;                                                                                                                   
      int8_t rssi = -100;                                                                                                                         
      if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {                                                                                         
          rssi = ap_info.rssi;                                                                                                                    
      }                                                                                                                                           
                                                                                                                                                  
      float battery = get_battery_percent();                                                                                                      
                                                                                                                                                  
      char url[128];                                                                                                                              
      snprintf(url, sizeof(url), "http://%s:%d/api/device-metrics/heartbeat",                                                                     
               SERVER_HOST, HTTP_PORT);                                                                                                           
                                                                                                                                                  
      char post_data[256];                                                                                                                        
      snprintf(post_data, sizeof(post_data),                                                                                                      
               "{\"battery_percent\":%.1f,\"signal_strength_dbm\":%d,\"firmware_version\":\"1.0.0\"}",                                            
               battery, rssi);                                                                                                                    
                                                                                                                                                  
      esp_http_client_config_t config = {                                                                                                         
          .url = url,                                                                                                                             
          .method = HTTP_METHOD_POST,                                                                                                             
          .timeout_ms = 5000,                                                                                                                     
      };                                                                                                                                          
                                                                                                                                                  
      esp_http_client_handle_t client = esp_http_client_init(&config);                                                                            
                                                                                                                                                  
      esp_http_client_set_header(client, "Content-Type", "application/json");                                                                     
      esp_http_client_set_header(client, "X-API-Key", API_KEY);                                                                                   
      esp_http_client_set_header(client, "X-Sensor-ID", SENSOR_ID);                                                                               
      esp_http_client_set_header(client, "X-Location", LOCATION);                                                                                 
      esp_http_client_set_post_field(client, post_data, strlen(post_data));                                                                       
                                                                                                                                                  
      esp_err_t err = esp_http_client_perform(client);                                                                                            
      if (err == ESP_OK) {                                                                                                                        
          int status = esp_http_client_get_status_code(client);                                                                                   
          if (status == 200) {                                                                                                                    
              ESP_LOGI(TAG, "Heartbeat sent successfully");                                                                                       
          } else {                                                                                                                                
              ESP_LOGW(TAG, "Heartbeat returned status %d", status);                                                                              
          }                                                                                                                                       
      } else {                                                                                                                                    
          ESP_LOGE(TAG, "Heartbeat failed: %s", esp_err_to_name(err));                                                                            
      }                                                                                                                                           
                                                                                                                                                  
      esp_http_client_cleanup(client);                                                                                                            
  }                                                                                                                                               
                                                                                                                                                  
  // ============== I2S Microphone (INMP441 example) ==============                                                                               
  static esp_err_t i2s_init(void)                                                                                                                 
  {                                                                                                                                               
      i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);                                                        
      ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));                                                                              
                                                                                                                                                  
      i2s_std_config_t std_cfg = {                                                                                                                
          .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),                                                                                     
          .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),                                          
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
                                                                                                                                                  
      ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));                                                                            
      ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));                                                                                             
                                                                                                                                                  
      ESP_LOGI(TAG, "I2S initialized");                                                                                                           
      return ESP_OK;                                                                                                                              
  }                                                                                                                                               
                                                                                                                                                  
  static size_t read_microphone(int16_t *buffer, size_t samples)                                                                                  
  {                                                                                                                                               
      size_t bytes_read = 0;                                                                                                                      
      esp_err_t err = i2s_channel_read(rx_handle, buffer, samples * sizeof(int16_t),                                                              
                                        &bytes_read, pdMS_TO_TICKS(1000));                                                                        
      if (err != ESP_OK) {                                                                                                                        
          ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(err));                                                                             
          return 0;                                                                                                                               
      }                                                                                                                                           
      return bytes_read / sizeof(int16_t);                                                                                                        
  }                                                                                                                                               
                                                                                                                                                  
  // ============== Audio Streaming Task ==============                                                                                           
  static void audio_stream_task(void *pvParameters)                                                                                               
  {                                                                                                                                               
      int16_t *audio_buffer = heap_caps_malloc(AUDIO_BUFFER_SIZE * sizeof(int16_t), MALLOC_CAP_DMA);                                              
      if (audio_buffer == NULL) {                                                                                                                 
          ESP_LOGE(TAG, "Failed to allocate audio buffer");                                                                                       
          vTaskDelete(NULL);                                                                                                                      
          return;                                                                                                                                 
      }                                                                                                                                           
                                                                                                                                                  
      while (1) {                                                                                                                                 
          // Ensure TCP connection                                                                                                                
          if (!tcp_ensure_connected()) {                                                                                                          
              ESP_LOGW(TAG, "TCP not connected, retrying in 5s...");                                                                              
              vTaskDelay(pdMS_TO_TICKS(5000));                                                                                                    
              continue;                                                                                                                           
          }                                                                                                                                       
                                                                                                                                                  
          // Read audio from microphone                                                                                                           
          size_t samples_read = read_microphone(audio_buffer, AUDIO_BUFFER_SIZE);                                                                 
          if (samples_read == 0) {                                                                                                                
              vTaskDelay(pdMS_TO_TICKS(10));                                                                                                      
              continue;                                                                                                                           
          }                                                                                                                                       
                                                                                                                                                  
          // Send raw PCM bytes over TCP                                                                                                          
          int sent = send(tcp_sock, audio_buffer, samples_read * sizeof(int16_t), 0);                                                             
          if (sent < 0) {                                                                                                                         
              ESP_LOGE(TAG, "TCP send failed: errno %d", errno);                                                                                  
              tcp_disconnect();                                                                                                                   
          }                                                                                                                                       
      }                                                                                                                                           
                                                                                                                                                  
      free(audio_buffer);                                                                                                                         
      vTaskDelete(NULL);                                                                                                                          
  }                                                                                                                                               
                                                                                                                                                  
  // ============== Heartbeat Task ==============                                                                                                 
  static void heartbeat_task(void *pvParameters)                                                                                                  
  {                                                                                                                                               
      while (1) {                                                                                                                                 
          // Wait for WiFi connection                                                                                                             
          EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,                                                                              
                  WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);                                                                           
                                                                                                                                                  
          if (bits & WIFI_CONNECTED_BIT) {                                                                                                        
              send_heartbeat();                                                                                                                   
          }                                                                                                                                       
                                                                                                                                                  
          vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));                                                                                       
      }                                                                                                                                           
                                                                                                                                                  
      vTaskDelete(NULL);                                                                                                                          
  }                                                                                                                                               
                                                                                                                                                  
  // ============== Main ==============                                                                                                           
  void app_main(void)                                                                                                                             
  {                                                                                                                                               
      // Initialize NVS                                                                                                                           
      esp_err_t ret = nvs_flash_init();                                                                                                           
      if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {                                                             
          ESP_ERROR_CHECK(nvs_flash_erase());                                                                                                     
          ret = nvs_flash_init();                                                                                                                 
      }                                                                                                                                           
      ESP_ERROR_CHECK(ret);                                                                                                                       
                                                                                                                                                  
      ESP_LOGI(TAG, "Hospital Sound Sensor Starting...");                                                                                         
      ESP_LOGI(TAG, "Sensor ID: %s", SENSOR_ID);                                                                                                  
      ESP_LOGI(TAG, "Location: %s", LOCATION);                                                                                                    
                                                                                                                                                  
      // Initialize WiFi                                                                                                                          
      if (wifi_init_sta() != ESP_OK) {                                                                                                            
          ESP_LOGE(TAG, "WiFi initialization failed");                                                                                            
          return;                                                                                                                                 
      }                                                                                                                                           
                                                                                                                                                  
      // Initialize I2S microphone                                                                                                                
      if (i2s_init() != ESP_OK) {                                                                                                                 
          ESP_LOGE(TAG, "I2S initialization failed");                                                                                             
          return;                                                                                                                                 
      }                                                                                                                                           
                                                                                                                                                  
      // Start audio streaming task                                                                                                               
      xTaskCreate(audio_stream_task, "audio_stream", 4096, NULL, 5, NULL);                                                                        
                                                                                                                                                  
      // Start heartbeat task                                                                                                                     
      xTaskCreate(heartbeat_task, "heartbeat", 4096, NULL, 4, NULL);                                                                              
                                                                                                                                                  
      ESP_LOGI(TAG, "Sensor initialized and running");                                                                                            
  };
