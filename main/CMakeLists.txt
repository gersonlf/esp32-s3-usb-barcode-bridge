idf_component_register(
    SRCS
        "main.c"
        "ble_hid_keyboard.c"
    INCLUDE_DIRS "."
    REQUIRES esp_hid bt nvs_flash usb
    PRIV_REQUIRES espressif__usb_host_hid
)
