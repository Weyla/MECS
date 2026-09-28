idf_component_register(SRCS "mecs_can.c"
                       INCLUDE_DIRS "include"
                       REQUIRES mecs_core esp_driver_twai freertos esp_timer)
