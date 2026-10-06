idf_component_register(SRCS "main.c" "esp32_s3_szp.c" INCLUDE_DIRS "."
 REQUIRES esp_driver_uart esp_driver_usb_serial_jtag esp_psram esp_timer spi_flash esp_wifi esp_event esp_netif nvs_flash bt fatfs sdmmc esp_lcd driver spiffs)
