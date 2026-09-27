idf_component_register(SRCS "mio_can.c" "mio_espidf_client.c"
                       INCLUDE_DIRS "include"
                       REQUIRES mio_core mio_client esp_driver_twai freertos)
