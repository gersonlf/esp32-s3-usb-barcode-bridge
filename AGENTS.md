# AGENTS.md

Instruções permanentes para agentes que trabalham neste repositório.

# PROJETO

Raiz oficial no ambiente Windows atual:

`D:\Developer\esp32-s3-usb-barcode-bridge`

Objetivo:

```text
Leitor USB HID Keyboard
        ↓
ESP32-S3 como USB Host
        ↓
Relatórios HID de teclado
        ↓
ESP32-S3 como BLE HID Keyboard
        ↓
Celular / tablet / computador
```

Nome Bluetooth esperado:

`Leitor QR ESP32`

# REGRAS DE ESCOPO

- Trabalhe somente dentro do repositório atual, salvo instrução explícita.
- Leia os fontes atuais antes de editar.
- Faça a menor alteração necessária.
- Não refatore ou modernize fora do escopo.
- Não substitua fontes atuais por exemplos genéricos.
- Não use memória de sessões anteriores como substituto de leitura necessária.
- Não invente APIs, pinos, porta COM, comportamento do leitor ou configuração de hardware.

# AMBIENTE

Sistema:

Windows

Shell:

PowerShell 7 (`pwsh`)

Framework obrigatório:

`ESP-IDF 5.5.4`

Alvo:

`esp32s3`

Componente gerenciado de referência:

`espressif/usb_host_hid 1.2.0`

Ambiente confirmado:

```text
D:\Espressif
D:\Espressif\frameworks\esp-idf-v5.5.4
D:\Espressif\python_env\idf5.5_py3.11_env
```

Não trocar framework, versão do ESP-IDF ou versão de `usb_host_hid` sem solicitação explícita e análise de compatibilidade.

# TERMINAL E PERMISSÕES

Use sintaxe PowerShell.

Não use Bash, Git Bash ou `sh` sem necessidade explícita.

Comandos de leitura/inspeção não exigem confirmação textual quando a configuração do OpenCode permitir execução direta.

Exemplos:

- `git status`
- `git diff`
- `git log`
- `git show`
- `git branch`
- `git remote`
- `Get-Content`
- `Get-ChildItem`
- `Select-String`
- `Test-Path`
- `Get-Item`
- `Get-CimInstance Win32_SerialPort`
- `idf.py --version`
- `python --version`
- `git --version`
- `where.exe idf.py`

Quando o usuário já tiver autorizado uma ação, não peça confirmação novamente em texto. Se a ferramenta estiver configurada como `ask`, deixe somente a aprovação da ferramenta ocorrer.

# REUSO DE RESULTADOS

Resultados obtidos nesta mesma tarefa permanecem válidos enquanto nenhuma operação posterior os invalidar.

Não repita por rotina:

- leituras;
- buscas;
- `git status`;
- `git diff`;
- versão do ESP-IDF;
- builds;
- flash;
- monitor;
- testes.

Repita somente quando houver alteração posterior, resultado incompleto, dúvida concreta ou necessidade de diagnóstico.

# HARDWARE CRÍTICO

Placa de referência:

`ESP32-S3 N16R8`

A capacidade exata de flash/PSRAM deve ser confirmada no hardware real antes de alterar configurações relacionadas.

USB nativa:

- GPIO19 = USB D+
- GPIO20 = USB D-

Regras:

- não usar GPIO19 ou GPIO20 para outra função;
- não configurar simultaneamente a USB nativa como USB Device/TinyUSB e USB Host;
- tratar inicialmente o leitor como USB HID Keyboard;
- não presumir que o pino 5 V da placa suporta qualquer leitor;
- não afirmar corrente, cabo ou capacidade elétrica sem confirmação.

# ARQUITETURA DO FIRMWARE

O firmware preserva duas funções simultâneas.

## USB Host HID

- enumerar o leitor;
- abrir a interface HID;
- receber relatórios de teclado;
- detectar desconexão;
- fechar a interface corretamente;
- manter logs úteis.

## BLE HID Keyboard

- anunciar como `Leitor QR ESP32`;
- permitir pareamento e bonding;
- enviar relatórios de teclado;
- voltar a anunciar após desconexão.

A estratégia preferida é encaminhar relatórios HID, evitando conversões desnecessárias.

Não execute operações bloqueantes em callbacks USB.

Use fila FreeRTOS ou mecanismo equivalente entre recepção USB e envio BLE.

# CONTRATO BLE HID VALIDADO

A implementação validada usa HOGP local em:

`main\ble_hid_keyboard.c`

Não substituir automaticamente pelo wrapper `esp_hidd_dev_init()` nem por exemplo genérico com Report ID 1.

Preserve:

- um único Input Report;
- exatamente 8 bytes;
- Report Protocol sem Report ID;
- Report Reference `{0x00, 0x01}`;
- sem Output Report;
- sem Boot Keyboard Input/Output no serviço BLE;
- HID Information `0x0111`;
- country code `0x00`;
- flags `0x02`;
- Report Map e Report Reference com leitura criptografada;
- notificações do Input Report somente com link criptografado;
- Battery Service e Device Information Service como serviços independentes;
- sem External Report Reference para Battery Level;
- envio por `ble_gatts_notify_custom()` com os 8 bytes, sem prefixo de Report ID.

Formato:

```text
Byte 0: modificadores
Byte 1: reservado
Byte 2: tecla 1
Byte 3: tecla 2
Byte 4: tecla 3
Byte 5: tecla 4
Byte 6: tecla 5
Byte 7: tecla 6
```

Não reintroduzir Report ID, Output Report, Boot Report ou External Report Reference sem necessidade funcional comprovada e novo teste físico em iOS e Android.

# SEGURANÇA BLE

Configuração validada:

- Security Mode 1 Level 2;
- conexão criptografada;
- bonding;
- sem MITM;
- Secure Connections habilitado.

Identidade BLE random static atualmente validada:

`C4:CB:8F:DA:3D:35`

Ao alterar banco GATT ou Report Map:

1. usar nova identidade random static;
2. apagar flash/NVS;
3. esquecer o dispositivo antigo nos clientes;
4. gravar novamente;
5. repetir teste físico em iOS e Android.

Não execute esse procedimento por rotina.

# CONFIGURAÇÃO ESP-IDF

Preserve em `sdkconfig.defaults`, quando presentes:

```text
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_HID_SERVICE=y
CONFIG_BT_NIMBLE_SECURITY_ENABLE=y
CONFIG_BT_NIMBLE_SM_LVL=2
CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME="Leitor QR ESP32"
CONFIG_BT_NIMBLE_SVC_GAP_APPEARANCE=0x03C1
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

`CONFIG_BT_NIMBLE_HID_SERVICE=y` é obrigatória.

Quando houver alteração em:

- `sdkconfig.defaults`;
- alvo;
- Bluetooth/NimBLE;
- tabela de partições;

faça reconstrução limpa:

```powershell
.\scripts\build.ps1 -Reconfigure
```

# DEPENDÊNCIAS E APIS

`main\idf_component.yml` deve permanecer compatível com:

```yaml
dependencies:
  idf: "5.5.4"
  espressif/usb_host_hid: "1.2.0"
```

Use a API realmente presente em:

`managed_components\espressif__usb_host_hid`

Não copie cegamente APIs de versões mais novas.

# EDIÇÃO

- Faça alterações pequenas e localizadas.
- Preserve indentação, encoding, quebras de linha, comentários, diretivas e estilo.
- Não reescreva arquivos inteiros sem necessidade.
- Em arquivos grandes, localize primeiro símbolos e trechos.
- Depois de editar, releia apenas a região necessária.
- Se uma edição falhar, releia o conteúdo atual antes de tentar novamente.

# BUILD

Toda alteração que possa afetar o firmware deve terminar com build real.

Build normal:

```powershell
.\scripts\build.ps1
```

Build limpo após mudança estrutural/configuração:

```powershell
.\scripts\build.ps1 -Reconfigure
```

Não declare sucesso sem saída real do comando.

Build bem-sucedido não prova funcionamento no hardware.

# FLASH E MONITOR

Listar portas:

```powershell
.\scripts\ports.ps1
```

Gravar:

```powershell
.\scripts\flash.ps1 -Port COMx
```

Monitor:

```powershell
.\scripts\monitor.ps1 -Port COMx
```

Gravar e monitorar:

```powershell
.\scripts\flash_monitor.ps1 -Port COMx
```

Apagar flash:

```powershell
.\scripts\erase_flash.ps1 -Port COMx
```

Não invente porta COM.

Apagar flash/NVS é consequencial. Execute somente quando solicitado ou quando fizer parte de procedimento técnico já autorizado.

# VALIDAÇÃO FÍSICA

Testes físicos podem envolver:

- enumeração USB;
- leitor real;
- alimentação;
- pareamento;
- bonding;
- criptografia;
- assinatura do Input Report;
- digitação BLE;
- iOS;
- Android;
- desconexão/reconexão.

Quando não forem executados, informe:

`validação física pendente`

Não invente resultado de hardware.

# GIT

Não faça commit ou push sem autorização explícita.

Não descarte alterações do usuário sem autorização.

Não use por iniciativa própria:

- `git reset`;
- `git reset --hard`;
- `git restore`;
- `git clean`;
- `git rebase`;
- force push;
- exclusão de branch.

# VERDADE DOS RESULTADOS

Resultados reais das ferramentas são a única fonte válida para afirmar:

- build;
- flash;
- monitor;
- teste;
- Git;
- arquivos;
- erros;
- funcionamento.

Se algo não foi executado, diga claramente:

`não foi executado`.

# RESPOSTA FINAL

Informe objetivamente:

- o que foi alterado;
- arquivos modificados;
- build realmente executado;
- resultado real;
- flash/monitor realmente executado, quando houver;
- validação física pendente, quando houver;
- qualquer falha real.