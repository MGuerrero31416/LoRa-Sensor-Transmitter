#pragma once

#include "esp_err.h"
#include <stdint.h>

#define OLED_ADDRESS 0x3C
#define OLED_I2C_FREQUENCY_HZ 400000

esp_err_t display_init(void);
esp_err_t display_show_tx(uint32_t device_id, uint32_t transmitted, float voc,
						  float temperature, float pm_2_5, float humidity);
esp_err_t display_show_sensor_unavailable(void);
