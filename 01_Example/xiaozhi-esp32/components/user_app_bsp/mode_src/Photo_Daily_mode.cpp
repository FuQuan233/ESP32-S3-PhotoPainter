#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <esp_heap_caps.h>
#include <nvs_flash.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <esp_smartconfig.h>
#include <esp_http_client.h>
#include <esp_sntp.h>
#include <esp_mac.h>
#include <esp_chip_info.h>
#include <esp_app_desc.h>
#include <esp_crt_bundle.h>
#include "display_bsp.h"
#include "button_bsp.h"
#include "user_app.h"
#include "power_bsp.h"
#include "codec_bsp.h"
#include "i2c_bsp.h"

#define TAG "PhotoDailyMode"
#define ext_wakeup_pin_3 GPIO_NUM_4

static CodecPort* AudioPort = NULL;  // 音频播放端口

// 异步提示音播放队列 (避免在 sys_evt 等小栈任务中直接调用 opus_decode)
static QueueHandle_t s_prompt_queue = NULL;

static void prompt_sound_task(void *arg) {
    PromptSound sound;
    for (;;) {
        if (xQueueReceive(s_prompt_queue, &sound, portMAX_DELAY) == pdTRUE) {
            if (AudioPort) {
                AudioPort->Codec_PlayPromptSound(sound);
            }
        }
    }
}

// 异步播放提示音 - 可安全在任意任务/回调中调用
static void play_prompt_async(PromptSound sound) {
    if (s_prompt_queue) {
        xQueueSend(s_prompt_queue, &sound, pdMS_TO_TICKS(100));
    }
}

// 配置参数 - 可以通过NVS持久化存储
#define DEFAULT_IMAGE_URL "https://stonephoto.fuquan.moe/api/get_today_image"
#define DEFAULT_STATUS_URL "https://stonephoto.fuquan.moe/api/device_status"
#define DEFAULT_API_KEY "your_secret_api_key_12345"
#define DEFAULT_WAKE_HOUR 8
#define DEFAULT_WAKE_MINUTE 0
#define TIMEZONE_OFFSET_HOURS 8  // 中国时区 UTC+8

static EventGroupHandle_t s_wifi_event_group;
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static bool s_is_wifi_connected = false;

// NVS存储的配置
typedef struct {
    char image_url[256];
    char status_url[256];
    char api_key[128];
    uint8_t wake_hour;
    uint8_t wake_minute;
    bool is_configured;
} photo_daily_config_t;

// ESPTouch配网相关
static void smartconfig_task(void *parm);
static bool s_wifi_configured = false;  // 标记WiFi是否已配置

// 检查WiFi是否已配置（NVS中有保存的配置）
static bool is_wifi_configured(void)
{
    wifi_config_t wifi_config;
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
    if (err == ESP_OK && strlen((char*)wifi_config.sta.ssid) > 0) {
        ESP_LOGI(TAG, "Found saved WiFi config, SSID: %s", wifi_config.sta.ssid);
        return true;
    }
    return false;
}

// WiFi事件处理
static void event_handler(void* arg, esp_event_base_t event_base, 
                         int32_t event_id, void* event_data)
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // 检查是否有已保存的WiFi配置
        if (is_wifi_configured()) {
            // 有保存的配置，直接连接
            s_wifi_configured = true;
            ESP_LOGI(TAG, "Using saved WiFi configuration, connecting...");
            if (wakeup_reason != ESP_SLEEP_WAKEUP_TIMER){
                play_prompt_async(PROMPT_WIFI_CONNECTING);
            }
            esp_wifi_connect();
        } else {
            // 没有保存的配置，启动SmartConfig
            s_wifi_configured = false;
            ESP_LOGI(TAG, "No saved WiFi config, starting SmartConfig...");
            if (wakeup_reason != ESP_SLEEP_WAKEUP_TIMER){
                play_prompt_async(PROMPT_WAIT_CONFIG);
            }
            xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 3, NULL);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_configured) {
            // 已配置的WiFi断开，尝试重连
            ESP_LOGI(TAG, "WiFi disconnected, trying to reconnect...");
            esp_wifi_connect();
        }
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
        s_is_wifi_connected = false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
        s_is_wifi_connected = true;
        ESP_LOGI(TAG, "Got IP address");
        if (wakeup_reason != ESP_SLEEP_WAKEUP_TIMER){
            play_prompt_async(PROMPT_WIFI_SUCCESS);
        }
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);

        ESP_ERROR_CHECK(esp_wifi_disconnect());
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        s_wifi_configured = true;
        ESP_ERROR_CHECK(esp_wifi_connect());
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

static void smartconfig_task(void *parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));
    
    ESP_LOGI(TAG, "ESPTouch started, please use app to configure WiFi");

    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi connected");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "SmartConfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}

// 初始化WiFi为STA模式
static void initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    s_wifi_event_group = xEventGroupCreate();
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// HTTP事件处理
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    default:
        break;
    }
    return ESP_OK;
}

// 获取唤醒原因字符串
static const char* get_wakeup_reason_string(esp_sleep_wakeup_cause_t reason) {
    switch (reason) {
        case ESP_SLEEP_WAKEUP_UNDEFINED: return "undefined";
        case ESP_SLEEP_WAKEUP_ALL:       return "all";
        case ESP_SLEEP_WAKEUP_EXT0:      return "ext0";
        case ESP_SLEEP_WAKEUP_EXT1:      return "ext1_button";
        case ESP_SLEEP_WAKEUP_TIMER:     return "timer";
        case ESP_SLEEP_WAKEUP_TOUCHPAD:  return "touchpad";
        case ESP_SLEEP_WAKEUP_ULP:       return "ulp";
        case ESP_SLEEP_WAKEUP_GPIO:      return "gpio";
        case ESP_SLEEP_WAKEUP_UART:      return "uart";
        default: return "unknown";
    }
}

// 向服务器上报设备状态
static bool report_device_status(const char *status_url, const char *api_key, const char *wakeup_reason)
{
    if (!s_is_wifi_connected) {
        ESP_LOGW(TAG, "WiFi not connected, skip status report");
        return false;
    }

    // 获取WiFi信息
    wifi_ap_record_t ap_info;
    esp_wifi_sta_get_ap_info(&ap_info);
    
    // 获取IP地址
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif, &ip_info);
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    
    // 获取MAC地址
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    // 获取电池信息
    battery_info_t batt = Get_BatteryInfo();
    
    // 获取芯片信息
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    // 获取固件版本
    const esp_app_desc_t *app_desc = esp_app_get_description();
    
    // 获取当前时间
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    // 获取内存信息
    size_t free_heap = esp_get_free_heap_size();
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    
    // 构建JSON格式的状态数据
    /*
     * API格式说明:
     * POST /api/device_status
     * Content-Type: application/json
     * X-API-Key: <api_key>
     * 
     * {
     *   "device_id": "AA:BB:CC:DD:EE:FF",      // MAC地址作为设备唯一ID
     *   "timestamp": "2026-02-03 12:00:00",     // 设备本地时间
     *   "wakeup_reason": "timer|ext1_button|undefined",  // 唤醒原因
     *   "wifi": {
     *     "ssid": "MyWiFi",                     // 连接的WiFi名称
     *     "rssi": -65,                          // 信号强度 dBm
     *     "channel": 6,                         // WiFi信道
     *     "ip": "192.168.1.100",                // IP地址
     *     "mac": "AA:BB:CC:DD:EE:FF"            // MAC地址
     *   },
     *   "battery": {
     *     "voltage_mv": 4200,                   // 电池电压 mV
     *     "percent": 85,                        // 电量百分比 0-100
     *     "is_charging": false,                 // 是否正在充电
     *     "charge_status": "not_charging"       // 充电状态详情
     *   },
     *   "system": {
     *     "firmware_version": "1.0.0",          // 固件版本
     *     "idf_version": "v5.5.2",              // ESP-IDF版本
     *     "chip_model": "ESP32-S3",             // 芯片型号
     *     "chip_cores": 2,                      // CPU核心数
     *     "free_heap": 123456,                  // 空闲堆内存 bytes
     *     "free_psram": 1234567,                // 空闲PSRAM bytes
     *     "total_psram": 8388608                // 总PSRAM bytes
     *   }
     * }
     */
    
    char *json_buffer = (char *)heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json_buffer) {
        ESP_LOGE(TAG, "Failed to allocate JSON buffer");
        return false;
    }
    
    int json_len = snprintf(json_buffer, 1024,
        "{"
        "\"device_id\":\"%s\","
        "\"timestamp\":\"%s\","
        "\"wakeup_reason\":\"%s\","
        "\"wifi\":{"
            "\"ssid\":\"%s\","
            "\"rssi\":%d,"
            "\"channel\":%d,"
            "\"ip\":\"%s\","
            "\"mac\":\"%s\""
        "},"
        "\"battery\":{"
            "\"voltage_mv\":%u,"
            "\"percent\":%u,"
            "\"is_charging\":%s,"
            "\"charge_status\":\"%s\""
        "},"
        "\"system\":{"
            "\"firmware_version\":\"%s\","
            "\"idf_version\":\"%s\","
            "\"chip_model\":\"ESP32-S3\","
            "\"chip_cores\":%d,"
            "\"free_heap\":%u,"
            "\"free_psram\":%u,"
            "\"total_psram\":%u"
        "}"
        "}",
        mac_str,
        time_str,
        wakeup_reason,
        (char *)ap_info.ssid,
        ap_info.rssi,
        ap_info.primary,
        ip_str,
        mac_str,
        batt.voltage_mv,
        batt.percent,
        batt.is_charging ? "true" : "false",
        batt.charge_status_str,
        app_desc->version,
        app_desc->idf_ver,
        chip_info.cores,
        (unsigned int)free_heap,
        (unsigned int)free_psram,
        (unsigned int)total_psram
    );
    
    ESP_LOGI(TAG, "Reporting device status: %s", json_buffer);
    
    // 发送HTTP POST请求
    esp_http_client_config_t config = {};
    config.url = status_url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 10000;
    config.crt_bundle_attach = esp_crt_bundle_attach;  // 支持HTTPS
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (api_key && strlen(api_key) > 0) {
        esp_http_client_set_header(client, "X-API-Key", api_key);
    }
    
    esp_http_client_set_post_field(client, json_buffer, json_len);
    
    esp_err_t err = esp_http_client_perform(client);
    
    bool success = false;
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Status report sent, HTTP status: %d", status_code);
        success = (status_code >= 200 && status_code < 300);
    } else {
        ESP_LOGE(TAG, "Failed to send status report: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    heap_caps_free(json_buffer);
    
    return success;
}

// 从HTTP下载图片并显示
static bool fetch_and_display_image(const char *url, const char *api_key)
{
    if (!s_is_wifi_connected) {
        ESP_LOGE(TAG, "WiFi not connected");
        return false;
    }

    // 分配内存用于存储图片（1.5MB以支持完整的BMP文件）
    uint8_t *image_buffer = (uint8_t *)heap_caps_malloc(1536 * 1024, MALLOC_CAP_SPIRAM);
    if (!image_buffer) {
        ESP_LOGE(TAG, "Failed to allocate image buffer");
        return false;
    }

    int total_len = 0;
    
    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = http_event_handler;
    config.buffer_size = 4096;
    config.timeout_ms = 30000;
    config.crt_bundle_attach = esp_crt_bundle_attach;  // 支持HTTPS
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // 添加API密钥头部
    if (api_key && strlen(api_key) > 0) {
        esp_http_client_set_header(client, "X-API-Key", api_key);
        ESP_LOGI(TAG, "API Key added to request header");
    }
    
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        heap_caps_free(image_buffer);
        esp_http_client_cleanup(client);
        return false;
    }
    
    int content_length = esp_http_client_fetch_headers(client);
    ESP_LOGI(TAG, "Content length: %d bytes (%.2f KB)", content_length, content_length / 1024.0);
    
    if (content_length > 1536 * 1024) {
        ESP_LOGE(TAG, "Image too large: %d bytes (%.2f MB)", content_length, content_length / (1024.0 * 1024.0));
        heap_caps_free(image_buffer);
        esp_http_client_cleanup(client);
        return false;
    }
    
    if (content_length <= 0) {
        ESP_LOGE(TAG, "Invalid content length: %d", content_length);
        heap_caps_free(image_buffer);
        esp_http_client_cleanup(client);
        return false;
    }
    
    // 读取数据
    int data_read = 0;
    while (total_len < content_length) {
        data_read = esp_http_client_read(client, (char *)(image_buffer + total_len), 4096);
        if (data_read <= 0) {
            ESP_LOGE(TAG, "Error read data");
            break;
        }
        total_len += data_read;
        ESP_LOGI(TAG, "Downloaded %d/%d bytes", total_len, content_length);
    }
    
    esp_http_client_cleanup(client);
    
    if (total_len != content_length) {
        ESP_LOGE(TAG, "Downloaded incomplete: %d/%d", total_len, content_length);
        heap_caps_free(image_buffer);
        return false;
    }
    
    ESP_LOGI(TAG, "Image downloaded successfully, displaying from memory...");
    
    // 直接从内存显示到墨水屏 - 使用EPD_MemoryBmpShakingColor处理BMP数据
    if (pdTRUE == xSemaphoreTake(epaper_gui_semapHandle, portMAX_DELAY)) {
        ePaperDisplay.EPD_Init();
        // BMP数据已经过抖动处理，直接从内存显示
        ePaperDisplay.EPD_MemoryBmpShakingColor(image_buffer, total_len, 0, 0);
        ePaperDisplay.EPD_Display();
        xSemaphoreGive(epaper_gui_semapHandle);
        
        ESP_LOGI(TAG, "Image displayed on e-paper");
    }
    
    heap_caps_free(image_buffer);
    return true;
}

// 初始化SNTP时间同步
static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "ntp1.aliyun.com");
    esp_sntp_init();
}

// 等待时间同步
static bool wait_for_time_sync(int timeout_sec)
{
    time_t now = 0;
    struct tm timeinfo = {};
    int retry = 0;
    const int retry_count = timeout_sec;

    while (timeinfo.tm_year < (2020 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    
    if (retry >= retry_count) {
        ESP_LOGE(TAG, "Failed to sync time");
        return false;
    }
    
    ESP_LOGI(TAG, "Time synchronized: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return true;
}

// 计算下次唤醒时间（秒）
static int64_t calculate_next_wakeup_time(uint8_t target_hour, uint8_t target_minute)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // 计算今天目标时间
    struct tm target_time = timeinfo;
    target_time.tm_hour = target_hour;
    target_time.tm_min = target_minute;
    target_time.tm_sec = 0;
    
    time_t target_timestamp = mktime(&target_time);
    
    // 如果目标时间已过，设置为明天
    if (target_timestamp <= now) {
        target_time.tm_mday += 1;
        target_timestamp = mktime(&target_time);
    }
    
    int64_t sleep_time_sec = (int64_t)(target_timestamp - now);
    
    ESP_LOGI(TAG, "Current time: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    ESP_LOGI(TAG, "Next wakeup: %04d-%02d-%02d %02d:%02d:%02d (in %lld seconds)",
             target_time.tm_year + 1900, target_time.tm_mon + 1, target_time.tm_mday,
             target_time.tm_hour, target_time.tm_min, target_time.tm_sec,
             sleep_time_sec);
    
    return sleep_time_sec;
}

// 主任务
static void photo_daily_task(void *arg)
{
    // 直接使用硬编码的配置参数
    photo_daily_config_t config = {
        .image_url = DEFAULT_IMAGE_URL,
        .status_url = DEFAULT_STATUS_URL,
        .api_key = DEFAULT_API_KEY,
        .wake_hour = DEFAULT_WAKE_HOUR,
        .wake_minute = DEFAULT_WAKE_MINUTE,
        .is_configured = true
    };
    
    ESP_LOGI(TAG, "Config: URL=%s, Status URL=%s, API Key=%s***, Wake time=%02d:%02d", 
             config.image_url,
             config.status_url,
             strlen(config.api_key) > 3 ? config.api_key : "***",
             config.wake_hour, config.wake_minute);
    
    // 等待WiFi连接
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);
    
    if (bits & CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected successfully");
        
        // 初始化时间同步
        initialize_sntp();
        
        // 等待时间同步
        if (wait_for_time_sync(30)) {
            // 设置时区
            setenv("TZ", "CST-8", 1);
            tzset();
            
            // 检查唤醒原因
            esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
            const char *wakeup_reason_str = get_wakeup_reason_string(wakeup_reason);
            
            // 先上报设备状态
            ESP_LOGI(TAG, "Reporting device status to server...");
            report_device_status(config.status_url, config.api_key, wakeup_reason_str);
            
            if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
                // 定时唤醒，拉取并显示图片
                ESP_LOGI(TAG, "Woke up by timer, fetching and displaying image...");
                fetch_and_display_image(config.image_url, config.api_key);
            } else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
                // GPIO按键唤醒，手动刷新图片
                uint64_t wakeup_pins = esp_sleep_get_ext1_wakeup_status();
                ESP_LOGI(TAG, "Woke up by button press (GPIO mask: 0x%llx), fetching image...", wakeup_pins);
                play_prompt_async(PROMPT_MANUAL_REFRESH);
                fetch_and_display_image(config.image_url, config.api_key);
            } else {
                // 首次启动
                ESP_LOGI(TAG, "First boot, fetching initial image...");
                // 首次启动也拉取图片
                fetch_and_display_image(config.image_url, config.api_key);
            }
            
            // 计算下次唤醒时间
            int64_t sleep_time_sec = calculate_next_wakeup_time(config.wake_hour, config.wake_minute);
            
            // 等待一段时间再进入深度睡眠（给用户时间查看）
            vTaskDelay(pdMS_TO_TICKS(5000));
            
            // 配置唤醒源：定时器 + GPIO按键
            const uint64_t ext_wakeup_pin_mask = 1ULL << ext_wakeup_pin_3;
            ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(ext_wakeup_pin_mask, ESP_EXT1_WAKEUP_ANY_LOW));
            ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(ext_wakeup_pin_3));
            ESP_ERROR_CHECK(rtc_gpio_pullup_en(ext_wakeup_pin_3));
            
            // 设置定时唤醒
            esp_sleep_enable_timer_wakeup(sleep_time_sec * 1000000ULL);  // 转换为微秒
            
            ESP_LOGI(TAG, "Entering deep sleep for %lld seconds...", sleep_time_sec);
            
            // 进入深度睡眠
            esp_deep_sleep_start();
        } else {
            ESP_LOGE(TAG, "Failed to sync time, retrying in 60 seconds...");
            play_prompt_async(PROMPT_WIFI_FAIL);
            vTaskDelay(pdMS_TO_TICKS(60000));
            esp_restart();
        }
    }
}

// Boot按键任务 - 用于重新配网
static void boot_button_task(void *arg)
{
    for (;;) {
        EventBits_t even = xEventGroupWaitBits(BootButtonGroups, GroupBit1, pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));
        if (even & GroupBit1) {
            ESP_LOGW(TAG, "Boot button long pressed, resetting WiFi config...");
            
            play_prompt_async(PROMPT_WIFI_RESET);
            
            // 清除WiFi配置
            esp_wifi_restore();
            
            // 重启设备重新配网
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        }
    }
}

// 模式初始化入口
void User_PhotoDaily_mode_app_init(void)
{
    ESP_LOGI(TAG, "=== Photo Daily Mode Initialized ===");
    ESP_LOGI(TAG, "Features:");
    ESP_LOGI(TAG, "  - ESPTouch SmartConfig for WiFi provisioning");
    ESP_LOGI(TAG, "  - Daily scheduled wakeup");
    ESP_LOGI(TAG, "  - HTTP image fetch and display");
    ESP_LOGI(TAG, "  - Deep sleep for power saving");
    
    // 初始化音频
    AudioPort = new CodecPort(I2cBus);
    
    // 创建提示音播放队列和专用任务 (opus_decode 需要大栈空间)
    s_prompt_queue = xQueueCreate(4, sizeof(PromptSound));
    xTaskCreate(prompt_sound_task, "prompt_sound", 16 * 1024, NULL, 4, NULL);
    
    // 初始化WiFi
    initialise_wifi();
    
    // 创建主任务
    xTaskCreate(photo_daily_task, "photo_daily_task", 8 * 1024, NULL, 5, NULL);
    
    // 创建Boot按键任务（用于重新配网）
    xTaskCreate(boot_button_task, "boot_button_task", 4 * 1024, NULL, 3, NULL);
}
