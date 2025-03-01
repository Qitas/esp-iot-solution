// Copyright 2021-2022 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/ip.h"
#include "iot_board.h"

/* The component use WiFi configuration that you can set via 'menuconfig' */
#define BOARD_WIFI_SSID      CONFIG_BOARD_WIFI_SSID
#define BOARD_WIFI_PASS      CONFIG_BOARD_WIFI_PASSWORD
#define BOARD_MAXIMUM_RETRY  CONFIG_BOARD_MAXIMUM_RETRY
#define BOARD_WIFI_AP_SSID   CONFIG_BOARD_WIFI_AP_SSID
#define BOARD_WIFI_AP_PASS   CONFIG_BOARD_WIFI_AP_PASSWORD
#define BOARD_MAX_STA_CONN       CONFIG_BOARD_MAX_STA_CONN
#define BOARD_IP_ADDR            CONFIG_BOARD_SERVER_IP
#define BOARD_WIFI_AP_CHANNEL CONFIG_BOARD_WIFI_AP_CHANNEL

static const char *TAG = "Board_Common_wifi";

static int s_retry_num = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base != WIFI_EVENT) {
        return;
    }

    switch (event_id) {
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
            ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                     MAC2STR(event->mac), event->aid);
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
            ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                     MAC2STR(event->mac), event->aid);
            break;
        }

        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_retry_num < BOARD_MAXIMUM_RETRY) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "retry to connect to the AP");
            } else {
                ESP_LOGI(TAG, "giveup retry");
            }

            ESP_LOGI(TAG, "connect to the AP fail");
            break;

        case IP_EVENT_STA_GOT_IP: {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
            ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
            s_retry_num = 0;
        }
        break;

        default:
            break;
    }
}

static void wifi_init_softap(esp_netif_t *wifi_netif)
{
    if (strcmp(BOARD_IP_ADDR, "192.168.4.1")) {
        int a, b, c, d;
        sscanf(BOARD_IP_ADDR, "%d.%d.%d.%d", &a, &b, &c, &d);
        esp_netif_ip_info_t ip_info;
        IP4_ADDR(&ip_info.ip, a, b, c, d);
        IP4_ADDR(&ip_info.gw, a, b, c, d);
        IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
        ESP_ERROR_CHECK(esp_netif_dhcps_stop(wifi_netif));
        ESP_ERROR_CHECK(esp_netif_set_ip_info(wifi_netif, &ip_info));
        ESP_ERROR_CHECK(esp_netif_dhcps_start(wifi_netif));
    }

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    snprintf((char *)wifi_config.ap.ssid, 32, "%s", BOARD_WIFI_AP_SSID);
    wifi_config.ap.ssid_len = strlen((char *)wifi_config.ap.ssid);
    snprintf((char *)wifi_config.ap.password, 64, "%s", BOARD_WIFI_AP_PASS);
    wifi_config.ap.max_connection = BOARD_MAX_STA_CONN;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.ap.channel = BOARD_WIFI_AP_CHANNEL;

    if (strlen(BOARD_WIFI_AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             BOARD_WIFI_AP_SSID, BOARD_WIFI_AP_PASS, BOARD_WIFI_AP_CHANNEL);
}

static void wifi_init_sta(esp_netif_t *wifi_netif)
{
    (void) wifi_netif;
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    snprintf((char *)wifi_config.sta.ssid, 32, "%s", BOARD_WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, 64, "%s", BOARD_WIFI_PASS);

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s",
             BOARD_WIFI_SSID, BOARD_WIFI_PASS);
}

ATTR_WEAK esp_err_t iot_board_wifi_init()
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_netif_t *wifi_netif = NULL;

    if (strlen(BOARD_WIFI_AP_SSID) && strlen(BOARD_WIFI_SSID)) {
        mode = WIFI_MODE_APSTA;
    } else if (strlen(BOARD_WIFI_AP_SSID)) {
        mode = WIFI_MODE_AP;
    } else if (strlen(BOARD_WIFI_SSID)) {
        mode = WIFI_MODE_STA;
    }

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    if (mode == WIFI_MODE_NULL) {
        ESP_LOGW(TAG, "Neither AP or STA have been configured. WiFi will be off.");
        return ESP_ERR_NVS_INVALID_STATE;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &wifi_event_handler,
                    NULL,
                    NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));

    if (mode & WIFI_MODE_AP) {
        wifi_netif = esp_netif_create_default_wifi_ap();
        wifi_init_softap(wifi_netif);
        ESP_LOGI(TAG, "wifi ap initialized");
    }

    if (mode & WIFI_MODE_STA) {
        wifi_netif = esp_netif_create_default_wifi_sta();
        wifi_init_sta(wifi_netif);
        ESP_LOGI(TAG, "wifi sta initialized");
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    return ESP_OK;
}
