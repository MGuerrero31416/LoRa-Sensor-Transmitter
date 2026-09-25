#pragma once

#include "esp_err.h"

#define SEN54_ADDRESS 0x69
#define SEN54_I2C_FREQUENCY_HZ 100000

typedef struct {
    float temperature;
    float humidity;
    float pm2_5;
    float voc;
} sen54_measurement_t;

esp_err_t sen54_init(void);
esp_err_t sen54_read_measurement(sen54_measurement_t *measurement);