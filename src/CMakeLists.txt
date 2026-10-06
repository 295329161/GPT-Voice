idf_component_register(SRCS "main.c" "esp32_s3_szp.c"
 "assets/font_cjk.c" "services/console.c" "core/terminal.c" "core/resampler.c" "core/path.c" "core/conversation.c" "core/ws_text.c" "services/network.c" "services/storage.c" "services/sensors.c"
 "services/bluetooth.c" "services/media.c" "services/voice.c" "services/tools.c" "apps/shell.c" "apps/games.c"
 INCLUDE_DIRS "."
 REQUIRES audio_player esp_driver_uart esp_driver_usb_serial_jtag esp_psram esp_timer spi_flash esp_wifi esp_event esp_netif nvs_flash bt fatfs sdmmc esp_lcd driver spiffs esp_http_client esp_http_server lwip json mbedtls)
