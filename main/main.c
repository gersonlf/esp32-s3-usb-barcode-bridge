/*
 * Adaptador ESP32-S3:
 *   leitor USB HID (código de barras / QR Code) -> teclado Bluetooth LE HID
 *
 * Ambiente recomendado: ESP-IDF 5.5.4
 * Alvo: ESP32-S3
 *
 * USB nativo do ESP32-S3:
 *   GPIO19 = USB D+
 *   GPIO20 = USB D-
 *
 * O VBUS de 5 V do conector USB-A deve vir de uma fonte de 5 V adequada.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_hid_common.h"
#include "esp_hidd.h"
#include "nvs_flash.h"

#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_sm.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#define BLE_DEVICE_NAME             "Leitor QR ESP32"
#define BLE_KEYBOARD_REPORT_ID      1
#define BLE_REPORT_QUEUE_LENGTH     64
#define BLE_SEND_RETRIES            4
#define BLE_SEND_RETRY_DELAY_MS     6

static const char *TAG = "USB_BLE_BRIDGE";

static esp_hidd_dev_t *s_ble_hid_dev = NULL;
static QueueHandle_t s_ble_report_queue = NULL;
static QueueHandle_t s_usb_event_queue = NULL;

static volatile bool s_ble_connected = false;
static volatile bool s_ble_ready = false;

/*
 * Relatório HID de teclado padrão:
 * byte 0: modificadores
 * byte 1: reservado
 * bytes 2..7: até seis teclas simultâneas
 */
typedef struct {
    uint8_t data[8];
} ble_keyboard_report_t;

/*
 * Descriptor HID de teclado BLE.
 * O Report ID é 1 e o conteúdo enviado por esp_hidd_dev_input_set() tem 8 bytes.
 */
static const uint8_t s_keyboard_report_map[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x06,       /* Usage (Keyboard) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x01,       /* Report ID (1) */

    0x05, 0x07,       /* Usage Page (Keyboard/Keypad) */
    0x19, 0xE0,       /* Usage Minimum (Left Control) */
    0x29, 0xE7,       /* Usage Maximum (Right GUI) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x01,       /* Logical Maximum (1) */
    0x75, 0x01,       /* Report Size (1) */
    0x95, 0x08,       /* Report Count (8) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */

    0x95, 0x01,       /* Report Count (1) */
    0x75, 0x08,       /* Report Size (8) */
    0x81, 0x03,       /* Input (Constant) - byte reservado */

    0x95, 0x05,       /* Report Count (5) */
    0x75, 0x01,       /* Report Size (1) */
    0x05, 0x08,       /* Usage Page (LEDs) */
    0x19, 0x01,       /* Usage Minimum (Num Lock) */
    0x29, 0x05,       /* Usage Maximum (Kana) */
    0x91, 0x02,       /* Output (Data, Variable, Absolute) */

    0x95, 0x01,       /* Report Count (1) */
    0x75, 0x03,       /* Report Size (3) */
    0x91, 0x03,       /* Output (Constant) */

    0x95, 0x06,       /* Report Count (6) */
    0x75, 0x08,       /* Report Size (8) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x65,       /* Logical Maximum (101) */
    0x05, 0x07,       /* Usage Page (Keyboard/Keypad) */
    0x19, 0x00,       /* Usage Minimum (0) */
    0x29, 0x65,       /* Usage Maximum (101) */
    0x81, 0x00,       /* Input (Data, Array, Absolute) */

    0xC0              /* End Collection */
};

static esp_hid_raw_report_map_t s_ble_report_maps[] = {
    {
        .data = s_keyboard_report_map,
        .len = sizeof(s_keyboard_report_map),
    },
};

static esp_hid_device_config_t s_ble_hid_config = {
    .vendor_id = 0x303A,
    .product_id = 0x4001,
    .version = 0x0100,
    .device_name = BLE_DEVICE_NAME,
    .manufacturer_name = "MySoft Sistemas",
    .serial_number = "ESP32S3-USB-BLE-01",
    .report_maps = s_ble_report_maps,
    .report_maps_len = 1,
};

/* -------------------------------------------------------------------------- */
/* Bluetooth LE                                                               */
/* -------------------------------------------------------------------------- */

static struct ble_hs_adv_fields s_adv_fields;
static ble_uuid16_t s_hid_service_uuid = BLE_UUID16_INIT(0x1812);

static esp_err_t ble_advertising_start(void);

static int ble_gap_event_callback(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_ble_connected = true;
            s_ble_ready = false;
            ESP_LOGI(TAG, "Bluetooth conectado; aguardando pareamento e notificacoes HID");
        } else {
            ESP_LOGW(TAG, "Falha na conexao BLE: status=%d", event->connect.status);
            ble_advertising_start();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Bluetooth desconectado: motivo=%d", event->disconnect.reason);
        s_ble_connected = false;
        s_ble_ready = false;
        if (s_ble_report_queue != NULL) {
            xQueueReset(s_ble_report_queue);
        }
        ble_advertising_start();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        /* O celular habilita notify na característica de entrada do teclado. */
        if (event->subscribe.cur_notify || event->subscribe.cur_indicate) {
            s_ble_ready = true;
            ESP_LOGI(TAG, "Teclado BLE pronto para transmitir leituras");
        }
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            ESP_LOGI(TAG, "Conexao BLE criptografada/pareada");
        } else {
            ESP_LOGW(TAG, "Falha de seguranca BLE: status=%d", event->enc_change.status);
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        int rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (!s_ble_connected) {
            ble_advertising_start();
        }
        return 0;

    default:
        return 0;
    }
}

static esp_err_t ble_advertising_configure(void)
{
    memset(&s_adv_fields, 0, sizeof(s_adv_fields));

    s_adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    s_adv_fields.appearance = ESP_HID_APPEARANCE_KEYBOARD;
    s_adv_fields.appearance_is_present = 1;
    s_adv_fields.tx_pwr_lvl_is_present = 1;
    s_adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    s_adv_fields.name = (uint8_t *)BLE_DEVICE_NAME;
    s_adv_fields.name_len = strlen(BLE_DEVICE_NAME);
    s_adv_fields.name_is_complete = 1;
    s_adv_fields.uuids16 = &s_hid_service_uuid;
    s_adv_fields.num_uuids16 = 1;
    s_adv_fields.uuids16_is_complete = 1;

    /* Pareamento "Just Works", adequado a um adaptador sem tela e sem teclado local. */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    return ESP_OK;
}

static esp_err_t ble_advertising_start(void)
{
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));

    int rc = ble_gap_adv_set_fields(&s_adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Falha ao configurar anuncio BLE: rc=%d", rc);
        return ESP_FAIL;
    }

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(30);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);

    rc = ble_gap_adv_start(
        BLE_OWN_ADDR_PUBLIC,
        NULL,
        BLE_HS_FOREVER,
        &adv_params,
        ble_gap_event_callback,
        NULL
    );

    if (rc != 0) {
        ESP_LOGW(TAG, "Nao foi possivel iniciar anuncio BLE: rc=%d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Bluetooth anunciando como '%s'", BLE_DEVICE_NAME);
    return ESP_OK;
}

static void ble_hidd_event_callback(
    void *handler_args,
    esp_event_base_t base,
    int32_t id,
    void *event_data
)
{
    (void)handler_args;
    (void)base;

    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "Perfil de teclado HID BLE iniciado");
        ble_advertising_start();
        break;

    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "Host conectado ao perfil HID BLE");
        break;

    case ESP_HIDD_OUTPUT_EVENT:
        /* LEDs do teclado, como Num Lock e Caps Lock. */
        ESP_LOGD(TAG, "Relatorio de saida HID: id=%u tamanho=%u",
                 param->output.report_id,
                 (unsigned)param->output.length);
        break;

    case ESP_HIDD_DISCONNECT_EVENT:
        s_ble_connected = false;
        s_ble_ready = false;
        break;

    case ESP_HIDD_STOP_EVENT:
        ESP_LOGI(TAG, "Perfil HID BLE encerrado");
        break;

    default:
        break;
    }
}

static void ble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "Tarefa NimBLE iniciada");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_store_config_init(void);

static void ble_init(void)
{
    ESP_ERROR_CHECK(ble_advertising_configure());

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_nimble_init());

    ESP_ERROR_CHECK(esp_hidd_dev_init(
        &s_ble_hid_config,
        ESP_HID_TRANSPORT_BLE,
        ble_hidd_event_callback,
        &s_ble_hid_dev
    ));

    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ESP_ERROR_CHECK(esp_nimble_enable(ble_host_task));
}

static void ble_sender_task(void *arg)
{
    (void)arg;
    ble_keyboard_report_t report;

    while (true) {
        if (xQueueReceive(s_ble_report_queue, &report, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!s_ble_ready || s_ble_hid_dev == NULL) {
            ESP_LOGW(TAG, "Leitura ignorada: nenhum celular/tablet BLE pronto");
            continue;
        }

        esp_err_t err = ESP_FAIL;
        for (int attempt = 0; attempt < BLE_SEND_RETRIES; ++attempt) {
            err = esp_hidd_dev_input_set(
                s_ble_hid_dev,
                0,
                BLE_KEYBOARD_REPORT_ID,
                report.data,
                sizeof(report.data)
            );

            if (err == ESP_OK) {
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(BLE_SEND_RETRY_DELAY_MS));
        }

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Falha ao enviar relatorio BLE: %s", esp_err_to_name(err));
        }

        /* Limita a taxa para não sobrecarregar as notificações BLE. */
        vTaskDelay(pdMS_TO_TICKS(3));
    }
}

/* -------------------------------------------------------------------------- */
/* USB Host HID                                                               */
/* -------------------------------------------------------------------------- */

typedef struct {
    hid_host_device_handle_t handle;
    hid_host_driver_event_t event;
    void *arg;
} usb_hid_event_t;

static void usb_keyboard_report_forward(const uint8_t *data, size_t length)
{
    if (length < sizeof(hid_keyboard_input_report_boot_t)) {
        ESP_LOGW(TAG, "Relatorio USB curto: %u bytes", (unsigned)length);
        return;
    }

    const hid_keyboard_input_report_boot_t *usb_report =
        (const hid_keyboard_input_report_boot_t *)data;

    ble_keyboard_report_t ble_report = {0};
    ble_report.data[0] = usb_report->modifier.val;
    ble_report.data[1] = 0;
    memcpy(&ble_report.data[2], usb_report->key, HID_KEYBOARD_KEY_MAX);

    /*
     * Envia tanto pressionamentos quanto liberações. A liberação é essencial
     * para que caracteres repetidos, como "00011", funcionem corretamente.
     */
    if (xQueueSend(s_ble_report_queue, &ble_report, pdMS_TO_TICKS(20)) != pdTRUE) {
        ESP_LOGW(TAG, "Fila BLE cheia; relatorio do leitor descartado");
    }
}

static void hid_interface_callback(
    hid_host_device_handle_t hid_device_handle,
    const hid_host_interface_event_t event,
    void *arg
)
{
    (void)arg;

    hid_host_dev_params_t params;
    esp_err_t err = hid_host_device_get_params(hid_device_handle, &params);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Nao foi possivel obter parametros HID: %s", esp_err_to_name(err));
        return;
    }

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
        uint8_t data[64] = {0};
        size_t data_length = 0;

        err = hid_host_device_get_raw_input_report_data(
            hid_device_handle,
            data,
            sizeof(data),
            &data_length
        );

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Erro ao receber relatorio USB HID: %s", esp_err_to_name(err));
            return;
        }

        if (params.proto == HID_PROTOCOL_KEYBOARD &&
            params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
            usb_keyboard_report_forward(data, data_length);
        }
        break;
    }

    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Leitor USB desconectado");
        hid_host_device_close(hid_device_handle);
        break;

    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        ESP_LOGW(TAG, "Erro de transferencia com o leitor USB");
        break;

    default:
        break;
    }
}

static void process_hid_device_event(const usb_hid_event_t *event)
{
    hid_host_dev_params_t params;
    ESP_ERROR_CHECK(hid_host_device_get_params(event->handle, &params));

    if (event->event != HID_HOST_DRIVER_EVENT_CONNECTED) {
        return;
    }

    ESP_LOGI(TAG,
             "Dispositivo USB HID conectado: protocolo=%d subclasse=%d",
             params.proto,
             params.sub_class);

    if (params.proto != HID_PROTOCOL_KEYBOARD) {
        ESP_LOGW(TAG, "O dispositivo USB nao se identificou como teclado HID");
        return;
    }

    if (params.sub_class != HID_SUBCLASS_BOOT_INTERFACE) {
        ESP_LOGW(TAG,
                 "O leitor nao usa o protocolo HID Boot Keyboard; esta versao nao conseguira interpreta-lo");
        return;
    }

    const hid_host_device_config_t dev_config = {
        .callback = hid_interface_callback,
        .callback_arg = NULL,
    };

    ESP_ERROR_CHECK(hid_host_device_open(event->handle, &dev_config));
    ESP_ERROR_CHECK(hid_class_request_set_protocol(event->handle, HID_REPORT_PROTOCOL_BOOT));
    ESP_ERROR_CHECK(hid_class_request_set_idle(event->handle, 0, 0));
    ESP_ERROR_CHECK(hid_host_device_start(event->handle));

    ESP_LOGI(TAG, "Leitor USB pronto");
}

static void hid_device_callback(
    hid_host_device_handle_t hid_device_handle,
    const hid_host_driver_event_t event,
    void *arg
)
{
    usb_hid_event_t queued_event = {
        .handle = hid_device_handle,
        .event = event,
        .arg = arg,
    };

    if (s_usb_event_queue != NULL) {
        xQueueSend(s_usb_event_queue, &queued_event, 0);
    }
}

static void usb_library_task(void *arg)
{
    TaskHandle_t task_to_notify = (TaskHandle_t)arg;

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    ESP_ERROR_CHECK(usb_host_install(&host_config));
    xTaskNotifyGive(task_to_notify);

    while (true) {
        uint32_t event_flags = 0;
        esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Erro no USB Host: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

static void usb_hid_event_task(void *arg)
{
    (void)arg;
    usb_hid_event_t event;

    while (true) {
        if (xQueueReceive(s_usb_event_queue, &event, portMAX_DELAY) == pdTRUE) {
            process_hid_device_event(&event);
        }
    }
}

static void usb_init(void)
{
    s_usb_event_queue = xQueueCreate(10, sizeof(usb_hid_event_t));
    assert(s_usb_event_queue != NULL);

    BaseType_t created = xTaskCreate(
        usb_library_task,
        "usb_library",
        4096,
        xTaskGetCurrentTaskHandle(),
        5,
        NULL
    );
    assert(created == pdTRUE);

    /* Aguarda usb_host_install(). */
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    const hid_host_driver_config_t hid_config = {
        .create_background_task = true,
        .task_priority = 6,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_device_callback,
        .callback_arg = NULL,
    };

    ESP_ERROR_CHECK(hid_host_install(&hid_config));

    created = xTaskCreate(
        usb_hid_event_task,
        "usb_hid_events",
        4096,
        NULL,
        5,
        NULL
    );
    assert(created == pdTRUE);

    ESP_LOGI(TAG, "USB Host iniciado; conecte o leitor USB-A");
}

/* -------------------------------------------------------------------------- */
/* Aplicação                                                                  */
/* -------------------------------------------------------------------------- */

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_ble_report_queue = xQueueCreate(
        BLE_REPORT_QUEUE_LENGTH,
        sizeof(ble_keyboard_report_t)
    );
    assert(s_ble_report_queue != NULL);

    BaseType_t created = xTaskCreate(
        ble_sender_task,
        "ble_sender",
        4096,
        NULL,
        7,
        NULL
    );
    assert(created == pdTRUE);

    ble_init();
    usb_init();

    ESP_LOGI(TAG, "Adaptador pronto");
    ESP_LOGI(TAG, "1) Pareie '%s' no celular/tablet", BLE_DEVICE_NAME);
    ESP_LOGI(TAG, "2) Abra um campo de texto");
    ESP_LOGI(TAG, "3) Leia um codigo de barras ou QR Code");
}
