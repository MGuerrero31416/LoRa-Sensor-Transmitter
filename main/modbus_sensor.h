#pragma once

#include "esp_err.h"

typedef struct {
    float value;
} modbus_measurement_t;

esp_err_t modbus_sensor_init(void);
esp_err_t modbus_sensor_read_measurement(modbus_measurement_t *measurement);
