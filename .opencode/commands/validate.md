---
description: Valida as alteracoes atuais do firmware
agent: build
---

Carregue:

- `esp32-usb-ble-bridge`
- `esp-idf-validation`

Identifique o que mudou.

Use build normal ou `-Reconfigure` conforme as regras da skill.

Nao faca flash automaticamente.

Se depender de placa, leitor ou cliente BLE real e nao houver teste, informe:

`validacao fisica pendente`