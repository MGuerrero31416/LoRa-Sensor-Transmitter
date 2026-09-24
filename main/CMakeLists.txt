idf_component_register(SRCS "main.c" "display.c" "sen54.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_timer u8g2)
