# ESP32 LoRa Room Sensor Transmitter — SEN54

Wireless room sensor transmitter based on the **Heltec WiFi LoRa 32 V4 (SX1262)** and **Sensirion SEN54**.

The device reads the SEN54 environmental sensor and transmits the sensor data via LoRa to a dedicated ESP32 LoRa Gateway.

The gateway receives the LoRa packets and exposes the sensor values through **BACnet MS/TP** for integration with building automation systems such as Johnson Controls Metasys.

## System Architecture

SEN54
  │
  ▼
ESP32-S3 + SX1262
  │
  │ LoRa
  ▼
ESP32-S3 + SX1262
LoRa / BACnet MS/TP Gateway
  │
  │ RS-485
  ▼
BACnet MS/TP
  │
  ▼
Metasys

## Related Project

The corresponding LoRa receiver and BACnet MS/TP Gateway is:

**ESP32-BACnet-Master**
https://github.com/MGuerrero31416/ESP32-BACnet-Master

The `lora` branch contains the `HW_PROFILE_LORA_GATEWAY` implementation for receiving these sensor packets and exposing the values through BACnet MS/TP.

## LoRa Packet

The transmitter sends a fixed 26-byte packet containing:

- Protocol version
- Device ID
- Sequence number
- Temperature
- Relative humidity
- PM2.5
- VOC index
- Status

The protocol includes packet validation, sequence tracking, and transmitter reboot/session handling.

## Hardware

- Heltec WiFi LoRa 32 V4
- ESP32-S3
- SX1262 LoRa transceiver
- Sensirion SEN54
- SSD1315 128×64 OLED

## Current Development Status

The LoRa transmitter and BACnet gateway have been tested as a complete wireless link.

Initial development includes dummy sensor data for testing before connecting the SEN54. The production configuration reads the SEN54 and transmits the measured values to the LoRa/BACnet gateway.