# OpenCode — ESP32-S3 USB Barcode Bridge

Configuração preparada para o firmware baseado em:

- ESP-IDF 5.5.4;
- C e CMake;
- `idf.py`;
- toolchain e Ninja gerenciados pelo ESP-IDF;
- NimBLE;
- USB Host HID;
- PowerShell 7;
- Git.

## Estrutura

```text
AGENTS.md
opencode.json

prompts\
  plan.txt
  build.txt
  review.txt

scripts\
  idf_env.ps1
  build.ps1
  ports.ps1
  flash.ps1
  monitor.ps1
  flash_monitor.ps1
  erase_flash.ps1
  size.ps1

.opencode\
  commands\
    build.md
    validate.md
    ports.md
    flash.md
    monitor.md
    flash-monitor.md
    erase-flash.md
    size.md

  skills\
    esp32-usb-ble-bridge\
      SKILL.md
    esp-idf-validation\
      SKILL.md
```

## Commands

```text
/build
/build reconfigure
/validate
/ports
/flash COM4
/monitor COM4
/flash-monitor COM4
/erase-flash COM4
/size
```

## Ambiente

Os scripts carregam e validam:

```text
D:\Espressif
D:\Espressif\frameworks\esp-idf-v5.5.4
D:\Espressif\python_env\idf5.5_py3.11_env
```

## Build

Normal:

```powershell
.\scripts\build.ps1
```

Com reconstrução de configuração:

```powershell
.\scripts\build.ps1 -Reconfigure
```

## Segurança operacional

Os scripts de gravação exigem uma porta COM explícita.

O comando de apagar flash foi mantido separado por ser consequencial para flash/NVS.

Build validado não equivale a teste físico do leitor USB ou do teclado BLE.