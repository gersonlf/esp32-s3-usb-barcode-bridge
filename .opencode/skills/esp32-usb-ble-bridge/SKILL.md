---
name: esp32-usb-ble-bridge
description: Preserva a arquitetura USB Host HID para BLE HID Keyboard deste firmware ESP32-S3.
compatibility: opencode
metadata:
  framework: esp-idf
  target: esp32s3
  platform: windows
  shell: powershell
---

# Regras centrais

Preserve:

- ESP-IDF 5.5.4;
- esp32s3;
- espressif/usb_host_hid 1.2.0;
- GPIO19 = D+;
- GPIO20 = D-;
- USB nativa em modo Host;
- nome BLE `Leitor QR ESP32`.

# BLE HID

A implementacao validada usa HOGP local em `main\ble_hid_keyboard.c`.

Preserve:

- um Input Report;
- 8 bytes;
- sem Report ID;
- Report Reference `{0x00, 0x01}`;
- sem Output Report;
- sem Boot Reports;
- sem External Report Reference para Battery Level;
- HID Information 0x0111 / country 0 / flags 0x02;
- leitura criptografada de Report Map/Reference;
- notificacao somente com link criptografado.

Nao substituir automaticamente por `esp_hidd_dev_init()` ou mapa HID generico.

# USB Host

- receba reports HID;
- nao bloqueie callbacks;
- use fila para encaminhar ao BLE;
- trate desconexao corretamente.

# Seguranca

Preserve:

- Security Mode 1 Level 2;
- bonding;
- sem MITM;
- Secure Connections.

Identidade validada:

`C4:CB:8F:DA:3D:35`

Mudanca de GATT/Report Map exige novo ciclo de identidade, erase/NVS, esquecimento do bond e teste fisico.

# Configuracao

Mudanca de sdkconfig/BLE/particao exige:

```powershell
.\scripts\build.ps1 -Reconfigure
```

# Hardware

Nao reutilize GPIO19/GPIO20.

Nao invente capacidade de alimentacao, VID/PID, layout de teclado ou porta COM.

Build nao equivale a teste fisico.