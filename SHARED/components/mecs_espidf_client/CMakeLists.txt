idf_component_register(SRCS "mecs_espidf_client.c" "MECSClient.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES mecs_core mecs_client mecs_espidf
                                esp_timer nvs_flash)
