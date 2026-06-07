idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd_axs15231b esp_lcd driver esp_driver_ledc esp_driver_gpio esp_driver_spi esp_timer
)
