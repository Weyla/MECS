idf_component_register(SRCS "main.c" "input_hardware.c" "pwm_measurement.c" INCLUDE_DIRS "." REQUIRES mecs_core mecs_espidf esp_timer esp_hw_support esp_driver_gpio)
