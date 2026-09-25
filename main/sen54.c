#include "sen54.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "sen54"
#define SEN54_SDA_GPIO 4
#define SEN54_SCL_GPIO 6
#define SEN54_CMD_START_MEASUREMENT 0x0021
#define SEN54_CMD_READ_MEASURED_VALUES 0x03C4
#define SEN54_WORD_COUNT 8

static i2c_master_bus_handle_t sen54_bus;
static i2c_master_dev_handle_t sen54_device;
static bool sen54_i2c_ready;
static bool sen54_warmed_up;
static bool sen54_probe_logged;
static bool sen54_measurement_warmed_up;

static void sen54_log_i2c_diagnostics(void)
{
    if (sen54_probe_logged) {
        return;
    }

    sen54_probe_logged = true;
    esp_err_t probe_error = i2c_master_probe(sen54_bus, SEN54_ADDRESS, pdMS_TO_TICKS(20));
    ESP_LOGE(TAG, "I2C probe 0x%02x: %s; SDA=%d SCL=%d", SEN54_ADDRESS, esp_err_to_name(probe_error),
             gpio_get_level(SEN54_SDA_GPIO), gpio_get_level(SEN54_SCL_GPIO));
}

static uint8_t sen54_crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xFF;
    for (size_t index = 0; index < length; index++) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) != 0 ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t sen54_read_words(uint16_t command, uint8_t *words, size_t word_count)
{
    uint8_t command_bytes[] = {(uint8_t)(command >> 8), (uint8_t)command};
    uint8_t response[SEN54_WORD_COUNT * 3] = {0};
    ESP_RETURN_ON_FALSE(word_count <= SEN54_WORD_COUNT, ESP_ERR_INVALID_SIZE, TAG, "response too large");

    for (int attempt = 0; attempt < 3; attempt++) {
        ESP_RETURN_ON_ERROR(i2c_master_transmit(sen54_device, command_bytes, sizeof(command_bytes), pdMS_TO_TICKS(100)),
                            TAG, "command 0x%04x failed", command);
        vTaskDelay(pdMS_TO_TICKS(20));
        esp_err_t response_error = i2c_master_receive(sen54_device, response, word_count * 3, pdMS_TO_TICKS(100));
        if (response_error == ESP_OK) {
            bool crc_valid = true;
            for (size_t index = 0; index < word_count; index++) {
                const uint8_t *word = &response[index * 3];
                if (sen54_crc8(word, 2) != word[2]) {
                    ESP_LOGW(TAG, "CRC failed for command 0x%04x, retrying", command);
                    crc_valid = false;
                    break;
                }
            }
            if (crc_valid) {
                for (size_t index = 0; index < word_count; index++) {
                    const uint8_t *word = &response[index * 3];
                    words[index * 2] = word[0];
                    words[index * 2 + 1] = word[1];
                }
                return ESP_OK;
            }
            if (attempt == 2) {
                ESP_LOGE(TAG, "response 0x%04x failed CRC after retries", command);
                return ESP_FAIL;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (attempt < 2) {
            ESP_LOGW(TAG, "response 0x%04x not ready (%s), retrying", command,
                     esp_err_to_name(response_error));
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            ESP_LOGE(TAG, "response 0x%04x failed after retries", command);
            return response_error;
        }
    }
    return ESP_FAIL;
}

static uint16_t sen54_decode_u16(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8) | data[1];
}

esp_err_t sen54_init(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = SEN54_SDA_GPIO,
        .scl_io_num = SEN54_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SEN54_ADDRESS,
        .scl_speed_hz = SEN54_I2C_FREQUENCY_HZ,
    };
    const uint8_t start_measurement[] = {
        (uint8_t)(SEN54_CMD_START_MEASUREMENT >> 8),
        (uint8_t)SEN54_CMD_START_MEASUREMENT,
    };

    if (!sen54_i2c_ready) {
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &sen54_bus), TAG, "I2C bus setup failed");
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(sen54_bus, &device_config, &sen54_device), TAG,
                            "I2C device setup failed");
        sen54_i2c_ready = true;
    }
    if (!sen54_warmed_up) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        sen54_warmed_up = true;
    }
    esp_err_t start_error = i2c_master_transmit(sen54_device, start_measurement, sizeof(start_measurement), pdMS_TO_TICKS(100));
    if (start_error != ESP_OK) {
        sen54_log_i2c_diagnostics();
        return start_error;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    sen54_measurement_warmed_up = false;
    ESP_LOGI(TAG, "SEN54 continuous measurement started");
    return ESP_OK;
}

esp_err_t sen54_read_measurement(sen54_measurement_t *measurement)
{
    uint8_t values[SEN54_WORD_COUNT * 2] = {0};
    ESP_RETURN_ON_FALSE(measurement != NULL, ESP_ERR_INVALID_ARG, TAG, "measurement is NULL");
    if (!sen54_measurement_warmed_up) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        sen54_measurement_warmed_up = true;
    }
    ESP_RETURN_ON_ERROR(sen54_read_words(SEN54_CMD_READ_MEASURED_VALUES, values, SEN54_WORD_COUNT), TAG,
                        "measurement read failed");

    const uint16_t raw_pm2_5 = sen54_decode_u16(&values[2]);
    const int16_t raw_humidity = (int16_t)sen54_decode_u16(&values[8]);
    const int16_t raw_temperature = (int16_t)sen54_decode_u16(&values[10]);
    const int16_t raw_voc = (int16_t)sen54_decode_u16(&values[12]);

    measurement->pm2_5 = raw_pm2_5 == UINT16_MAX ? -1.0f : (float)raw_pm2_5 / 10.0f;
    measurement->humidity = raw_humidity == INT16_MAX ? -1.0f : (float)raw_humidity / 100.0f;
    measurement->temperature = raw_temperature == INT16_MAX ? -1.0f : (float)raw_temperature / 200.0f;
    measurement->voc = raw_voc == INT16_MAX ? -1.0f : (float)raw_voc / 10.0f;
    return ESP_OK;
}