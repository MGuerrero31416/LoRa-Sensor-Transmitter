#pragma once

#include "esp_err.h"

typedef struct {
    float temperature;
    float humidity;
    float pm2_5;
    float voc;
} sen54_measurement_t;

esp_err_t sen54_init(void);
esp_err_t sen54_read_measurement(sen54_measurement_t *measurement);