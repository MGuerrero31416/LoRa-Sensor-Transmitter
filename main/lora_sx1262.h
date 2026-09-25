#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define LORA_PACKET_LEN 26
#define LORA_FREQUENCY_HZ 923000000UL
#define LORA_BANDWIDTH 0x04
#define LORA_SPREADING_FACTOR 10
#define LORA_CODING_RATE 0x01
#define LORA_PREAMBLE_LENGTH 12
#define LORA_TX_POWER_DBM 22

esp_err_t lora_init(void);
esp_err_t lora_send(const uint8_t *payload, size_t length);
