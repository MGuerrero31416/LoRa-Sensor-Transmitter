#include "display.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "u8g2.h"

#include <stdio.h>

#define TAG "display"
#define OLED_ADDRESS 0x3C
#define OLED_I2C_FREQUENCY_HZ 400000
#define OLED_SDA_GPIO 17
#define OLED_SCL_GPIO 18
#define OLED_RESET_GPIO 21
#define OLED_VEXT_GPIO 36

static i2c_master_bus_handle_t display_bus;
static i2c_master_dev_handle_t display_device;
static u8g2_t display;
static bool display_ready;

static uint8_t u8g2_i2c_callback(u8x8_t *u8x8, uint8_t message, uint8_t argument, void *data)
{
    static uint8_t buffer[132];
    static size_t buffer_length;
    (void)u8x8;

    switch (message) {
    case U8X8_MSG_BYTE_INIT: {
        const i2c_device_config_t device_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = OLED_ADDRESS,
            .scl_speed_hz = OLED_I2C_FREQUENCY_HZ,
        };
        return i2c_master_bus_add_device(display_bus, &device_config, &display_device) == ESP_OK;
    }
    case U8X8_MSG_BYTE_START_TRANSFER:
        buffer_length = 0;
        break;
    case U8X8_MSG_BYTE_SEND:
        if (buffer_length + argument > sizeof(buffer)) {
            return 0;
        }
        for (size_t index = 0; index < argument; index++) {
            buffer[buffer_length++] = ((const uint8_t *)data)[index];
        }
        break;
    case U8X8_MSG_BYTE_END_TRANSFER:
        return i2c_master_transmit(display_device, buffer, buffer_length, -1) == ESP_OK;
    default:
        break;
    }
    return 1;
}

static uint8_t u8g2_gpio_delay_callback(u8x8_t *u8x8, uint8_t message, uint8_t argument, void *data)
{
    (void)u8x8;
    (void)data;

    switch (message) {
    case U8X8_MSG_DELAY_MILLI:
        vTaskDelay(pdMS_TO_TICKS(argument));
        break;
    case U8X8_MSG_DELAY_10MICRO:
        esp_rom_delay_us(argument * 10);
        break;
    case U8X8_MSG_DELAY_100NANO:
        __asm__ __volatile__("nop");
        break;
    case U8X8_MSG_DELAY_I2C:
        esp_rom_delay_us(argument);
        break;
    case U8X8_MSG_GPIO_RESET:
        gpio_set_level(OLED_RESET_GPIO, argument);
        break;
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        break;
    default:
        break;
    }
    return 1;
}

static void display_send_buffer(void)
{
    u8g2_SendBuffer(&display);
}

esp_err_t display_init(void)
{
    const gpio_config_t output = {
        .pin_bit_mask = (1ULL << OLED_VEXT_GPIO) | (1ULL << OLED_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = OLED_SDA_GPIO,
        .scl_io_num = OLED_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&output), TAG, "OLED GPIO setup failed");
    gpio_set_level(OLED_VEXT_GPIO, 0);
    gpio_set_level(OLED_RESET_GPIO, 1);
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &display_bus), TAG, "I2C bus setup failed");

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&display, U8G2_R0, u8g2_i2c_callback, u8g2_gpio_delay_callback);
    u8g2_SetI2CAddress(&display, OLED_ADDRESS * 2);
    u8g2_InitDisplay(&display);
    u8g2_SetPowerSave(&display, 0);
    u8g2_SetContrast(&display, 140);
    display_ready = true;
    return ESP_OK;
}

esp_err_t display_show_tx(uint32_t device_id, uint32_t transmitted, float voc,
                          float temperature, float pm2_5, float humidity)
{
    if (!display_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    char line[24];
    char value[12];
    u8g2_ClearBuffer(&display);

    u8g2_SetFont(&display, u8g2_font_6x13B_tr);
    snprintf(line, sizeof(line), "TX ID: %lu Sent: %lu",
             (unsigned long)device_id, (unsigned long)transmitted);
    u8g2_DrawStr(&display, 0, 13, line);
    u8g2_DrawLine(&display, 0, 15, 127, 15);

    u8g2_SetFont(&display, u8g2_font_6x13_tr);
    u8g2_DrawStr(&display, 0, 31, "VOC:");
    snprintf(value, sizeof(value), "%.0f", (double)voc);
    u8g2_DrawStr(&display, 30, 31, value);
    u8g2_DrawStr(&display, 74, 31, "T:");
    snprintf(value, sizeof(value), "%.1f", (double)temperature);
    u8g2_DrawStr(&display, 90, 31, value);

    u8g2_DrawStr(&display, 0, 47, "PM2.5:");
    snprintf(value, sizeof(value), "%.0f", (double)pm2_5);
    u8g2_DrawStr(&display, 42, 47, value);
    u8g2_DrawStr(&display, 74, 47, "HR:");
    snprintf(value, sizeof(value), "%.0f", (double)humidity);
    u8g2_DrawStr(&display, 96, 47, value);
    
    display_send_buffer();
    return ESP_OK;

}

esp_err_t display_show_sensor_unavailable(void)
{
    if (!display_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    u8g2_ClearBuffer(&display);
    u8g2_SetFont(&display, u8g2_font_9x15B_tr);
    u8g2_DrawStr(&display, 0, 24, "SEN54 offline");
    u8g2_DrawStr(&display, 0, 50, "Retrying...");
    display_send_buffer();
    return ESP_OK;

}
