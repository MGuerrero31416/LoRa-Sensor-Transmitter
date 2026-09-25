#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sen54.h"

#define TELEMETRY_PACKET_LEN 26
#define TELEMETRY_PACKET_VERSION 1
#define TELEMETRY_STATUS_BOOT 0x01

_Static_assert(TELEMETRY_PACKET_LEN == 26, "unexpected telemetry packet length");

esp_err_t telemetry_packet_encode(uint8_t *packet, size_t packet_length, uint32_t device_id,
                                  uint32_t sequence, bool first_packet,
                                  const sen54_measurement_t *measurement);
