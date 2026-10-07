idf_component_register(SRCS "main.c" "esp32_s3_szp.c" "core/button.c" "services/backlight.c"
 "core/usb_disk.c" "services/usb_transfer.c" "apps/usb_transfer_view.c"
 "assets/font_cjk.c" "assets/ui_art.c" "apps/weather_view.c" "apps/music_view.c" "core/arcade.c" "core/tilt.c" "services/console.c" "services/diagnostics.c" "core/terminal.c" "core/resampler.c" "core/path.c" "core/image_info.c" "core/conversation.c" "core/ws_text.c" "services/network.c" "services/storage.c" "services/sensors.c"
 "services/bluetooth.c" "services/media.c" "services/voice.c" "services/tools.c" "services/step_search.c" "core/search_intent.c" "core/search_evidence.c" "apps/ui_style.c" "apps/shell.c" "apps/games.c"
 INCLUDE_DIRS "." "usb_config"
 REQUIRES espressif__tinyusb usb audio_player esp_driver_uart esp_driver_usb_serial_jtag esp_psram esp_timer spi_flash esp_wifi esp_event esp_netif nvs_flash bt fatfs sdmmc esp_lcd driver spiffs esp_http_client esp_http_server lwip json mbedtls)
