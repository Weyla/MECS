idf_component_register(SRCS "main.c" "output_hardware.c" INCLUDE_DIRS "." REQUIRES mecs_core mecs_espidf esp_timer esp_hw_support esp_driver_gpio esp_driver_ledc)
