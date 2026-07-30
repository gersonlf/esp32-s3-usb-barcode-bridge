#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define BLE_HID_KEYBOARD_REPORT_SIZE 8

esp_err_t ble_hid_keyboard_service_init(void);
void ble_hid_keyboard_connection_set(uint16_t conn_handle, bool connected);
bool ble_hid_keyboard_subscription_update(uint16_t attr_handle, bool notify_enabled);
bool ble_hid_keyboard_ready(void);
esp_err_t ble_hid_keyboard_input_send(const uint8_t *report, size_t length);

