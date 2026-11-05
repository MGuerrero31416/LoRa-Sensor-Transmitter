# ESP32 LoRa Analog Sensor Transmitter

Wireless analog sensor transmitter based on the **Heltec WiFi LoRa 32 V4 (SX1262)**. It reads one analog measurement from a Modbus RTU device over an external RS-485 transceiver and sends it to the LoRa gateway.

This project is the transmitter side of the **LoRa BACnet Gateway** located here:

https://github.com/MGuerrero31416/LoRa-BACnet-Gateway

The transmitter reads one analog measurement from a Modbus device and sends it over LoRa to the gateway, which exposes the value through **BACnet MS/TP** for building automation integration such as Johnson Controls Metasys.

## Hardware Photos

<img src="docs/images/TX.jpg" alt="LoRa sensor transmitter board" width="100%" />

<img src="docs/images/TX-RX.jpg" alt="LoRa transmitter and receiver setup" width="100%" />

## System Architecture

```text
Modbus analog sensor
  |
  | Modbus RTU / RS-485
  v
ESP32-S3 + SX1262
  |
  | LoRa
  v
LoRa BACnet Gateway
  |
  | RS-485 / BACnet MS/TP
  v
BACnet MS/TP
  |
  v
Building Automation System
```

## Related Project

The corresponding receiving gateway project is:

**LoRa BACnet Gateway**
https://github.com/MGuerrero31416/LoRa-BACnet-Gateway

This transmitter sends the wireless sensor payload to that gateway, which decodes the packet and exposes the values over BACnet MS/TP.

## Modbus RTU / RS-485 Wiring

The Modbus master uses UART1 and an external RS-485 transceiver:

- ESP32 TX: `GPIO4` to transceiver DI
- ESP32 RX: `GPIO6` from transceiver RO
- UART RTS: `GPIO5` connected to both transceiver DE and active-low `/RE`
- Transceiver A/B: to the Modbus device's RS-485 pair; connect grounds

GPIO4, GPIO5, and GPIO6 are exposed on Heltec V4.2.0 header J3 pins 15, 16, and 17 respectively. GPIO5 is not used by the firmware's LoRa or OLED mappings and is configured as the hardware UART RTS direction signal. The ESP-Modbus RTU master sets UART1 to hardware half-duplex mode; no GPIO direction-control task is used.

Modbus settings are grouped at the top of [main/modbus_sensor.c](main/modbus_sensor.c): unit ID `1`, input register function `0x04`, zero-based register address `0`, 9600 baud, 8N1, and scale `1.0` with offset `0.0`. Change these to match the connected device. The default measurement is an unsigned 16-bit register value converted as `raw * scale + offset`.

## OLED GPIO / I2C Wiring

The SSD1306 OLED uses a separate I2C bus:

- Display: SSD1306 128x64
- I2C bus: `I2C_NUM_0`
- SDA GPIO: `GPIO17`
- SCL GPIO: `GPIO18`
- I2C address: `0x3C`
- Clock speed: `400000 Hz`
- Reset GPIO: `GPIO21`
- Power-enable GPIO: `GPIO36`

## LoRa Packet

The transmitter sends a fixed 26-byte packet. All multi-byte values use little-endian byte order, and floating-point values use the ESP32's 32-bit IEEE-754 representation.

| Offset | Size | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 1 | Protocol version | `uint8_t` |
| 1 | 4 | Device ID | `uint32_t`, little-endian |
| 5 | 4 | Sequence number | `uint32_t`, little-endian |
| 9 | 4 | Temperature | 32-bit float, little-endian |
| 13 | 4 | Relative humidity | 32-bit float, little-endian |
| 17 | 4 | PM2.5 | 32-bit float, little-endian |
| 21 | 4 | VOC index | 32-bit float, little-endian |
| 25 | 1 | Status | Bit 0 is the boot flag |

The packet fields are:

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
- Modbus RTU analog sensor and RS-485 transceiver
- SSD1306 128×64 OLED

## User Settings

Modify these settings before building and flashing the transmitter:

- Firmware revision and date: `FIRMWARE_REVISION` and `FIRMWARE_DATE` in [main/main.c](main/main.c).
- Device identifier: `DEVICE_ID` in [main/main.c](main/main.c). This must match the device identity expected by the gateway.
- Transmission interval: `TRANSMIT_PERIOD_MS` in [main/main.c](main/main.c), in milliseconds.
- LoRa frequency and transmit power: `LORA_FREQUENCY_HZ` and `LORA_TX_POWER_DBM` in [main/lora_sx1262.h](main/lora_sx1262.h). The gateway and regional radio configuration must use a compatible frequency.
- LoRa modulation: `LORA_BANDWIDTH`, `LORA_SPREADING_FACTOR`, `LORA_CODING_RATE`, and `LORA_PREAMBLE_LENGTH` in [main/lora_sx1262.h](main/lora_sx1262.h). These settings must match the gateway.
- Packet format: `TELEMETRY_PACKET_VERSION` and `TELEMETRY_PACKET_LEN` in [main/telemetry_packet.h](main/telemetry_packet.h). Changes require corresponding gateway updates.
- OLED I2C address and speed: `OLED_ADDRESS` and `OLED_I2C_FREQUENCY_HZ` in [main/display.h](main/display.h).

Hardware pin mappings are defined in [main/lora_sx1262.c](main/lora_sx1262.c), [main/modbus_sensor.c](main/modbus_sensor.c), and [main/display.c](main/display.c).

## Current Development Status

The LoRa transmitter and BACnet gateway have been tested as a complete wireless link.

The transmitter reads one Modbus RTU analog register and temporarily places its value in the packet's existing VOC float field. If the Modbus master cannot initialize or a register read fails, the transmitter sends `-1.0` in that field to indicate an unavailable measurement, then waits 10 seconds before the next retry/transmission cycle. When the measurement is available, the transmitter retains its regular 10-second transmission schedule. The telemetry packet remains 26 bytes with its current version and offsets; temperature, humidity, and PM2.5 are also encoded as `-1.0` (unavailable).