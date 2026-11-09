# ESP-only: the TWAI driver for n2k_bus (the simulator never builds this).
idf_component_register(SRCS src/can_twai.c
                       INCLUDE_DIRS include
                       REQUIRES n2k n2k_bus esp_driver_twai freertos esp_driver_gpio)
