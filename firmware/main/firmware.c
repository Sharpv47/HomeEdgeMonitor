#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_https_ota.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "sht4x.h"

#include "mqtt_client.h"
#include "mqtt_secrets.h"
#include "wifi_secrets.h"
#include "homeedge_config.h"

static const char *TAG = "home_edge";

#if HOMEEDGE_HAS_ENV_SENSOR
static sht4x_t dev;
#endif

static EventGroupHandle_t wifi_event_group;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool ota_in_progress = false;
static bool ota_pending_verify = false;

static volatile uint32_t wifi_reconnect_count = 0;
static volatile uint32_t mqtt_reconnect_count = 0;

#define WIFI_CONNECTED_BIT BIT0

#define MQTT_CONNECTED_BIT BIT1

static void mqtt_publish_discovery(void)
{
    #if HOMEEDGE_HAS_ENV_SENSOR

    const char *temperature_config =
        "{"
        "\"name\":\"Temperature\","
        "\"unique_id\":\"" HOMEEDGE_UNIQUE_ID_TEMPERATURE "\","
        "\"state_topic\":\"" HOMEEDGE_TOPIC_TEMPERATURE "\","
        "\"availability_topic\":\"" HOMEEDGE_TOPIC_STATUS "\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"device_class\":\"temperature\","
        "\"unit_of_measurement\":\"\\u00b0F\","
        "\"state_class\":\"measurement\","
        "\"device\":{"
            "\"identifiers\":[\"" HOMEEDGE_DEVICE_ID "\"],"
            "\"name\":\"" HOMEEDGE_DEVICE_NAME "\","
            "\"manufacturer\":\"" HOMEEDGE_MANUFACTURER "\","
            "\"model\":\"" HOMEEDGE_MODEL "\","
            "\"sw_version\":\"" HOMEEDGE_FIRMWARE_VERSION "\""
        "}"
        "}";

    const char *humidity_config =
        "{"
        "\"name\":\"Humidity\","
        "\"unique_id\":\"" HOMEEDGE_UNIQUE_ID_HUMIDITY "\","
        "\"state_topic\":\"" HOMEEDGE_TOPIC_HUMIDITY "\","
        "\"availability_topic\":\"" HOMEEDGE_TOPIC_STATUS "\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"device_class\":\"humidity\","
        "\"unit_of_measurement\":\"%\","
        "\"state_class\":\"measurement\","
        "\"device\":{"
            "\"identifiers\":[\"" HOMEEDGE_DEVICE_ID "\"],"
            "\"name\":\"" HOMEEDGE_DEVICE_NAME "\","
            "\"manufacturer\":\"" HOMEEDGE_MANUFACTURER "\","
            "\"model\":\"" HOMEEDGE_MODEL "\","
            "\"sw_version\":\"" HOMEEDGE_FIRMWARE_VERSION "\""
        "}"
        "}";
    #endif
    const char *rssi_config =
        "{"
        "\"name\":\"Wi-Fi Signal\","
        "\"unique_id\":\"" HOMEEDGE_UNIQUE_ID_RSSI "\","
        "\"state_topic\":\"" HOMEEDGE_TOPIC_RSSI "\","
        "\"availability_topic\":\"" HOMEEDGE_TOPIC_STATUS "\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"device_class\":\"signal_strength\","
        "\"unit_of_measurement\":\"dBm\","
        "\"state_class\":\"measurement\","
        "\"entity_category\":\"diagnostic\","
        "\"device\":{"
            "\"identifiers\":[\"" HOMEEDGE_DEVICE_ID "\"],"
            "\"name\":\"" HOMEEDGE_DEVICE_NAME "\","
            "\"manufacturer\":\"" HOMEEDGE_MANUFACTURER "\","
            "\"model\":\"" HOMEEDGE_MODEL "\","
            "\"sw_version\":\"" HOMEEDGE_FIRMWARE_VERSION "\""
        "}"
        "}";

    const char *uptime_config =
        "{"
        "\"name\":\"Uptime\","
        "\"unique_id\":\"" HOMEEDGE_UNIQUE_ID_UPTIME "\","
        "\"state_topic\":\"" HOMEEDGE_TOPIC_UPTIME "\","
        "\"availability_topic\":\"" HOMEEDGE_TOPIC_STATUS "\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"device_class\":\"duration\","
        "\"unit_of_measurement\":\"s\","
        "\"state_class\":\"total_increasing\","
        "\"entity_category\":\"diagnostic\","
        "\"device\":{"
            "\"identifiers\":[\"" HOMEEDGE_DEVICE_ID "\"],"
            "\"name\":\"" HOMEEDGE_DEVICE_NAME "\","
            "\"manufacturer\":\"" HOMEEDGE_MANUFACTURER "\","
            "\"model\":\"" HOMEEDGE_MODEL "\","
            "\"sw_version\":\"" HOMEEDGE_FIRMWARE_VERSION "\""
        "}"
        "}";

    const char *firmware_config =
        "{"
        "\"name\":\"Firmware Version\","
        "\"unique_id\":\"" HOMEEDGE_UNIQUE_ID_FIRMWARE "\","
        "\"state_topic\":\"" HOMEEDGE_TOPIC_FIRMWARE "\","
        "\"availability_topic\":\"" HOMEEDGE_TOPIC_STATUS "\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"entity_category\":\"diagnostic\","
        "\"icon\":\"mdi:chip\","
        "\"device\":{"
            "\"identifiers\":[\"" HOMEEDGE_DEVICE_ID "\"],"
            "\"name\":\"" HOMEEDGE_DEVICE_NAME "\","
            "\"manufacturer\":\"" HOMEEDGE_MANUFACTURER "\","
            "\"model\":\"" HOMEEDGE_MODEL "\","
            "\"sw_version\":\"" HOMEEDGE_FIRMWARE_VERSION "\""
        "}"
        "}";

    const char *free_heap_config =
        "{"
        "\"name\":\"Free Heap\","
        "\"unique_id\":\"" HOMEEDGE_UNIQUE_ID_FREE_HEAP "\","
        "\"state_topic\":\"" HOMEEDGE_TOPIC_FREE_HEAP "\","
        "\"availability_topic\":\"" HOMEEDGE_TOPIC_STATUS "\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"unit_of_measurement\":\"B\","
        "\"state_class\":\"measurement\","
        "\"entity_category\":\"diagnostic\","
        "\"icon\":\"mdi:memory\","
        "\"device\":{"
            "\"identifiers\":[\"" HOMEEDGE_DEVICE_ID "\"],"
            "\"name\":\"" HOMEEDGE_DEVICE_NAME "\","
            "\"manufacturer\":\"" HOMEEDGE_MANUFACTURER "\","
            "\"model\":\"" HOMEEDGE_MODEL "\","
            "\"sw_version\":\"" HOMEEDGE_FIRMWARE_VERSION "\""
        "}"
        "}";

    const char *min_free_heap_config =
        "{"
        "\"name\":\"Minimum Free Heap\","
        "\"unique_id\":\"" HOMEEDGE_UNIQUE_ID_MIN_FREE_HEAP "\","
        "\"state_topic\":\"" HOMEEDGE_TOPIC_MIN_FREE_HEAP "\","
        "\"availability_topic\":\"" HOMEEDGE_TOPIC_STATUS "\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"unit_of_measurement\":\"B\","
        "\"state_class\":\"measurement\","
        "\"entity_category\":\"diagnostic\","
        "\"icon\":\"mdi:memory\","
        "\"device\":{"
            "\"identifiers\":[\"" HOMEEDGE_DEVICE_ID "\"],"
            "\"name\":\"" HOMEEDGE_DEVICE_NAME "\","
            "\"manufacturer\":\"" HOMEEDGE_MANUFACTURER "\","
            "\"model\":\"" HOMEEDGE_MODEL "\","
            "\"sw_version\":\"" HOMEEDGE_FIRMWARE_VERSION "\""
        "}"
        "}";

    const char *wifi_reconnects_config =
        "{"
        "\"name\":\"Wi-Fi Reconnects\","
        "\"unique_id\":\"" HOMEEDGE_UNIQUE_ID_WIFI_RECONNECTS "\","
        "\"state_topic\":\"" HOMEEDGE_TOPIC_WIFI_RECONNECTS "\","
        "\"availability_topic\":\"" HOMEEDGE_TOPIC_STATUS "\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"state_class\":\"total_increasing\","
        "\"entity_category\":\"diagnostic\","
        "\"icon\":\"mdi:wifi-sync\","
        "\"device\":{"
            "\"identifiers\":[\"" HOMEEDGE_DEVICE_ID "\"],"
            "\"name\":\"" HOMEEDGE_DEVICE_NAME "\","
            "\"manufacturer\":\"" HOMEEDGE_MANUFACTURER "\","
            "\"model\":\"" HOMEEDGE_MODEL "\","
            "\"sw_version\":\"" HOMEEDGE_FIRMWARE_VERSION "\""
        "}"
        "}";

    const char *mqtt_reconnects_config =
        "{"
        "\"name\":\"MQTT Reconnects\","
        "\"unique_id\":\"" HOMEEDGE_UNIQUE_ID_MQTT_RECONNECTS "\","
        "\"state_topic\":\"" HOMEEDGE_TOPIC_MQTT_RECONNECTS "\","
        "\"availability_topic\":\"" HOMEEDGE_TOPIC_STATUS "\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"state_class\":\"total_increasing\","
        "\"entity_category\":\"diagnostic\","
        "\"icon\":\"mdi:connection\","
        "\"device\":{"
            "\"identifiers\":[\"" HOMEEDGE_DEVICE_ID "\"],"
            "\"name\":\"" HOMEEDGE_DEVICE_NAME "\","
            "\"manufacturer\":\"" HOMEEDGE_MANUFACTURER "\","
            "\"model\":\"" HOMEEDGE_MODEL "\","
            "\"sw_version\":\"" HOMEEDGE_FIRMWARE_VERSION "\""
        "}"
        "}";
        
    #if HOMEEDGE_HAS_ENV_SENSOR

    esp_mqtt_client_publish(
        mqtt_client,
        HOMEEDGE_DISCOVERY_TOPIC_TEMPERATURE,
        temperature_config,
        0,
        1,
        1);

    esp_mqtt_client_publish(
        mqtt_client,
        HOMEEDGE_DISCOVERY_TOPIC_HUMIDITY,
        humidity_config,
        0,
        1,
        1);

    #endif

    esp_mqtt_client_publish(
        mqtt_client,
        HOMEEDGE_DISCOVERY_TOPIC_RSSI,
        rssi_config,
        0,
        1,
        1);

    esp_mqtt_client_publish(
        mqtt_client,
        HOMEEDGE_DISCOVERY_TOPIC_UPTIME,
        uptime_config,
        0,
        1,
        1);

    esp_mqtt_client_publish(
        mqtt_client,
       HOMEEDGE_DISCOVERY_TOPIC_FIRMWARE,
        firmware_config,
        0,
        1,
        1);

    esp_mqtt_client_publish(
        mqtt_client,
        HOMEEDGE_DISCOVERY_TOPIC_FREE_HEAP,
        free_heap_config,
        0,
        1,
        1);

    esp_mqtt_client_publish(
        mqtt_client,
        HOMEEDGE_DISCOVERY_TOPIC_MIN_FREE_HEAP,
        min_free_heap_config,
        0,
        1,
        1);
    
    esp_mqtt_client_publish(
        mqtt_client,
        HOMEEDGE_DISCOVERY_TOPIC_WIFI_RECONNECTS,
        wifi_reconnects_config,
        0,
        1,
        1);

    esp_mqtt_client_publish(
        mqtt_client,
        HOMEEDGE_DISCOVERY_TOPIC_MQTT_RECONNECTS,
        mqtt_reconnects_config,
        0,
        1,
        1);    

    ESP_LOGI(TAG, "MQTT discovery published");
}

static void mqtt_publish_ota_status(const char *status)
{
    if (mqtt_client == NULL)
    {
        return;
    }

    if (xEventGroupGetBits(wifi_event_group) & MQTT_CONNECTED_BIT)
    {
        esp_mqtt_client_publish(
            mqtt_client,
            HOMEEDGE_TOPIC_OTA_STATUS,
            status,
            0,
            1,
            1);
    }
}

static void ota_task(void *pvParameter)
{
    ESP_LOGI(
        TAG,
        "Checking OTA update from %s",
        HOMEEDGE_OTA_URL);

    mqtt_publish_ota_status("checking");

    esp_http_client_config_t http_config = {
        .url = HOMEEDGE_OTA_URL,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t ota_handle = NULL;

    esp_err_t ret =
        esp_https_ota_begin(
            &ota_config,
            &ota_handle);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "OTA begin failed: %s",
            esp_err_to_name(ret));

        mqtt_publish_ota_status("failed");

        ota_in_progress = false;
        vTaskDelete(NULL);
        return;
    }

    esp_app_desc_t new_app_info = {0};

    ret =
        esp_https_ota_get_img_desc(
            ota_handle,
            &new_app_info);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to read OTA image description: %s",
            esp_err_to_name(ret));

        mqtt_publish_ota_status("failed");

        esp_https_ota_abort(ota_handle);

        ota_in_progress = false;
        vTaskDelete(NULL);
        return;
    }

    const esp_app_desc_t *running_app_info =
        esp_app_get_description();

    ESP_LOGI(
        TAG,
        "Running firmware version: %s",
        running_app_info->version);

    ESP_LOGI(
        TAG,
        "Available firmware version: %s",
        new_app_info.version);

    if (memcmp(
            new_app_info.version,
            running_app_info->version,
            sizeof(new_app_info.version)) == 0)
    {
        ESP_LOGI(
            TAG,
            "Firmware is already up to date");

        mqtt_publish_ota_status("up_to_date");

        esp_https_ota_abort(ota_handle);

        ota_in_progress = false;
        vTaskDelete(NULL);
        return;
    }

    mqtt_publish_ota_status("downloading");

    do
    {
        ret = esp_https_ota_perform(ota_handle);
    }
    while (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "OTA download failed: %s",
            esp_err_to_name(ret));

        mqtt_publish_ota_status("failed");

        esp_https_ota_abort(ota_handle);

        ota_in_progress = false;
        vTaskDelete(NULL);
        return;
    }

    if (!esp_https_ota_is_complete_data_received(ota_handle))
    {
        ESP_LOGE(
            TAG,
            "OTA image was not completely received");

        mqtt_publish_ota_status("failed");

        esp_https_ota_abort(ota_handle);

        ota_in_progress = false;
        vTaskDelete(NULL);
        return;
    }

    ret = esp_https_ota_finish(ota_handle);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "OTA finish failed: %s",
            esp_err_to_name(ret));

        mqtt_publish_ota_status("failed");

        ota_in_progress = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "OTA update successful");

    mqtt_publish_ota_status("success");

    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_restart();
}

static void ota_start(void)
{
    if (ota_in_progress)
    {
        ESP_LOGW(TAG, "OTA update already in progress");
        mqtt_publish_ota_status("busy");
        return;
    }

    ota_in_progress = true;

    BaseType_t result = xTaskCreate(
        ota_task,
        "homeedge_ota",
        8192,
        NULL,
        5,
        NULL);

    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create OTA task");

        ota_in_progress = false;
        mqtt_publish_ota_status("failed");
    }
}

static void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data)
{
    switch ((esp_mqtt_event_id_t)event_id)
    {
       case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");

            esp_mqtt_client_subscribe(
                mqtt_client,
                HOMEEDGE_TOPIC_OTA_COMMAND,
                1);

            xEventGroupSetBits(
                wifi_event_group,
                MQTT_CONNECTED_BIT);

            esp_mqtt_client_publish(
                mqtt_client,
                HOMEEDGE_TOPIC_STATUS,
                "online",
                0,
                1,
                1);

            esp_mqtt_client_publish(
                mqtt_client,
                HOMEEDGE_TOPIC_FIRMWARE,
                HOMEEDGE_FIRMWARE_VERSION,
                0,
                1,
                1);

            mqtt_publish_ota_status("ready");

            mqtt_publish_discovery();

            break;
        
                case MQTT_EVENT_DATA:
                {
                    esp_mqtt_event_handle_t event =
                        (esp_mqtt_event_handle_t)event_data;

                    if (event->topic_len ==
                            strlen(HOMEEDGE_TOPIC_OTA_COMMAND) &&
                        strncmp(
                            event->topic,
                            HOMEEDGE_TOPIC_OTA_COMMAND,
                            event->topic_len) == 0)
                    {
                        if (event->data_len == strlen("update") &&
                            strncmp(
                                event->data,
                                "update",
                                event->data_len) == 0)
                        {
                            ESP_LOGI(TAG, "OTA update command received");
                            ota_start();
                        }
                        else
                        {
                            ESP_LOGW(TAG, "Unknown OTA command received");
                        }
                    }

                    break;
                }

        case MQTT_EVENT_DISCONNECTED:
            mqtt_reconnect_count++;

            ESP_LOGW(TAG, "MQTT disconnected");

            xEventGroupClearBits(
                wifi_event_group,
                MQTT_CONNECTED_BIT);

            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;

        default:
            break;
    }
}

static void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
    .broker.address.uri = MQTT_BROKER_URI,

    .credentials.username = MQTT_USERNAME,
    .credentials.authentication.password = MQTT_PASSWORD,

    .session.keepalive = 20,

    .session.last_will.topic = HOMEEDGE_TOPIC_STATUS,
    .session.last_will.msg = "offline",
    .session.last_will.qos = 1,
    .session.last_will.retain = 1,
};

    mqtt_client =
        esp_mqtt_client_init(&mqtt_cfg);

    ESP_ERROR_CHECK(
        esp_mqtt_client_register_event(
            mqtt_client,
            ESP_EVENT_ANY_ID,
            mqtt_event_handler,
            NULL));

    ESP_ERROR_CHECK(
        esp_mqtt_client_start(mqtt_client));

    ESP_LOGI(TAG, "MQTT client started");
}

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "Wi-Fi starting...");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *event =
            (wifi_event_sta_disconnected_t *)event_data;

        wifi_reconnect_count++;

        ESP_LOGW(
            TAG,
            "Wi-Fi disconnected, reason: %d",
            event->reason);

        ESP_LOGW(TAG, "Reconnecting...");

        xEventGroupClearBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT);

        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG, "Wi-Fi connected");
        ESP_LOGI(
            TAG,
            "IP address: " IPSTR,
            IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT);
    }
}

static void ota_check_pending_verify(void)
{
    const esp_partition_t *running =
        esp_ota_get_running_partition();

    esp_ota_img_states_t ota_state;

    if (esp_ota_get_state_partition(
            running,
            &ota_state) == ESP_OK &&
        ota_state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        ota_pending_verify = true;

        ESP_LOGW(
            TAG,
            "OTA image is pending verification");
    }
}

static void wifi_init(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    else
    {
        ESP_ERROR_CHECK(ret);
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_event_group = xEventGroupCreate();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            wifi_event_handler,
            NULL));

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            wifi_event_handler,
            NULL));

    wifi_config_t wifi_config = {0};

    strlcpy(
        (char *)wifi_config.sta.ssid,
        WIFI_SSID,
        sizeof(wifi_config.sta.ssid));

    strlcpy(
        (char *)wifi_config.sta.password,
        WIFI_PASSWORD,
        sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");

    TickType_t wifi_wait =
        ota_pending_verify
            ? pdMS_TO_TICKS(60000)
            : portMAX_DELAY;

    EventBits_t wifi_bits =
        xEventGroupWaitBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT,
            pdFALSE,
            pdTRUE,
            wifi_wait);

    if (ota_pending_verify &&
        !(wifi_bits & WIFI_CONNECTED_BIT))
    {
        ESP_LOGE(
            TAG,
            "OTA validation failed: Wi-Fi did not connect");

        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

static void ota_validate_running_image(void)
{
    if (!ota_pending_verify)
    {
        return;
    }

    ESP_LOGI(
        TAG,
        "Validating newly installed OTA firmware");

    EventBits_t mqtt_bits =
        xEventGroupWaitBits(
            wifi_event_group,
            MQTT_CONNECTED_BIT,
            pdFALSE,
            pdTRUE,
            pdMS_TO_TICKS(30000));

    if (!(mqtt_bits & MQTT_CONNECTED_BIT))
    {
        ESP_LOGE(
            TAG,
            "OTA validation failed: MQTT did not connect");

        esp_ota_mark_app_invalid_rollback_and_reboot();
        return;
    }

    ESP_LOGI(
        TAG,
        "OTA validation successful");

    esp_err_t ret =
        esp_ota_mark_app_valid_cancel_rollback();

    if (ret == ESP_OK)
    {
        ESP_LOGI(
            TAG,
            "OTA firmware marked valid");

        ota_pending_verify = false;
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Failed to mark OTA firmware valid: %s",
            esp_err_to_name(ret));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Home Edge Monitor starting");
    ESP_LOGI(TAG, "Firmware version: %s", HOMEEDGE_FIRMWARE_VERSION);
    ESP_LOGI(TAG, "Device: %s", HOMEEDGE_DEVICE_NAME);
    ESP_LOGI(TAG, "Device ID: %s", HOMEEDGE_DEVICE_ID);
    ESP_LOGI(TAG, "Telemetry interval: %u ms", HOMEEDGE_TELEMETRY_INTERVAL_MS);

    ESP_LOGI(
        TAG,
        "Capabilities - Environment: %d, Washer: %d, Freezer: %d",
        HOMEEDGE_HAS_ENV_SENSOR,
        HOMEEDGE_HAS_WASHER_MONITOR,
        HOMEEDGE_HAS_FREEZER_MONITOR);

    ota_check_pending_verify();

    #if HOMEEDGE_HAS_ENV_SENSOR

        ESP_ERROR_CHECK(i2cdev_init());

        memset(&dev, 0, sizeof(sht4x_t));

        ESP_ERROR_CHECK(
            sht4x_init_desc(&dev, 0, 8, 9));

        ESP_ERROR_CHECK(sht4x_init(&dev));

        ESP_LOGI(TAG, "SHT40 initialized");

    #endif

    wifi_init();
    mqtt_start();

    while (1)
    {
    #if HOMEEDGE_HAS_ENV_SENSOR

        float temperature_c;
        float humidity;

        ESP_ERROR_CHECK(
            sht4x_measure(
                &dev,
                &temperature_c,
                &humidity));

        float temperature_f =
            (temperature_c * 9.0f / 5.0f) + 32.0f;

    #endif

        ota_validate_running_image();

        wifi_ap_record_t ap_info;
        bool rssi_available =
            (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);

        if (xEventGroupGetBits(wifi_event_group) & MQTT_CONNECTED_BIT)
        {
    #if HOMEEDGE_HAS_ENV_SENSOR

            char temperature_payload[16];
            char humidity_payload[16];

            snprintf(
                temperature_payload,
                sizeof(temperature_payload),
                "%.2f",
                temperature_f);

            snprintf(
                humidity_payload,
                sizeof(humidity_payload),
                "%.2f",
                humidity);

            esp_mqtt_client_publish(
                mqtt_client,
                HOMEEDGE_TOPIC_TEMPERATURE,
                temperature_payload,
                0,
                1,
                0);

            esp_mqtt_client_publish(
                mqtt_client,
                HOMEEDGE_TOPIC_HUMIDITY,
                humidity_payload,
                0,
                1,
                0);

    #endif

            char uptime_payload[24];

            int64_t uptime_seconds =
                esp_timer_get_time() / 1000000;

            snprintf(
                uptime_payload,
                sizeof(uptime_payload),
                "%lld",
                (long long)uptime_seconds);

                       esp_mqtt_client_publish(
                mqtt_client,
                HOMEEDGE_TOPIC_UPTIME,
                uptime_payload,
                0,
                1,
                0);

            char free_heap_payload[16];
            char min_free_heap_payload[16];

            snprintf(
                free_heap_payload,
                sizeof(free_heap_payload),
                "%" PRIu32,
                esp_get_free_heap_size());

            snprintf(
                min_free_heap_payload,
                sizeof(min_free_heap_payload),
                "%" PRIu32,
                esp_get_minimum_free_heap_size());

            esp_mqtt_client_publish(
                mqtt_client,
                HOMEEDGE_TOPIC_FREE_HEAP,
                free_heap_payload,
                0,
                1,
                0);

            esp_mqtt_client_publish(
                mqtt_client,
                HOMEEDGE_TOPIC_MIN_FREE_HEAP,
                min_free_heap_payload,
                0,
                1,
                0);
            
            char wifi_reconnect_payload[16];
            char mqtt_reconnect_payload[16];

            snprintf(
                wifi_reconnect_payload,
                sizeof(wifi_reconnect_payload),
                "%" PRIu32,
                (uint32_t)wifi_reconnect_count);

            snprintf(
                mqtt_reconnect_payload,
                sizeof(mqtt_reconnect_payload),
                "%" PRIu32,
                (uint32_t)mqtt_reconnect_count);

            esp_mqtt_client_publish(
                mqtt_client,
                HOMEEDGE_TOPIC_WIFI_RECONNECTS,
                wifi_reconnect_payload,
                0,
                1,
                0);

            esp_mqtt_client_publish(
                mqtt_client,
                HOMEEDGE_TOPIC_MQTT_RECONNECTS,
                mqtt_reconnect_payload,
                0,
                1,
                0);    

            if (rssi_available)
            {
                char rssi_payload[16];

                snprintf(
                    rssi_payload,
                    sizeof(rssi_payload),
                    "%d",
                    ap_info.rssi);

                esp_mqtt_client_publish(
                    mqtt_client,
                    HOMEEDGE_TOPIC_RSSI,
                    rssi_payload,
                    0,
                    1,
                    0);
            }
        }

    #if HOMEEDGE_HAS_ENV_SENSOR

        ESP_LOGI(
            TAG,
            "Temperature: %.2f C / %.2f F",
            temperature_c,
            temperature_f);

        ESP_LOGI(
            TAG,
            "Humidity: %.2f %%",
            humidity);

    #endif

        if (rssi_available)
        {
            ESP_LOGI(
                TAG,
                "Wi-Fi RSSI: %d dBm",
                ap_info.rssi);
        }

        vTaskDelay(pdMS_TO_TICKS(HOMEEDGE_TELEMETRY_INTERVAL_MS));
    }
}