# AGENTS.md — ESP32-S3 Leitor USB → BLE HID

> Documento de contexto e regras para agentes de código (Codex) trabalhando neste projeto.
>
> Atualizado em: 2026-07-30  
> Projeto: `ESP32 S3 Leitor USB`  
> Pasta usada no Windows: `D:\Developer\esp32-s3-usb-barcode-bridge`

---

## 1. Objetivo do projeto

Construir um adaptador com ESP32-S3 que receba dados de um leitor conectado por USB e retransmita as teclas por Bluetooth Low Energy como um teclado HID.

Fluxo esperado:

```text
Leitor USB-A HID
        ↓
ESP32-S3 funcionando como USB Host
        ↓
Leitura de relatórios HID de teclado
        ↓
ESP32-S3 funcionando como BLE HID Device
        ↓
Celular, tablet ou computador
```

Nome Bluetooth definido para o projeto:

```text
Leitor QR ESP32
```

O dispositivo de destino deve enxergar o ESP32-S3 como um teclado Bluetooth. Ao ler um código, o conteúdo deve aparecer no campo de texto ativo, normalmente seguido de Enter, conforme a configuração do leitor.

---

## 2. Regras obrigatórias para o Codex

1. Este projeto usa **ESP-IDF**, não Arduino.
2. A versão de referência é **ESP-IDF 5.5.4**.
3. O alvo é sempre:

   ```powershell
   idf.py set-target esp32s3
   ```

4. Não migrar para Arduino IDE, PlatformIO ou outro framework sem solicitação expressa.
5. Não trocar a versão do ESP-IDF ou do componente `usb_host_hid` sem justificar compatibilidade.
6. Não alterar a pinagem USB nativa:
   - GPIO19 = USB D+
   - GPIO20 = USB D-
7. Não usar GPIO19 e GPIO20 para outra função.
8. Não configurar a USB nativa simultaneamente como USB Device/TinyUSB e USB Host.
9. O leitor deve ser tratado inicialmente como **USB HID Keyboard**.
10. Toda alteração deve terminar com pelo menos:

    ```powershell
    idf.py build
    ```

11. Quando houver alteração em `sdkconfig.defaults`, alvo, Bluetooth ou tabela de partições, apagar o estado gerado e reconstruir:

    ```powershell
    Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
    Remove-Item -Force sdkconfig -ErrorAction SilentlyContinue
    idf.py set-target esp32s3
    idf.py build
    ```

12. Não substituir fontes funcionais inteiros por exemplos genéricos. Ler primeiro o conteúdo atual do repositório e fazer alterações pequenas e rastreáveis.
13. Não ocultar erros de compilação com remoção de componentes ou desativação de recursos necessários.
14. Não afirmar que o firmware foi validado no hardware sem realmente executar o teste na placa.
15. Ao concluir uma tarefa, informar:
    - arquivos modificados;
    - motivo de cada modificação;
    - comando de build executado;
    - resultado do build;
    - pendências de teste físico.

---

## 3. Hardware confirmado

### 3.1 Placa

Placa informada e mostrada no projeto:

```text
ESP32-S3 N16R8
```

Interpretação do módulo:

- `N16`: normalmente indica 16 MB de flash.
- `R8`: normalmente indica 8 MB de PSRAM.

A capacidade exata deve ser confirmada no modelo real da placa antes de alterar configurações de flash/PSRAM. Não habilitar opções apenas com base no nome comercial se o hardware não tiver sido identificado com segurança.

A placa possui duas portas USB-C.

Pela identificação feita nas fotos do projeto:

- USB-C de cima: USB nativa / USB-OTG;
- USB-C de baixo: UART/serial para gravação e monitor.

A orientação “cima/baixo” depende da mesma posição das fotos do projeto. Antes de orientar uma ligação física, conferir a serigrafia da placa.

### 3.2 Pinos USB nativos do ESP32-S3

| Sinal USB | GPIO do ESP32-S3 | Cor provável no cabo usado |
|---|---:|---|
| D+ | GPIO19 | Azul |
| D- | GPIO20 | Branco |
| VBUS | 5V | Vermelho |
| GND | GND | Preto |

Ligação definida para o cabo USB-A fêmea usado no teste:

```text
Vermelho → 5V
Preto    → GND
Branco   → GPIO20 / USB D-
Azul     → GPIO19 / USB D+
```

A convenção de cores não é garantia elétrica. Confirmar continuidade com multímetro sempre que possível.

### 3.3 Uso das portas durante o teste

Configuração prática recomendada:

- alimentar, gravar e abrir o monitor serial pela USB-C UART;
- ligar o leitor à USB Host usando:
  - a porta USB-OTG correta da placa; ou
  - os fios diretamente em 5V, GND, GPIO19 e GPIO20.

Não conectar simultaneamente outra fonte de dados USB nos mesmos GPIO19/GPIO20.

### 3.4 Alimentação do leitor

O ESP32-S3 precisa fornecer VBUS ao dispositivo USB Host.

Antes de alimentar o leitor pelo pino 5V da placa, conferir:

- tensão exigida pelo leitor;
- corrente de pico;
- limite do regulador, conector e trilhas da placa;
- existência de fonte externa;
- existência de GND comum.

Para um leitor USB HID comum de 5 V, pode ser necessário usar uma fonte regulada de 5 V externa com terra comum. Não assumir que a placa suporta a corrente do leitor.

#### Caso Honeywell MS7820

O Honeywell MS7820 apareceu nas conversas relacionadas, mas deve ser tratado como um caso separado:

- é um leitor de códigos 1D, não um leitor QR;
- pode usar diferentes cabos/interfaces;
- pode exigir sua fonte e cabo host corretos;
- não se deve presumir que qualquer cabo modular do MS7820 seja USB;
- não alimentar um MS7820 com 5 V sem confirmar a configuração e a documentação do conjunto cabo/interface/fonte.

---

## 4. Ambiente de desenvolvimento confirmado

Instalador usado:

```text
esp-idf-tools-setup-offline-5.5.4.exe
```

Sistema:

```text
Windows
```

Diretórios observados nos logs:

```text
D:\Espressif
D:\Espressif\frameworks\esp-idf-v5.5.4
D:\Espressif\python_env\idf5.5_py3.11_env
D:\Developer\esp32-s3-usb-barcode-bridge
```

Versões observadas:

```text
ESP-IDF v5.5.4
Python 3.11.2
Git 2.44.0.windows.1
GCC/G++ Xtensa 14.2.0
esptool.py 4.12.dev1
```

Componente gerenciado observado:

```text
espressif/usb_host_hid 1.2.0
```

---

## 5. Instalação do ESP-IDF Tools Offline 5.5.4

### 5.1 Instalação inicial

1. Executar:

   ```text
   esp-idf-tools-setup-offline-5.5.4.exe
   ```

2. Manter a instalação do ESP-IDF 5.5.4.
3. No computador usado neste projeto, os arquivos ficaram em:

   ```text
   D:\Espressif
   ```

4. Após concluir, abrir o atalho/terminal do ESP-IDF 5.5.4, preferencialmente o PowerShell fornecido pelo instalador.
5. Não usar um PowerShell comum sem importar o ambiente do ESP-IDF.

### 5.2 Validação do ambiente

No terminal do ESP-IDF:

```powershell
idf.py --version
```

Saída confirmada:

```text
ESP-IDF v5.5.4
```

Também é útil validar:

```powershell
python --version
git --version
where.exe idf.py
$env:IDF_PATH
```

O `IDF_PATH` deve apontar para o framework 5.5.4.

### 5.3 Acesso ao projeto

```powershell
D:
cd D:\Developer\esp32-s3-usb-barcode-bridge
idf.py --version
```

---

## 6. Estrutura conhecida do projeto

Estrutura definida anteriormente:

```text
esp32s3_usb_leitor_ble/
├── CMakeLists.txt
├── README.md
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml
    └── main.c
```

O agente deve verificar a árvore real antes de editar. Arquivos adicionais podem ter sido criados depois.

### Importante sobre os fontes históricos

O conteúdo integral do `main.c` original não ficou preservado no histórico recuperado. Ficaram confirmados:

- nome e estrutura do projeto;
- dependências;
- versão do ESP-IDF;
- funções/APIs principais usadas;
- objetivo USB Host → BLE HID;
- configurações necessárias;
- erro de link encontrado;
- correção aplicada.

Portanto:

- os fontes existentes no repositório local são a fonte principal;
- este `AGENTS.md` não deve ser usado para sobrescrever automaticamente um `main.c` atual;
- qualquer reconstrução deve partir do contrato funcional descrito neste documento e dos exemplos oficiais compatíveis com ESP-IDF 5.5.4.

---

## 7. Arquivos de configuração esperados

### 7.1 `CMakeLists.txt` da raiz

Base esperada:

```cmake
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp32s3_usb_leitor_ble)
```

### 7.2 `main/idf_component.yml`

Dependência confirmada:

```yaml
dependencies:
  idf: "5.5.4"
  espressif/usb_host_hid: "1.2.0"
```

Não atualizar automaticamente para uma versão mais nova.

Ao resolver dependências, o ESP-IDF gerou:

```text
dependencies.lock
managed_components/espressif__usb_host_hid
```

### 7.3 `main/CMakeLists.txt`

O registro deve incluir os fontes reais e as dependências necessárias.

Base de referência:

```cmake
idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        bt
        esp_hid
        nvs_flash
        usb
    PRIV_REQUIRES
        espressif__usb_host_hid
)
```

Se os componentes forem divididos em mais arquivos, adicionar cada `.c` no `SRCS`.

### 7.4 `sdkconfig.defaults`

Configurações obrigatórias já definidas para resolver o BLE HID:

```text
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_HID_SERVICE=y
CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME="Leitor QR ESP32"
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

A configuração decisiva para o erro de link foi:

```text
CONFIG_BT_NIMBLE_HID_SERVICE=y
```

A tabela de partições grande foi definida porque o firmware com USB Host e Bluetooth ultrapassou ou poderia ultrapassar o tamanho da partição padrão:

```text
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

Após mudar essas opções, apagar `sdkconfig` e `build`.

---

## 8. Arquitetura obrigatória do firmware

O firmware possui duas funções simultâneas:

1. **USB Host HID**
   - enumera o leitor;
   - abre a interface HID;
   - recebe os relatórios de teclado;
   - detecta desconexão;
   - mantém logs claros.

2. **BLE HID Keyboard**
   - anuncia como `Leitor QR ESP32`;
   - permite pareamento e bonding;
   - envia relatórios de teclado ao dispositivo conectado;
   - volta a anunciar após desconexão.

### 8.1 Inicialização geral

A função `app_main()` deve, em ordem segura:

1. inicializar NVS;
2. inicializar BLE/NimBLE;
3. criar o dispositivo BLE HID;
4. iniciar advertising;
5. instalar a biblioteca USB Host;
6. instalar o driver HID Host;
7. iniciar as tarefas de eventos;
8. criar fila entre callback USB e envio BLE.

### 8.2 NVS

Padrão esperado:

```c
esp_err_t ret = nvs_flash_init();

if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
    ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
}

ESP_ERROR_CHECK(ret);
```

O NVS também pode manter dados de bonding BLE.

### 8.3 USB Host

APIs confirmadas para a arquitetura:

```c
usb_host_install(...)
usb_host_lib_handle_events(...)
hid_host_install(...)
hid_host_device_open(...)
hid_host_device_start(...)
hid_host_device_get_raw_input_report_data(...)
hid_host_device_close(...)
```

Eventos esperados:

```c
HID_HOST_DRIVER_EVENT_CONNECTED
HID_HOST_INTERFACE_EVENT_INPUT_REPORT
HID_HOST_INTERFACE_EVENT_DISCONNECTED
```

Nomes podem variar levemente conforme a versão exata do componente. Usar a API presente em `managed_components/espressif__usb_host_hid` versão 1.2.0, e não copiar cegamente a API de uma versão mais nova.

### 8.4 BLE HID

APIs confirmadas:

```c
esp_hid_gap_init(...)
esp_hid_ble_gap_adv_init(...)
esp_hid_ble_gap_adv_start(...)
esp_hidd_dev_init(...)
esp_hidd_dev_input_set(...)
esp_hidd_dev_connected(...)
```

Inicialização central:

```c
esp_hidd_dev_init(
    &ble_hid_config,
    ESP_HID_TRANSPORT_BLE,
    ble_hidd_event_callback,
    &ble_hid_dev
);
```

Envio de um relatório de teclado:

```c
esp_hidd_dev_input_set(
    ble_hid_dev,
    0,      // índice do report map
    1,      // report ID do teclado
    report,
    8
);
```

O descritor BLE de teclado precisa ser compatível com um relatório boot keyboard de 8 bytes:

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

### 8.5 Ligação lógica USB → BLE

A estratégia preferida é transmitir os códigos HID, evitando converter USB HID → ASCII → USB HID novamente.

Fluxo:

```text
USB report recebido
    ↓
validar tamanho e tipo
    ↓
normalizar para relatório de teclado de 8 bytes
    ↓
colocar em fila
    ↓
tarefa BLE envia key-down
    ↓
tarefa BLE envia key-release
```

Relatório de liberação:

```c
uint8_t release[8] = {0};
esp_hidd_dev_input_set(ble_hid_dev, 0, 1, release, sizeof(release));
```

Alguns leitores já enviam relatórios de key-down e key-up. Não duplicar a liberação sem verificar o comportamento real.

### 8.6 Fila e concorrência

Não fazer operações bloqueantes dentro dos callbacks do USB.

Usar uma fila FreeRTOS, por exemplo:

```c
typedef struct {
    uint8_t data[8];
    size_t length;
} keyboard_report_t;
```

Comportamento:

- callback USB copia o relatório para a fila;
- tarefa BLE consome;
- se BLE não estiver conectado, registrar e descartar ou manter uma fila curta;
- nunca permitir crescimento ilimitado;
- proteger ponteiros de dispositivo e estado de conexão.

### 8.7 Buffer do código lido

Pode existir um `barcode buffer` apenas para log e diagnóstico.

Regras:

- acumular caracteres até Enter;
- limitar tamanho;
- sempre terminar a string com `\0`;
- limpar após Enter ou timeout;
- o encaminhamento BLE não deve depender da conversão para texto quando for possível retransmitir o relatório HID diretamente.

### 8.8 Bonding BLE

A configuração histórica incluiu:

```c
ble_hs_cfg.sm_bonding = 1;
```

O projeto deve permitir reconexão com o computador/celular já pareado.

Ao depurar pareamento antigo:

- remover o dispositivo no computador/celular;
- apagar NVS da placa quando necessário:

  ```powershell
  idf.py -p COMx erase-flash
  ```

- gravar novamente.

---

## 9. Estado BLE e eventos

A callback BLE deve tratar pelo menos:

```c
ESP_HIDD_START_EVENT
ESP_HIDD_CONNECT_EVENT
ESP_HIDD_DISCONNECT_EVENT
ESP_HIDD_PROTOCOL_MODE_EVENT
ESP_HIDD_OUTPUT_EVENT
ESP_HIDD_STOP_EVENT
```

Comportamento:

- `START`: iniciar advertising;
- `CONNECT`: marcar `ble_connected = true`;
- `DISCONNECT`: marcar `ble_connected = false` e reiniciar advertising;
- `PROTOCOL_MODE`: registrar Boot/Report;
- `OUTPUT`: registrar LEDs do teclado, se aplicável;
- `STOP`: registrar encerramento.

Nunca chamar `esp_hidd_dev_input_set()` com ponteiro de dispositivo inválido.

---

## 10. Histórico de compilação e erro corrigido

O projeto foi configurado corretamente para:

```text
IDF_TARGET=esp32s3
```

O ESP-IDF processou:

```text
espressif/usb_host_hid (1.2.0)
idf (5.5.4)
```

O bootloader foi gerado, mas o link do aplicativo falhou com:

```text
undefined reference to `esp_ble_hidd_dev_init`
```

Origem informada pelo linker:

```text
esp-idf/esp_hid/libesp_hid.a(esp_hidd.c.obj)
```

Erro final:

```text
collect2.exe: error: ld returned 1 exit status
ninja: build stopped: subcommand failed
```

### Causa

O projeto chamava:

```c
esp_hidd_dev_init(...)
```

com transporte BLE/NimBLE, mas o serviço HID do NimBLE não estava habilitado no `sdkconfig`.

### Correção obrigatória

Adicionar em `sdkconfig.defaults`:

```text
CONFIG_BT_NIMBLE_HID_SERVICE=y
```

Também manter:

```text
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

Depois executar:

```powershell
cd D:\Developer\esp32-s3-usb-barcode-bridge

Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
Remove-Item -Force sdkconfig -ErrorAction SilentlyContinue

idf.py set-target esp32s3
idf.py build
```

Não tentar resolver esse erro removendo `esp_hidd_dev_init()` ou trocando o projeto para Bluetooth clássico.

### 10.1 Validação física registrada em 2026-07-30

Após alinhar os fontes para uso exclusivo em:

```text
D:\Developer\esp32-s3-usb-barcode-bridge
```

foi executado build com ESP-IDF 5.5.4 usando:

```powershell
idf.py -B build_d set-target esp32s3
idf.py -B build_d build
```

Resultado do build:

```text
Project build complete.
esp32_s3_usb_barcode_bridge.bin binary size 0x8b6e0 bytes.
Smallest app partition is 0x177000 bytes.
0xeb920 bytes (63%) free.
```

O firmware foi gravado na placa e o usuário confirmou que o conjunto ficou funcionando. Essa confirmação cobre o teste prático básico de firmware gravado na placa, mas detalhes como VID/PID do leitor, corrente medida do leitor, modelo exato da placa e logs completos de enumeração USB ainda não foram registrados no repositório.

---

## 11. Build, gravação e monitor

### 11.1 Selecionar o alvo

```powershell
cd D:\Developer\esp32-s3-usb-barcode-bridge
idf.py set-target esp32s3
```

### 11.2 Compilar

```powershell
idf.py build
```

### 11.3 Descobrir a porta COM

```powershell
Get-CimInstance Win32_SerialPort |
    Select-Object DeviceID, Name
```

Também pode ser conferida no Gerenciador de Dispositivos.

### 11.4 Gravar e abrir monitor

```powershell
idf.py -p COMx flash monitor
```

Substituir `COMx` pela porta real, por exemplo:

```powershell
idf.py -p COM7 flash monitor
```

### 11.5 Apenas monitor

```powershell
idf.py -p COMx monitor
```

Sair do monitor:

```text
Ctrl + ]
```

### 11.6 Modo download manual

Quando a gravação não iniciar:

1. segurar `BOOT`;
2. pressionar e soltar `RESET`;
3. soltar `BOOT`;
4. repetir o comando de flash.

A sequência pode variar ligeiramente conforme a placa.

---

## 12. Procedimento de teste físico

### Etapa 1 — Testar somente o ESP32

1. Ligar a USB-C UART ao computador.
2. Abrir:

   ```powershell
   idf.py -p COMx monitor
   ```

3. Confirmar boot sem reset contínuo.
4. Confirmar logs de inicialização BLE e USB Host.

### Etapa 2 — Testar BLE

1. Procurar no computador/celular:

   ```text
   Leitor QR ESP32
   ```

2. Parear.
3. Abrir um editor de texto.
4. Confirmar no monitor:

   ```text
   BLE HID iniciado
   Advertising iniciado
   BLE conectado
   ```

### Etapa 3 — Ligar o leitor USB

Ligação direta:

```text
Leitor VBUS / vermelho → 5V
Leitor GND / preto     → GND
Leitor D- / branco     → GPIO20
Leitor D+ / azul       → GPIO19
```

### Etapa 4 — Conferir enumeração

Logs desejados, mesmo que o texto exato seja diferente:

```text
USB Host iniciado
Dispositivo USB conectado
VID: xxxx
PID: xxxx
Interface HID encontrada
HID Keyboard encontrado
Recepção de relatórios iniciada
```

### Etapa 5 — Ler um código

1. Manter um campo de texto ativo no dispositivo pareado.
2. Ler um código.
3. Conferir:
   - relatório USB recebido;
   - relatório enviado por BLE;
   - caracteres digitados;
   - Enter final, quando configurado no leitor.

---

## 13. Diagnóstico

### 13.1 Leitor acende, mas não enumera

Verificar:

1. GND comum;
2. 5 V estável no conector do leitor;
3. GPIO19 ligado ao D+;
4. GPIO20 ligado ao D-;
5. continuidade do cabo;
6. corrente disponível;
7. leitor configurado para USB HID Keyboard;
8. porta/PHY USB em modo Host;
9. nenhum outro periférico usando GPIO19/GPIO20.

Como teste diagnóstico, D+ e D- podem ser conferidos com multímetro/oscilloscope. Não inverter permanentemente sem confirmar o cabo.

### 13.2 USB conecta e desconecta

Possíveis causas:

- queda de tensão;
- fonte insuficiente;
- cabo ruim;
- mau contato;
- falta de VBUS estável;
- ruído;
- callback bloqueando a pilha;
- erro no tratamento de interface/desconexão.

### 13.3 BLE não aparece

Verificar:

```text
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_HID_SERVICE=y
CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME="Leitor QR ESP32"
```

Depois apagar `sdkconfig` e `build`.

Também verificar se o advertising é iniciado após `ESP_HIDD_START_EVENT`.

### 13.4 BLE conecta, mas não digita

Verificar:

- estado `ble_connected`;
- `ble_hid_dev` válido;
- map index;
- report ID;
- tamanho do relatório;
- relatório de liberação;
- descritor HID;
- modo Boot/Report;
- se o destino está com campo de texto ativo.

### 13.5 Caracteres errados

O relatório HID representa teclas físicas, não caracteres universais. O resultado depende do layout do teclado configurado no dispositivo de destino.

Exemplo:

- leitor envia códigos HID de teclado US;
- computador está configurado como ABNT2;
- símbolos podem sair diferentes.

Para códigos numéricos, normalmente não há problema. Para letras e símbolos, definir qual layout será suportado.

### 13.6 Build ainda mostra `esp_ble_hidd_dev_init`

Executar exatamente:

```powershell
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
Remove-Item -Force sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32s3
idf.py build
```

Depois conferir no `sdkconfig` gerado:

```text
CONFIG_BT_NIMBLE_HID_SERVICE=y
```

---

## 14. Logs mínimos que o firmware deve produzir

Na inicialização:

```text
Projeto: esp32s3_usb_leitor_ble
ESP-IDF: 5.5.4
Alvo: esp32s3
Inicializando NVS
Inicializando BLE HID
BLE HID iniciado
Advertising iniciado
Inicializando USB Host
USB Host iniciado
HID Host instalado
Aguardando leitor USB
```

Na conexão USB:

```text
USB conectado
VID=....
PID=....
Interface=....
Protocolo=Keyboard/Boot/Report
```

Na leitura:

```text
USB HID report: ...
BLE HID report enviado: ...
```

Na desconexão:

```text
Leitor USB desconectado
Interface HID fechada
```

Evitar imprimir dados sensíveis em produção. Para depuração, permitir nível de log configurável.

---

## 15. Contrato do `main.c`

O arquivo deve conter ou delegar para outros módulos:

```c
void app_main(void);
```

Inicialização NVS:

```c
static void nvs_init_or_erase(void);
```

BLE:

```c
static esp_err_t ble_hid_init(void);
static void ble_hidd_event_callback(
    void *handler_args,
    esp_event_base_t base,
    int32_t id,
    void *event_data
);
static void ble_host_task(void *param);
static esp_err_t ble_send_keyboard_report(
    const uint8_t *report,
    size_t length
);
```

USB Host:

```c
static esp_err_t usb_host_init(void);
static void usb_lib_task(void *arg);
static void hid_driver_event_callback(
    hid_host_device_handle_t hid_device_handle,
    const hid_host_driver_event_t event,
    void *arg
);
static void hid_interface_event_callback(
    hid_host_device_handle_t hid_device_handle,
    const hid_host_interface_event_t event,
    void *arg
);
```

Fila:

```c
static QueueHandle_t keyboard_report_queue;
static void ble_sender_task(void *arg);
```

Os nomes reais podem ser diferentes, mas as responsabilidades devem permanecer separadas.

---

## 16. Critérios de aceitação

Uma versão só está concluída quando:

- [ ] compila com ESP-IDF 5.5.4;
- [ ] usa `idf.py set-target esp32s3`;
- [ ] usa `espressif/usb_host_hid` 1.2.0;
- [ ] mantém GPIO19 como D+;
- [ ] mantém GPIO20 como D-;
- [ ] instala USB Host;
- [ ] detecta conexão do leitor;
- [ ] identifica interface HID de teclado;
- [ ] recebe relatórios;
- [ ] anuncia `Leitor QR ESP32`;
- [ ] permite pareamento BLE;
- [ ] envia teclas pelo BLE HID;
- [ ] trata desconexão USB;
- [ ] trata desconexão BLE;
- [ ] não bloqueia callbacks;
- [ ] não causa reset por watchdog;
- [ ] informa o resultado do build;
- [ ] registra o que ainda depende de teste físico.

---

## 17. Pontos ainda não confirmados

O agente não deve inventar estas informações:

1. fabricante e revisão exata da placa ESP32-S3 N16R8;
2. porta COM fixa;
3. corrente máxima segura do pino 5V da placa;
4. modelo/interface exata do leitor que será usado no produto final;
5. VID/PID do leitor;
6. se o leitor envia boot protocol puro ou relatório HID proprietário;
7. necessidade de suporte a múltiplas interfaces HID;
8. layout de teclado final: US, ABNT2 ou outro;
9. conteúdo integral do primeiro `main.c`;
10. resultado final do build após a correção, caso não esteja registrado no repositório.

Esses pontos devem ser descobertos por inspeção do hardware, logs e fontes atuais.

---

## 18. Referências técnicas para o agente

Preferir documentação e código oficial da Espressif compatíveis com a versão usada.

Referências principais:

```text
ESP-IDF v5.5.4:
https://github.com/espressif/esp-idf/tree/v5.5.4

Exemplo oficial BLE HID Device:
https://github.com/espressif/esp-idf/tree/v5.5.4/examples/bluetooth/esp_hid_device

Componente USB Host HID:
https://components.espressif.com/components/espressif/usb_host_hid

Repositório oficial esp-usb:
https://github.com/espressif/esp-usb
```

Não copiar APIs do branch `master` sem comparar com:

```text
managed_components/espressif__usb_host_hid
```

Esse diretório local corresponde à versão realmente resolvida pelo projeto.

---

## 19. Comandos rápidos

### Limpar, configurar e compilar

```powershell
cd D:\Developer\esp32-s3-usb-barcode-bridge

Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
Remove-Item -Force sdkconfig -ErrorAction SilentlyContinue

idf.py set-target esp32s3
idf.py build
```

### Gravar e monitorar

```powershell
idf.py -p COMx flash monitor
```

### Apenas apagar flash

```powershell
idf.py -p COMx erase-flash
```

### Apenas monitorar

```powershell
idf.py -p COMx monitor
```

### Ver tamanho

```powershell
idf.py size
idf.py size-components
```

---

## 20. Resumo operacional para o Codex

Trabalhe sempre sobre os fontes atuais da pasta:

```text
D:\Developer\esp32-s3-usb-barcode-bridge
```

Preserve:

```text
ESP-IDF 5.5.4
ESP32-S3
usb_host_hid 1.2.0
GPIO19 = D+
GPIO20 = D-
BLE name = Leitor QR ESP32
NimBLE HID Service habilitado
Single App Large habilitado
```

O objetivo final é simples:

```text
receber relatórios de teclado do leitor USB
e retransmiti-los como teclado BLE
```

Qualquer mudança que não contribua diretamente para esse fluxo deve ser evitada.
