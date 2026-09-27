idf_component_register(
    SRCS "main.c"
         "master_state.c"
         "master_commands.c"
         "master_console.c"
         "master_json.c"
         "network.c"
         "web.c"
    INCLUDE_DIRS "."
    REQUIRES mecs_core mecs_command mecs_espidf esp_timer esp_hw_support esp_wifi esp_netif
             esp_event nvs_flash esp_http_server
    EMBED_TXTFILES "index.html"
)
