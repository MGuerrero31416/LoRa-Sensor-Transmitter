idf_component_register(SRCS "main.c" "display.c" "modbus_sensor.c" "lora_sx1262.c" "telemetry_packet.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_uart esp_timer
                             espressif__esp-modbus u8g2)
