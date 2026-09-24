# ESP32 LoRa Room Sensor Transmitter — SEN54 / SEN56

Wireless room sensor transmitter based on the **Heltec WiFi LoRa 32 V4 (SX1262)** and a **Sensirion SEN54/SEN56-class environmental sensor**.

This project is the transmitter side of the **LoRa BACnet Gateway** located here:

https://github.com/MGuerrero31416/LoRa-BACnet-Gateway

The transmitter reads the air quality and environmental data from the sensor and sends it over LoRa to the gateway, which exposes the values through **BACnet MS/TP** for building automation integration such as Johnson Controls Metasys.

## System Architecture

SEN54 / SEN56
  │
  ▼
ESP32-S3 + SX1262
  │
  │ LoRa
  ▼
LoRa BACnet Gateway
  │
  │ RS-485 / BACnet MS/TP
  ▼
BACnet MS/TP
  │
  ▼
Metasys

## Related Project

The corresponding receiving gateway project is:

**LoRa BACnet Gateway**
https://github.com/MGuerrero31416/LoRa-BACnet-Gateway

This transmitter sends the wireless sensor payload to that gateway, which decodes the packet and exposes the values over BACnet MS/TP.

## Sensor GPIO / I2C Wiring

The current code configures the Sensirion sensor on the ESP32's I2C port 1:

- Sensor family: Sensirion SEN54 (SEN56-compatible interface)
- I2C bus: `I2C_NUM_1`
- SDA GPIO: `GPIO4`
- SCL GPIO: `GPIO6`
- I2C address: `0x69`
- Clock speed: `100000 Hz`

This is the exact hardware mapping implemented in the device firmware.

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
- Sensirion SEN54 / SEN56-class sensor
- SSD1306 128×64 OLED

## Current Development Status

The LoRa transmitter and BACnet gateway have been tested as a complete wireless link.

Initial development included dummy sensor data for validation before connecting the environmental sensor. The production configuration reads the SEN54/SEN56-family sensor and transmits the measured values to the LoRa/BACnet gateway.