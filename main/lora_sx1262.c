#include "lora_sx1262.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Heltec WiFi LoRa 32 V4 / SX1262 wiring. */
#define LORA_NSS_GPIO 8
#define LORA_SCK_GPIO 9
#define LORA_MOSI_GPIO 10
#define LORA_MISO_GPIO 11
#define LORA_RESET_GPIO 12
#define LORA_BUSY_GPIO 13
#define LORA_DIO1_GPIO 14
#define LORA_PA_POWER_GPIO 7
#define LORA_PA_EN_GPIO 2
#define LORA_PA_TX_EN_GPIO 46
#define LORA_FREQUENCY_HZ 923000000UL
#define LORA_BANDWIDTH 0x04
#define LORA_SPREADING_FACTOR 10
#define LORA_CODING_RATE 0x01
#define LORA_PREAMBLE_LENGTH 12
#define LORA_TX_POWER_DBM 22
#define TRANSMIT_TIMEOUT_MS 5000

#define SX1262_CMD_SET_STANDBY 0x80
#define SX1262_CMD_SET_PACKET_TYPE 0x8A
#define SX1262_CMD_SET_RF_FREQUENCY 0x86
#define SX1262_CMD_SET_BUFFER_BASE 0x8F
#define SX1262_CMD_SET_MODULATION 0x8B
#define SX1262_CMD_SET_PACKET_PARAMS 0x8C
#define SX1262_CMD_SET_TX_PARAMS 0x8E
#define SX1262_CMD_SET_DIO_IRQ 0x08
#define SX1262_CMD_CLEAR_IRQ 0x02
#define SX1262_CMD_GET_IRQ_STATUS 0x12
#define SX1262_CMD_WRITE_BUFFER 0x0E
#define SX1262_CMD_SET_TX 0x83
#define SX1262_CMD_SET_REGULATOR 0x96
#define SX1262_CMD_SET_TCXO 0x97
#define SX1262_CMD_CALIBRATE 0x89
#define SX1262_CMD_SET_RF_SWITCH 0x9D
#define SX1262_CMD_SET_PA_CONFIG 0x95
#define SX1262_CMD_CALIBRATE_IMAGE 0x98
#define SX1262_CMD_GET_STATUS 0xC0

#define SX1262_IRQ_TX_DONE 0x0001

static const char *TAG = "lora_tx";
static spi_device_handle_t lora_spi;
static volatile TaskHandle_t lora_tx_waiter;

static void lora_dio1_isr(void *arg)
{
    (void)arg;
    BaseType_t higher_priority_task_woken = pdFALSE;
    TaskHandle_t waiter = lora_tx_waiter;
    if (waiter != NULL) {
        vTaskNotifyGiveFromISR(waiter, &higher_priority_task_woken);
    }
    if (higher_priority_task_woken != pdFALSE) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t lora_wait_ready(TickType_t timeout)
{
    TickType_t start = xTaskGetTickCount();
    while (gpio_get_level(LORA_BUSY_GPIO) != 0) {
        if ((xTaskGetTickCount() - start) > timeout) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_OK;
}

static esp_err_t lora_transfer(const uint8_t *tx_data, uint8_t *rx_data, size_t length)
{
    spi_transaction_t transaction = {
        .length = length * 8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };
    return spi_device_transmit(lora_spi, &transaction);
}

static esp_err_t lora_command(uint8_t command, const uint8_t *arguments, size_t argument_length)
{
    uint8_t buffer[64] = {0};
    ESP_RETURN_ON_FALSE(argument_length + 1 <= sizeof(buffer), ESP_ERR_INVALID_SIZE, TAG, "command too long");
    buffer[0] = command;
    if (argument_length > 0) {
        memcpy(&buffer[1], arguments, argument_length);
    }
    ESP_RETURN_ON_ERROR(lora_wait_ready(pdMS_TO_TICKS(100)), TAG, "radio busy");
    return lora_transfer(buffer, NULL, argument_length + 1);
}

static esp_err_t lora_read_command(uint8_t command, uint8_t *response, size_t response_length)
{
    uint8_t buffer[64] = {0};
    ESP_RETURN_ON_FALSE(response_length + 2 <= sizeof(buffer), ESP_ERR_INVALID_SIZE, TAG, "read too long");
    buffer[0] = command;
    ESP_RETURN_ON_ERROR(lora_wait_ready(pdMS_TO_TICKS(100)), TAG, "radio busy");
    ESP_RETURN_ON_ERROR(lora_transfer(buffer, buffer, response_length + 2), TAG, "SPI read failed");
    memcpy(response, &buffer[2], response_length);
    return ESP_OK;
}

static void lora_fem_set_rx(void)
{
    gpio_set_level(LORA_PA_POWER_GPIO, 1);
    gpio_set_level(LORA_PA_EN_GPIO, 1);
    gpio_set_level(LORA_PA_TX_EN_GPIO, 0);
}

static void lora_fem_set_tx(void)
{
    gpio_set_level(LORA_PA_POWER_GPIO, 1);
    gpio_set_level(LORA_PA_EN_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(LORA_PA_TX_EN_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(2));
}

esp_err_t lora_init(void)
{
    gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << LORA_RESET_GPIO) | (1ULL << LORA_PA_POWER_GPIO) |
                        (1ULL << LORA_PA_EN_GPIO) | (1ULL << LORA_PA_TX_EN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), TAG, "output GPIO setup failed");

    gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << LORA_BUSY_GPIO),
        .mode = GPIO_MODE_INPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&inputs), TAG, "input GPIO setup failed");
    gpio_config_t dio1 = {
        .pin_bit_mask = (1ULL << LORA_DIO1_GPIO),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&dio1), TAG, "DIO1 GPIO setup failed");
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), TAG, "GPIO ISR setup failed");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(LORA_DIO1_GPIO, lora_dio1_isr, NULL), TAG,
                        "DIO1 ISR registration failed");
    lora_fem_set_rx();

    spi_bus_config_t bus = {
        .mosi_io_num = LORA_MOSI_GPIO,
        .miso_io_num = LORA_MISO_GPIO,
        .sclk_io_num = LORA_SCK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "SPI setup failed");

    spi_device_interface_config_t device = {
        .clock_speed_hz = 8000000,
        .mode = 0,
        .spics_io_num = LORA_NSS_GPIO,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &device, &lora_spi), TAG, "SPI device setup failed");

    gpio_set_level(LORA_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(LORA_RESET_GPIO, 1);
    ESP_RETURN_ON_ERROR(lora_wait_ready(pdMS_TO_TICKS(100)), TAG, "radio reset failed");

    const uint8_t standby[] = {0x00};
    const uint8_t regulator[] = {0x01};
    const uint8_t tcxo[] = {0x07, 0x00, 0x01, 0x40};
    const uint8_t calibration[] = {0x7F};
    const uint8_t rf_switch[] = {0x01};
    const uint8_t pa_config[] = {0x04, 0x07, 0x00, 0x01};
    const uint8_t packet_type[] = {0x01};
    const uint8_t buffer_base[] = {0x00, 0x00};
    const uint8_t modulation[] = {LORA_SPREADING_FACTOR, LORA_BANDWIDTH, LORA_CODING_RATE, 0x00};
    const uint8_t packet_params[] = {0x00, LORA_PREAMBLE_LENGTH, 0x00, LORA_PACKET_LEN, 0x01, 0x00};
    const uint8_t tx_params[] = {LORA_TX_POWER_DBM, 0x04};
    const uint32_t frequency = (uint32_t)(((uint64_t)LORA_FREQUENCY_HZ << 25) / 32000000ULL);
    const uint8_t rf_frequency[] = {(uint8_t)(frequency >> 24), (uint8_t)(frequency >> 16),
                                    (uint8_t)(frequency >> 8), (uint8_t)frequency};
    const uint8_t image_calibration[] = {0xE1, 0xE9};
    const uint8_t irq[] = {0x02, 0x01, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00};

    ESP_RETURN_ON_ERROR(lora_command(SX1262_CMD_SET_STANDBY, standby, sizeof(standby)), TAG, "standby failed");
    ESP_RETURN_ON_ERROR(lora_command(SX1262_CMD_SET_REGULATOR, regulator, sizeof(regulator)), TAG, "regulator setup failed");
    ESP_RETURN_ON_ERROR(lora_command(SX1262_CMD_SET_TCXO, tcxo, sizeof(tcxo)), TAG, "TCXO setup failed");
    ESP_RETURN_ON_ERROR(lora_command(SX1262_CMD_CALIBRATE, calibration, sizeof(calibration)), TAG, "calibration failed");
    ESP_RETURN_ON_ERROR(lora_command(SX1262_CMD_SET_RF_SWITCH, rf_switch, sizeof(rf_switch)), TAG, "RF switch setup failed");
    ESP_RETURN_ON_ERROR(lora_command(SX1262_CMD_SET_PA_CONFIG, pa_config, sizeof(pa_config)), TAG, "PA setup failed");
    ESP_RETURN_ON_ERROR(lora_command(SX1262_CMD_SET_PACKET_TYPE, packet_type, sizeof(packet_type)), TAG, "packet type failed");
    ESP_RETURN_ON_ERROR(lora_command(SX1262_CMD_CALIBRATE_IMAGE, image_calibration, sizeof(image_calibration)), TAG, "image calibration failed");
    ESP_RETURN_ON_ERROR(lora_command(SX1262_CMD_SET_RF_FREQUENCY, rf_frequency, sizeof(rf_frequency)), TAG, "frequency setup failed");
    ESP_RETURN_ON_ERROR(lora_command(SX1262_CMD_SET_BUFFER_BASE, buffer_base, sizeof(buffer_base)), TAG, "buffer setup failed");
    ESP_RETURN_ON_ERROR(lora_command(SX1262_CMD_SET_MODULATION, modulation, sizeof(modulation)), TAG, "modulation setup failed");
    ESP_RETURN_ON_ERROR(lora_command(SX1262_CMD_SET_PACKET_PARAMS, packet_params, sizeof(packet_params)), TAG, "packet setup failed");
    ESP_RETURN_ON_ERROR(lora_command(SX1262_CMD_SET_TX_PARAMS, tx_params, sizeof(tx_params)), TAG, "TX power setup failed");
    ESP_RETURN_ON_ERROR(lora_command(SX1262_CMD_SET_DIO_IRQ, irq, sizeof(irq)), TAG, "IRQ setup failed");

    uint8_t status = 0;
    ESP_RETURN_ON_ERROR(lora_read_command(SX1262_CMD_GET_STATUS, &status, 1), TAG, "status read failed");
    ESP_LOGI(TAG, "SX1262 initialized, status=0x%02x", status);
    return ESP_OK;
}

esp_err_t lora_send(const uint8_t *payload, size_t length)
{
    ESP_RETURN_ON_FALSE(length == LORA_PACKET_LEN, ESP_ERR_INVALID_SIZE, TAG, "packet must be 26 bytes");
    const uint8_t buffer[] = {0x00, 0x00};
    const uint8_t packet[] = {0x00, LORA_PREAMBLE_LENGTH, 0x00, (uint8_t)length, 0x01, 0x00};
    /* SX1262 TX timeout units are 15.625 us; 320000 units is 5 seconds. */
    const uint8_t tx_timeout[] = {0x04, 0xE2, 0x00};
    const uint8_t clear_irq[] = {0xFF, 0xFF};
    uint8_t write_buffer[LORA_PACKET_LEN + 1] = {0};
    write_buffer[0] = 0x00;
    memcpy(&write_buffer[1], payload, LORA_PACKET_LEN);

    esp_err_t ret = ESP_OK;
    esp_err_t result = ESP_OK;
    lora_fem_set_tx();
    if ((ret = lora_command(SX1262_CMD_SET_BUFFER_BASE, buffer, sizeof(buffer))) != ESP_OK) {
        ESP_LOGE(TAG, "buffer reset failed: %s", esp_err_to_name(ret));
        goto tx_cleanup;
    }
    if ((ret = lora_command(SX1262_CMD_SET_PACKET_PARAMS, packet, sizeof(packet))) != ESP_OK) {
        ESP_LOGE(TAG, "packet length setup failed: %s", esp_err_to_name(ret));
        goto tx_cleanup;
    }
    if ((ret = lora_command(SX1262_CMD_WRITE_BUFFER, write_buffer, LORA_PACKET_LEN + 1)) != ESP_OK) {
        ESP_LOGE(TAG, "payload write failed: %s", esp_err_to_name(ret));
        goto tx_cleanup;
    }
    if ((ret = lora_command(SX1262_CMD_CLEAR_IRQ, clear_irq, sizeof(clear_irq))) != ESP_OK) {
        ESP_LOGE(TAG, "IRQ clear failed: %s", esp_err_to_name(ret));
        goto tx_cleanup;
    }
    if ((ret = lora_command(SX1262_CMD_SET_TX, tx_timeout, sizeof(tx_timeout))) != ESP_OK) {
        ESP_LOGE(TAG, "transmit start failed: %s", esp_err_to_name(ret));
        goto tx_cleanup;
    }

    uint8_t irq_status[2] = {0};
    result = ESP_ERR_TIMEOUT;
    lora_tx_waiter = xTaskGetCurrentTaskHandle();
    ulTaskNotifyTake(pdTRUE, 0);
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TRANSMIT_TIMEOUT_MS)) != 0) {
        result = lora_read_command(SX1262_CMD_GET_IRQ_STATUS, irq_status, sizeof(irq_status));
    }

tx_cleanup:
    lora_tx_waiter = NULL;
    lora_fem_set_rx();
    if (ret != ESP_OK) {
        return ret;
    }
    if (result != ESP_OK) {
        return result;
    }
    const uint16_t flags = ((uint16_t)irq_status[0] << 8) | irq_status[1];
    if ((flags & SX1262_IRQ_TX_DONE) == 0) {
        ESP_LOGE(TAG, "TX did not complete: IRQ=0x%04x DIO1=%d", flags, gpio_get_level(LORA_DIO1_GPIO));
    }
    ESP_RETURN_ON_ERROR(lora_command(SX1262_CMD_CLEAR_IRQ, clear_irq, sizeof(clear_irq)), TAG, "IRQ cleanup failed");
    return (flags & SX1262_IRQ_TX_DONE) != 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}
