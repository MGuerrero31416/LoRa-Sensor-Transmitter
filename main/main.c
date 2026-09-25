#include <inttypes.h>

#include "display.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lora_sx1262.h"
#include "sen54.h"
#include "telemetry_packet.h"

const char FIRMWARE_REVISION[] = "1.2";
const char FIRMWARE_DATE[] = "2026_09_25";
#define DEVICE_ID 1
#define TRANSMIT_PERIOD_MS 10000

static const char *TAG = "lora_tx";

static esp_err_t display_show_sensor_unavailable_with_recovery(esp_err_t display_error)
{
    if (display_error != ESP_OK) {
        display_error = display_init();
    }
    if (display_error == ESP_OK) {
        display_error = display_show_sensor_unavailable();
    }
    return display_error;
}

/* Main sensor loop: initialize peripherals, build a telemetry packet, and periodically transmit it. */
void app_main(void)
{
    esp_err_t display_error = display_init();
    if (display_error != ESP_OK) {
        ESP_LOGE(TAG, "OLED unavailable: %s; continuing without display", esp_err_to_name(display_error));
    }
    esp_err_t sensor_error = sen54_init();
    if (sensor_error != ESP_OK) {
        ESP_LOGE(TAG, "SEN54 unavailable: %s; will retry", esp_err_to_name(sensor_error));
        display_error = display_show_sensor_unavailable_with_recovery(display_error);
    }
    ESP_ERROR_CHECK(lora_init());
    ESP_LOGI(TAG, "ready; transmitting every %d ms", TRANSMIT_PERIOD_MS);

    uint32_t sequence = 0;
    uint32_t transmitted = 0;
    bool first_packet = true;
    TickType_t next_transmission = xTaskGetTickCount();
    while (true) {
        sen54_measurement_t measurement = {0};
        if (sensor_error != ESP_OK) {
            sensor_error = sen54_init();
            if (sensor_error != ESP_OK) {
                ESP_LOGE(TAG, "SEN54 initialization retry failed: %s", esp_err_to_name(sensor_error));
                display_error = display_show_sensor_unavailable_with_recovery(display_error);
                vTaskDelayUntil(&next_transmission, pdMS_TO_TICKS(TRANSMIT_PERIOD_MS));
                continue;
            }
        }

        esp_err_t error = sen54_read_measurement(&measurement);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "SENSOR ERROR: %s", esp_err_to_name(error));
            if (error != ESP_ERR_NOT_FINISHED) {
                sensor_error = error;
            }
            display_error = display_show_sensor_unavailable_with_recovery(display_error);
            vTaskDelayUntil(&next_transmission, pdMS_TO_TICKS(TRANSMIT_PERIOD_MS));
            continue;
        }
        uint8_t payload[TELEMETRY_PACKET_LEN];
        error = telemetry_packet_encode(payload, sizeof(payload), DEVICE_ID, sequence, first_packet, &measurement);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "packet encoding failed: %s", esp_err_to_name(error));
            vTaskDelayUntil(&next_transmission, pdMS_TO_TICKS(TRANSMIT_PERIOD_MS));
            continue;
        }

        error = lora_send(payload, sizeof(payload));
        if (error == ESP_OK) {
            ESP_LOGI(TAG, "TX: seq=%" PRIu32 " device=%" PRIu32 " temp=%.2f rh=%.2f pm2.5=%.2f voc=%.2f status=%u%s len=%u",
                     sequence, (uint32_t)DEVICE_ID, measurement.temperature, measurement.humidity,
                     measurement.pm2_5, measurement.voc, payload[TELEMETRY_PACKET_LEN - 1],
                     (payload[TELEMETRY_PACKET_LEN - 1] & TELEMETRY_STATUS_BOOT) != 0 ? " BOOT" : "",
                     (unsigned)sizeof(payload));
            if (first_packet) {
                ESP_LOGI(TAG, "TX: BOOT flag set for first packet after transmitter boot");
                first_packet = false;
            }
            sequence++;
            transmitted++;
            if (display_error != ESP_OK) {
                display_error = display_init();
            }
            if (display_error == ESP_OK) {
                esp_err_t update_error = display_show_tx(DEVICE_ID, transmitted, measurement.voc,
                                                         measurement.temperature, measurement.pm2_5,
                                                         measurement.humidity);
                if (update_error != ESP_OK) {
                    ESP_LOGE(TAG, "OLED update failed: %s", esp_err_to_name(update_error));
                    display_error = display_init();
                    if (display_error != ESP_OK) {
                        ESP_LOGE(TAG, "OLED recovery failed: %s", esp_err_to_name(display_error));
                    }
                }
            }
        }
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "cycle failed: %s", esp_err_to_name(error));
        }
        vTaskDelayUntil(&next_transmission, pdMS_TO_TICKS(TRANSMIT_PERIOD_MS));
    }
}
