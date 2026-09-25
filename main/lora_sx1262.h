#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define LORA_PACKET_LEN 26

esp_err_t lora_init(void);
esp_err_t lora_send(const uint8_t *payload, size_t length);
