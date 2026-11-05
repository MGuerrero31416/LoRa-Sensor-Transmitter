#include "modbus_sensor.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_modbus_master.h"

#define TAG "modbus_sensor"

/* Modbus RTU and RS-485 settings; adjust these to match the analog sensor. */
#define MODBUS_UART_PORT UART_NUM_1
#define MODBUS_UART_TX_GPIO 4
#define MODBUS_UART_RX_GPIO 6
#define MODBUS_UART_RTS_GPIO 5
#define MODBUS_BAUD_RATE 9600
#define MODBUS_PARITY UART_PARITY_DISABLE
#define MODBUS_STOP_BITS UART_STOP_BITS_1
#define MODBUS_SLAVE_ADDRESS 1
/* Keep the register type and function code paired when changing to holding registers. */
#define MODBUS_REGISTER_TYPE MB_PARAM_INPUT
#define MODBUS_FUNCTION_CODE 0x04
#define MODBUS_REGISTER_ADDRESS 0
#define MODBUS_REGISTER_COUNT 1
#define MODBUS_RESPONSE_TIMEOUT_MS 1000
#define MODBUS_VALUE_SCALE 1.0f
#define MODBUS_VALUE_OFFSET 0.0f

static void *master;
static bool master_ready;

static mb_parameter_descriptor_t analog_register = {
    .cid = 0,
    .param_key = "analog_input",
    .param_units = "",
    .mb_slave_addr = MODBUS_SLAVE_ADDRESS,
    .mb_param_type = MODBUS_REGISTER_TYPE,
    .mb_reg_start = MODBUS_REGISTER_ADDRESS,
    .mb_size = MODBUS_REGISTER_COUNT,
    .param_type = PARAM_TYPE_U16,
    .param_size = sizeof(uint16_t),
    .access = PAR_PERMS_READ,
};

static esp_err_t modbus_sensor_startup_failed(esp_err_t error)
{
    ESP_LOGE(TAG, "Modbus master startup failed: %s", esp_err_to_name(error));
    if (master != NULL) {
        esp_err_t delete_error = mbc_master_delete(master);
        if (delete_error != ESP_OK) {
            ESP_LOGE(TAG, "Modbus master cleanup failed: %s", esp_err_to_name(delete_error));
        }
        master = NULL;
    }
    return error;
}

esp_err_t modbus_sensor_init(void)
{
    if (master_ready) {
        return ESP_OK;
    }

    mb_communication_info_t communication = {
        .ser_opts.port = MODBUS_UART_PORT,
        .ser_opts.mode = MB_RTU,
        .ser_opts.baudrate = MODBUS_BAUD_RATE,
        .ser_opts.parity = MODBUS_PARITY,
        .ser_opts.uid = 0,
        .ser_opts.response_tout_ms = MODBUS_RESPONSE_TIMEOUT_MS,
        .ser_opts.data_bits = UART_DATA_8_BITS,
        .ser_opts.stop_bits = MODBUS_STOP_BITS,
    };

    esp_err_t error = mbc_master_create_serial(&communication, &master);
    if (error != ESP_OK) {
        return modbus_sensor_startup_failed(error);
    }

    error = mbc_master_set_descriptor(master, &analog_register, 1);
    if (error == ESP_OK) {
        error = uart_set_pin(MODBUS_UART_PORT, MODBUS_UART_TX_GPIO, MODBUS_UART_RX_GPIO,
                             MODBUS_UART_RTS_GPIO, UART_PIN_NO_CHANGE);
    }
    if (error == ESP_OK) {
        error = uart_set_mode(MODBUS_UART_PORT, UART_MODE_RS485_HALF_DUPLEX);
    }
    if (error == ESP_OK) {
        error = mbc_master_start(master);
    }
    if (error != ESP_OK) {
        return modbus_sensor_startup_failed(error);
    }

    master_ready = true;
    ESP_LOGI(TAG, "RTU master UART%d TX=%d RX=%d RTS=%d, %u baud, unit=%u, FC%u address=%u",
             (int)MODBUS_UART_PORT, MODBUS_UART_TX_GPIO, MODBUS_UART_RX_GPIO,
             MODBUS_UART_RTS_GPIO, (unsigned)MODBUS_BAUD_RATE, (unsigned)MODBUS_SLAVE_ADDRESS,
             (unsigned)MODBUS_FUNCTION_CODE, (unsigned)MODBUS_REGISTER_ADDRESS);
    return ESP_OK;
}

esp_err_t modbus_sensor_read_measurement(modbus_measurement_t *measurement)
{
    ESP_RETURN_ON_FALSE(measurement != NULL, ESP_ERR_INVALID_ARG, TAG, "measurement is NULL");
    ESP_RETURN_ON_FALSE(master_ready, ESP_ERR_INVALID_STATE, TAG, "Modbus master is not initialized");

    mb_param_request_t request = {
        .slave_addr = MODBUS_SLAVE_ADDRESS,
        .command = MODBUS_FUNCTION_CODE,
        .reg_start = MODBUS_REGISTER_ADDRESS,
        .reg_size = MODBUS_REGISTER_COUNT,
    };
    uint16_t register_value = 0;
    ESP_RETURN_ON_ERROR(mbc_master_send_request(master, &request, &register_value), TAG,
                        "analog register read failed");

    measurement->value = (float)register_value * MODBUS_VALUE_SCALE + MODBUS_VALUE_OFFSET;
    return ESP_OK;
}
