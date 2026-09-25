#include "telemetry_packet.h"

#include <string.h>

static void put_u32_le(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8);
    buffer[2] = (uint8_t)(value >> 16);
    buffer[3] = (uint8_t)(value >> 24);
}

static void put_float_le(uint8_t *buffer, float value)
{
    uint32_t representation = 0;
    memcpy(&representation, &value, sizeof(representation));
    put_u32_le(buffer, representation);
}

esp_err_t telemetry_packet_encode(uint8_t *packet, size_t packet_length, uint32_t device_id,
                                  uint32_t sequence, bool first_packet,
                                  const sen54_measurement_t *measurement)
{
    if (packet == NULL || measurement == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (packet_length != TELEMETRY_PACKET_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(packet, 0, packet_length);
    packet[0] = TELEMETRY_PACKET_VERSION;
    put_u32_le(&packet[1], device_id);
    put_u32_le(&packet[5], sequence);
    put_float_le(&packet[9], measurement->temperature);
    put_float_le(&packet[13], measurement->humidity);
    put_float_le(&packet[17], measurement->pm2_5);
    put_float_le(&packet[21], measurement->voc);
    packet[25] = first_packet ? TELEMETRY_STATUS_BOOT : 0;
    return ESP_OK;
}
