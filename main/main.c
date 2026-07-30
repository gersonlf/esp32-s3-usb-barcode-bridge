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
#include "nvs_flash.h"

#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_sm.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"

#include "ble_hid_keyboard.h"

#define BLE_DEVICE_NAME             "Leitor QR ESP32"
#define BLE_REPORT_QUEUE_LENGTH     64
#define BLE_SEND_RETRIES            4
#define BLE_SEND_RETRY_DELAY_MS     6
#define BLE_KEY_PRESS_DELAY_MS      50
#define BLE_INTER_REPORT_DELAY_MS   5

static const char *TAG = "USB_BLE_BRIDGE";

static QueueHandle_t s_ble_report_queue = NULL;
static QueueHandle_t s_usb_event_queue = NULL;

static volatile bool s_ble_connected = false;
static uint16_t s_ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_ble_own_addr_type = BLE_OWN_ADDR_PUBLIC;
static bool s_ble_random_addr_configured = false;

/*
 * Relatório HID de teclado padrão:
 * byte 0: modificadores
 * byte 1: reservado
 * bytes 2..7: até seis teclas simultâneas
 */
typedef struct {
    uint8_t data[BLE_HID_KEYBOARD_REPORT_SIZE];
} ble_keyboard_report_t;

static bool keyboard_report_has_pressed_key(const ble_keyboard_report_t *report)
{
    if (report->data[0] != 0) {
        return true;
    }

    for (size_t i = 2; i < sizeof(report->data); ++i) {
        if (report->data[i] != 0) {
            return true;
        }
    }

    return false;
}

/* -------------------------------------------------------------------------- */
/* Bluetooth LE                                                               */
/* -------------------------------------------------------------------------- */

static struct ble_hs_adv_fields s_adv_fields;
static ble_uuid16_t s_hid_service_uuid = BLE_UUID16_INIT(0x1812);

static esp_err_t ble_advertising_start(void);
static void ble_random_static_address_configure(void)
{
    if (s_ble_random_addr_configured) {
        return;
    }

    /*
     * iOS pode manter cache GATT agressivo para a identidade BLE publica.
     * Usar um random static fixo preserva bonding entre reboots e força
     * uma identidade nova para este firmware de teclado HID.
     */
    static const uint8_t random_static_addr[6] = {
        0x35, 0x3D, 0xDA, 0x8F, 0xCB, 0xC4
    };

    int rc = ble_hs_id_set_rnd(random_static_addr);
    if (rc != 0) {
        ESP_LOGW(TAG, "Nao foi possivel configurar endereco BLE random static: rc=%d", rc);
        s_ble_own_addr_type = BLE_OWN_ADDR_PUBLIC;
        return;
    }

    s_ble_own_addr_type = BLE_OWN_ADDR_RANDOM;
    s_ble_random_addr_configured = true;
    ESP_LOGI(TAG, "Endereco BLE random static configurado: C4:CB:8F:DA:3D:35");
}

static void ble_gap_identity_configure(void)
{
    int rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGW(TAG, "Nao foi possivel definir nome GAP BLE: rc=%d", rc);
    }

    rc = ble_svc_gap_device_appearance_set(ESP_HID_APPEARANCE_KEYBOARD);
    if (rc != 0) {
        ESP_LOGW(TAG, "Nao foi possivel definir appearance GAP BLE: rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "GAP BLE configurado como teclado: appearance=0x%04x",
                 ESP_HID_APPEARANCE_KEYBOARD);
    }
}

static void ble_security_start(uint16_t conn_handle)
{
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc == 0 && desc.sec_state.encrypted) {
        ESP_LOGD(TAG, "Seguranca BLE ja esta ativa");
        return;
    }

    rc = ble_gap_security_initiate(conn_handle);
    if (rc == 0) {
        ESP_LOGI(TAG, "Seguranca BLE iniciada para conn_handle=%u", conn_handle);
    } else if (rc == BLE_HS_EALREADY) {
        ESP_LOGD(TAG, "Seguranca BLE ja estava em andamento");
    } else {
        ESP_LOGW(TAG, "Nao foi possivel iniciar seguranca BLE: rc=%d", rc);
    }
}

static void ble_connection_adopt(uint16_t conn_handle)
{
    if (s_ble_connected && s_ble_conn_handle == conn_handle) {
        return;
    }

    s_ble_connected = true;
    s_ble_conn_handle = conn_handle;
    ble_hid_keyboard_connection_set(conn_handle, true);
}

static int ble_gap_event_callback(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        bool link_active = event->connect.status == 0;

        if (!link_active) {
            struct ble_gap_conn_desc desc;
            link_active = ble_gap_conn_find(event->connect.conn_handle, &desc) == 0;
        }

        if (link_active) {
            ble_connection_adopt(event->connect.conn_handle);
            if (event->connect.status != 0) {
                ESP_LOGW(TAG,
                         "Conexao BLE ativa apesar do status inicial=%d; mantendo conn_handle=%u",
                         event->connect.status,
                         event->connect.conn_handle);
            }
            ESP_LOGI(TAG, "Bluetooth conectado; aguardando pareamento e notificacoes HID");
            ble_security_start(s_ble_conn_handle);
        } else {
            ESP_LOGW(TAG, "Falha na conexao BLE: status=%d", event->connect.status);
            if (!s_ble_connected) {
                ble_advertising_start();
            }
        }
        return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Bluetooth desconectado: motivo=%d", event->disconnect.reason);
        s_ble_connected = false;
        ble_hid_keyboard_connection_set(BLE_HS_CONN_HANDLE_NONE, false);
        s_ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        if (s_ble_report_queue != NULL) {
            xQueueReset(s_ble_report_queue);
        }
        ble_advertising_start();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG,
                 "BLE subscribe: conn=%u attr=%u reason=%u prev_notify=%u cur_notify=%u prev_indicate=%u cur_indicate=%u",
                 event->subscribe.conn_handle,
                 event->subscribe.attr_handle,
                 event->subscribe.reason,
                 event->subscribe.prev_notify,
                 event->subscribe.cur_notify,
                 event->subscribe.prev_indicate,
                 event->subscribe.cur_indicate);

        bool notify_enabled = event->subscribe.cur_notify ||
                              event->subscribe.cur_indicate;
        ble_connection_adopt(event->subscribe.conn_handle);
        if (ble_hid_keyboard_subscription_update(event->subscribe.attr_handle,
                                                  notify_enabled)) {
            if (ble_hid_keyboard_ready()) {
                ESP_LOGI(TAG, "Teclado BLE pronto para transmitir leituras");
                ble_security_start(s_ble_conn_handle);
            } else {
                ESP_LOGI(TAG, "Host BLE desabilitou o relatorio HID ativo");
            }
        }
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            struct ble_gap_conn_desc desc;
            ble_connection_adopt(event->enc_change.conn_handle);
            int rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
            if (rc == 0) {
                ESP_LOGI(TAG,
                         "Conexao BLE criptografada/pareada: encrypted=%u authenticated=%u bonded=%u key_size=%u",
                         desc.sec_state.encrypted,
                         desc.sec_state.authenticated,
                         desc.sec_state.bonded,
                         desc.sec_state.key_size);
            } else {
                ESP_LOGI(TAG, "Conexao BLE criptografada/pareada");
            }
        } else {
            ESP_LOGW(TAG, "Falha de seguranca BLE: status=%d", event->enc_change.status);
        }
        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        if (!event->notify_tx.indication) {
            ESP_LOGI(TAG, "BLE notify concluido: conn=%u attr=%u status=%d",
                     event->notify_tx.conn_handle,
                     event->notify_tx.attr_handle,
                     event->notify_tx.status);
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

    /* Pareamento simples para manter compatibilidade com Android e iOS. */
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

    ble_random_static_address_configure();

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
        s_ble_own_addr_type,
        NULL,
        BLE_HS_FOREVER,
        &adv_params,
        ble_gap_event_callback,
        NULL
    );

    if (rc == BLE_HS_EALREADY) {
        ESP_LOGD(TAG, "Advertising BLE ja estava ativo");
        return ESP_OK;
    }

    if (rc != 0) {
        ESP_LOGW(TAG, "Nao foi possivel iniciar anuncio BLE: rc=%d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Bluetooth anunciando como '%s'", BLE_DEVICE_NAME);
    return ESP_OK;
}

static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "Perfil de teclado HOGP local iniciado");
    ESP_ERROR_CHECK(ble_advertising_start());
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

    ESP_ERROR_CHECK(ble_hid_keyboard_service_init());
    ble_gap_identity_configure();

    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sync_cb = ble_on_sync;

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

        if (!s_ble_connected || !ble_hid_keyboard_ready()) {
            ESP_LOGW(TAG, "Leitura ignorada: nenhum celular/tablet BLE pronto");
            continue;
        }

        esp_err_t err = ESP_FAIL;
        for (int attempt = 0; attempt < BLE_SEND_RETRIES; ++attempt) {
            err = ble_hid_keyboard_input_send(report.data, sizeof(report.data));

            if (err == ESP_OK) {
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(BLE_SEND_RETRY_DELAY_MS));
        }

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Falha ao enviar relatorio BLE: %s", esp_err_to_name(err));
        } else if (keyboard_report_has_pressed_key(&report)) {
            ESP_LOGI(TAG,
                     "BLE HID report enviado: %02x %02x %02x %02x %02x %02x %02x %02x",
                     report.data[0],
                     report.data[1],
                     report.data[2],
                     report.data[3],
                     report.data[4],
                     report.data[5],
                     report.data[6],
                     report.data[7]);
        }

        /* iPadOS tende a ser menos tolerante a rajadas muito rápidas de notify. */
        if (keyboard_report_has_pressed_key(&report)) {
            vTaskDelay(pdMS_TO_TICKS(BLE_KEY_PRESS_DELAY_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(BLE_INTER_REPORT_DELAY_MS));
        }
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

    if (xQueueSend(s_ble_report_queue, &ble_report, pdMS_TO_TICKS(20)) != pdTRUE) {
        ESP_LOGW(TAG, "Fila BLE cheia; relatorio do leitor descartado");
    }

    bool has_pressed_key = keyboard_report_has_pressed_key(&ble_report);

    /*
     * Alguns hosts, especialmente iPadOS, sao mais rigorosos com o ciclo
     * key-down/key-up. Enviar um release extra apos cada tecla pressionada e
     * inofensivo quando o leitor ja manda release, e evita tecla presa quando
     * o leitor envia apenas o pressionamento.
     */
    if (has_pressed_key) {
        const ble_keyboard_report_t release_report = {0};
        if (xQueueSend(s_ble_report_queue, &release_report, pdMS_TO_TICKS(20)) != pdTRUE) {
            ESP_LOGW(TAG, "Fila BLE cheia; release HID descartado");
        }
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
