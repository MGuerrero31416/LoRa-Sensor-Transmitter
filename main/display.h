#pragma once

#include "esp_err.h"
#include <stdint.h>

esp_err_t display_init(void);
esp_err_t display_show_tx(uint32_t transmitted, float voc, float pm2_5);
