#include "ble_hid_keyboard.h"

#include <string.h>

#include "esp_log.h"

#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define UUID_HID_SERVICE                 0x1812
#define UUID_BATTERY_SERVICE             0x180F
#define UUID_DEVICE_INFORMATION_SERVICE  0x180A

#define UUID_HID_INFORMATION             0x2A4A
#define UUID_REPORT_MAP                  0x2A4B
#define UUID_HID_CONTROL_POINT           0x2A4C
#define UUID_REPORT                      0x2A4D
#define UUID_PROTOCOL_MODE               0x2A4E
#define UUID_BATTERY_LEVEL               0x2A19
#define UUID_MANUFACTURER_NAME           0x2A29
#define UUID_SERIAL_NUMBER               0x2A25
#define UUID_PNP_ID                      0x2A50

#define UUID_REPORT_REFERENCE            0x2908

#define HID_REPORT_TYPE_INPUT            1
#define HID_PROTOCOL_REPORT              1

#define HID_READ_FLAGS \
    (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC)
#define HID_NOTIFY_FLAGS \
    (BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC)

static const char *TAG = "BLE_HID_KEYBOARD";

static const ble_uuid16_t s_uuid_hid_service = BLE_UUID16_INIT(UUID_HID_SERVICE);
static const ble_uuid16_t s_uuid_battery_service = BLE_UUID16_INIT(UUID_BATTERY_SERVICE);
static const ble_uuid16_t s_uuid_device_information_service =
    BLE_UUID16_INIT(UUID_DEVICE_INFORMATION_SERVICE);

static const ble_uuid16_t s_uuid_hid_information = BLE_UUID16_INIT(UUID_HID_INFORMATION);
static const ble_uuid16_t s_uuid_report_map = BLE_UUID16_INIT(UUID_REPORT_MAP);
static const ble_uuid16_t s_uuid_hid_control_point = BLE_UUID16_INIT(UUID_HID_CONTROL_POINT);
static const ble_uuid16_t s_uuid_report = BLE_UUID16_INIT(UUID_REPORT);
static const ble_uuid16_t s_uuid_protocol_mode = BLE_UUID16_INIT(UUID_PROTOCOL_MODE);
static const ble_uuid16_t s_uuid_battery_level = BLE_UUID16_INIT(UUID_BATTERY_LEVEL);
static const ble_uuid16_t s_uuid_manufacturer_name =
    BLE_UUID16_INIT(UUID_MANUFACTURER_NAME);
static const ble_uuid16_t s_uuid_serial_number = BLE_UUID16_INIT(UUID_SERIAL_NUMBER);
static const ble_uuid16_t s_uuid_pnp_id = BLE_UUID16_INIT(UUID_PNP_ID);

static const ble_uuid16_t s_uuid_report_reference =
    BLE_UUID16_INIT(UUID_REPORT_REFERENCE);

/*
 * Teclado HID de 8 bytes em Report Protocol, sem Report ID:
 * modificadores, reservado e seis codigos de tecla. Este e o formato
 * minimo do exemplo de teclado interoperavel com iOS da Silicon Labs.
 */
static const uint8_t s_keyboard_report_map[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x06,       /* Usage (Keyboard) */
    0xA1, 0x01,       /* Collection (Application) */

    0x05, 0x07,
    0x19, 0xE0,
    0x29, 0xE7,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x08,
    0x81, 0x02,

    0x95, 0x01,
    0x75, 0x08,
    0x81, 0x01,

    0x95, 0x06,
    0x75, 0x08,
    0x15, 0x00,
    0x25, 0x65,
    0x05, 0x07,
    0x19, 0x00,
    0x29, 0x65,
    0x81, 0x00,
    0xC0
};

static const uint8_t s_hid_information[] = {
    0x11, 0x01,       /* HID version 1.11 */
    0x00,             /* Country code */
    0x02              /* Normally connectable */
};
static const uint8_t s_input_report_reference[] = {
    0x00,             /* Sem Report ID no descritor HID */
    HID_REPORT_TYPE_INPUT
};
static const uint8_t s_pnp_id[] = {
    0x02,             /* Vendor ID source: USB-IF */
    0x3A, 0x30,       /* Vendor ID: 0x303A */
    0x01, 0x40,       /* Product ID: 0x4001 */
    0x00, 0x01        /* Product version: 0x0100 */
};
static const char s_manufacturer_name[] = "MySoft Sistemas";
static const char s_serial_number[] = "ESP32S3-USB-BLE-02";

static uint8_t s_protocol_mode = HID_PROTOCOL_REPORT;
static uint8_t s_control_point = 1;
static uint8_t s_input_report[BLE_HID_KEYBOARD_REPORT_SIZE];
static uint8_t s_battery_level = 100;

static uint16_t s_report_input_handle;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_report_input_subscribed;

typedef enum {
    VALUE_PROTOCOL_MODE,
    VALUE_REPORT_MAP,
    VALUE_REPORT_INPUT,
    VALUE_HID_INFORMATION,
    VALUE_CONTROL_POINT,
    VALUE_BATTERY_LEVEL,
    VALUE_MANUFACTURER_NAME,
    VALUE_SERIAL_NUMBER,
    VALUE_PNP_ID,
} value_id_t;

static int append_value(struct ble_gatt_access_ctxt *ctxt,
                        const void *value,
                        size_t length)
{
    return os_mbuf_append(ctxt->om, value, length) == 0
        ? 0
        : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int read_exact(struct os_mbuf *om, void *value, size_t length)
{
    if (OS_MBUF_PKTLEN(om) != length) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    return os_mbuf_copydata(om, 0, length, value) == 0
        ? 0
        : BLE_ATT_ERR_UNLIKELY;
}

static int characteristic_access(uint16_t conn_handle,
                                 uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt,
                                 void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    value_id_t value_id = (value_id_t)(uintptr_t)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        switch (value_id) {
        case VALUE_PROTOCOL_MODE:
            return append_value(ctxt, &s_protocol_mode, sizeof(s_protocol_mode));
        case VALUE_REPORT_MAP:
            return append_value(ctxt, s_keyboard_report_map,
                                sizeof(s_keyboard_report_map));
        case VALUE_REPORT_INPUT:
            return append_value(ctxt, s_input_report, sizeof(s_input_report));
        case VALUE_HID_INFORMATION:
            return append_value(ctxt, s_hid_information, sizeof(s_hid_information));
        case VALUE_BATTERY_LEVEL:
            return append_value(ctxt, &s_battery_level, sizeof(s_battery_level));
        case VALUE_MANUFACTURER_NAME:
            return append_value(ctxt, s_manufacturer_name,
                                strlen(s_manufacturer_name));
        case VALUE_SERIAL_NUMBER:
            return append_value(ctxt, s_serial_number, strlen(s_serial_number));
        case VALUE_PNP_ID:
            return append_value(ctxt, s_pnp_id, sizeof(s_pnp_id));
        default:
            return BLE_ATT_ERR_READ_NOT_PERMITTED;
        }
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        int rc;

        switch (value_id) {
        case VALUE_PROTOCOL_MODE: {
            uint8_t protocol_mode;
            rc = read_exact(ctxt->om, &protocol_mode, sizeof(protocol_mode));
            if (rc != 0) {
                return rc;
            }
            if (protocol_mode != HID_PROTOCOL_REPORT) {
                return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            }
            s_protocol_mode = protocol_mode;
            ESP_LOGI(TAG, "Modo HID mantido em Report");
            return 0;
        }
        case VALUE_CONTROL_POINT:
            return read_exact(ctxt->om, &s_control_point, sizeof(s_control_point));
        default:
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        }
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static int descriptor_access(uint16_t conn_handle,
                             uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg)
{
    (void)conn_handle;
    (void)attr_handle;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_DSC) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    if (arg == s_input_report_reference) {
        return append_value(ctxt, s_input_report_reference,
                            sizeof(s_input_report_reference));
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static struct ble_gatt_dsc_def s_input_report_descriptors[] = {
    {
        .uuid = &s_uuid_report_reference.u,
        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC,
        .access_cb = descriptor_access,
        .arg = (void *)s_input_report_reference,
    },
    {0}
};

static const struct ble_gatt_chr_def s_hid_characteristics[] = {
    {
        .uuid = &s_uuid_protocol_mode.u,
        .access_cb = characteristic_access,
        .arg = (void *)VALUE_PROTOCOL_MODE,
        .flags = HID_READ_FLAGS | BLE_GATT_CHR_F_WRITE_NO_RSP |
                 BLE_GATT_CHR_F_WRITE_ENC,
    },
    {
        .uuid = &s_uuid_report_map.u,
        .access_cb = characteristic_access,
        .arg = (void *)VALUE_REPORT_MAP,
        .flags = HID_READ_FLAGS,
    },
    {
        .uuid = &s_uuid_report.u,
        .access_cb = characteristic_access,
        .arg = (void *)VALUE_REPORT_INPUT,
        .val_handle = &s_report_input_handle,
        .flags = HID_READ_FLAGS | HID_NOTIFY_FLAGS,
        .descriptors = s_input_report_descriptors,
    },
    {
        .uuid = &s_uuid_hid_information.u,
        .access_cb = characteristic_access,
        .arg = (void *)VALUE_HID_INFORMATION,
        .flags = HID_READ_FLAGS,
    },
    {
        .uuid = &s_uuid_hid_control_point.u,
        .access_cb = characteristic_access,
        .arg = (void *)VALUE_CONTROL_POINT,
        .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
    },
    {0}
};

static const struct ble_gatt_chr_def s_battery_characteristics[] = {
    {
        .uuid = &s_uuid_battery_level.u,
        .access_cb = characteristic_access,
        .arg = (void *)VALUE_BATTERY_LEVEL,
        .flags = HID_READ_FLAGS,
    },
    {0}
};

static const struct ble_gatt_chr_def s_device_information_characteristics[] = {
    {
        .uuid = &s_uuid_manufacturer_name.u,
        .access_cb = characteristic_access,
        .arg = (void *)VALUE_MANUFACTURER_NAME,
        .flags = HID_READ_FLAGS,
    },
    {
        .uuid = &s_uuid_serial_number.u,
        .access_cb = characteristic_access,
        .arg = (void *)VALUE_SERIAL_NUMBER,
        .flags = HID_READ_FLAGS,
    },
    {
        .uuid = &s_uuid_pnp_id.u,
        .access_cb = characteristic_access,
        .arg = (void *)VALUE_PNP_ID,
        .flags = HID_READ_FLAGS,
    },
    {0}
};

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_uuid_hid_service.u,
        .characteristics = s_hid_characteristics,
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_uuid_battery_service.u,
        .characteristics = s_battery_characteristics,
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_uuid_device_information_service.u,
        .characteristics = s_device_information_characteristics,
    },
    {0}
};

esp_err_t ble_hid_keyboard_service_init(void)
{
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "Falha ao contar atributos HOGP: rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "Falha ao registrar servico HOGP: rc=%d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "Servico HOGP minimo registrado para teclado Report de 8 bytes sem Report ID");
    return ESP_OK;
}

void ble_hid_keyboard_connection_set(uint16_t conn_handle, bool connected)
{
    s_conn_handle = connected ? conn_handle : BLE_HS_CONN_HANDLE_NONE;
    s_protocol_mode = HID_PROTOCOL_REPORT;
    s_report_input_subscribed = false;
    memset(s_input_report, 0, sizeof(s_input_report));
}

bool ble_hid_keyboard_subscription_update(uint16_t attr_handle, bool notify_enabled)
{
    if (attr_handle == s_report_input_handle) {
        s_report_input_subscribed = notify_enabled;
        ESP_LOGI(TAG, "Notificacoes Report Input %s (handle=%u)",
                 notify_enabled ? "habilitadas" : "desabilitadas",
                 attr_handle);
        return true;
    }

    return false;
}

bool ble_hid_keyboard_ready(void)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return false;
    }

    return s_report_input_subscribed;
}

esp_err_t ble_hid_keyboard_input_send(const uint8_t *report, size_t length)
{
    if (report == NULL || length != BLE_HID_KEYBOARD_REPORT_SIZE ||
        !ble_hid_keyboard_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(s_input_report, report, length);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(report, length);
    if (om == NULL) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_report_input_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Falha no notify HID: handle=%u rc=%d",
                 s_report_input_handle, rc);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Notify HID enviado: handle=%u tamanho=%u",
             s_report_input_handle, (unsigned)length);
    return ESP_OK;
}
