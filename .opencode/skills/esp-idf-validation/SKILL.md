---
name: esp-idf-validation
description: Padroniza build, flash, monitor e validacao deste projeto ESP-IDF.
compatibility: opencode
metadata:
  framework: esp-idf
  target: esp32s3
  platform: windows
  shell: powershell
---

# Build normal

```powershell
.\scripts\build.ps1
```

# Build com reconfiguracao

```powershell
.\scripts\build.ps1 -Reconfigure
```

Use quando houver mudanca em:

- `sdkconfig.defaults`;
- alvo;
- Bluetooth/NimBLE;
- tabela de particoes.

# Portas

```powershell
.\scripts\ports.ps1
```

Nao invente porta COM.

# Flash

```powershell
.\scripts\flash.ps1 -Port COMx
```

Flash nao e obrigatorio para toda alteracao.

# Monitor

```powershell
.\scripts\monitor.ps1 -Port COMx
```

# Flash e monitor

```powershell
.\scripts\flash_monitor.ps1 -Port COMx
```

# Erase

```powershell
.\scripts\erase_flash.ps1 -Port COMx
```

Nao execute erase por rotina.

# Tamanho

```powershell
.\scripts\size.ps1
```

# Reuso

Nao repita build que ja passou depois da ultima alteracao relevante.

O REVIEW nao deve recompilar apenas para obter independencia.

# Validacao fisica

Build nao comprova USB real, BLE real, pareamento, iOS, Android ou alimentacao.

Quando nao houver teste real:

`validacao fisica pendente`