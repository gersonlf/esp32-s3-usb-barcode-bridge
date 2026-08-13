# ESP32-S3: leitor USB-A para teclado Bluetooth LE

Projeto para o **ESP32-S3-N16R8** atuar como adaptador:

```text
Leitor de codigo de barras/QR USB HID
                |
        ESP32-S3 USB Host
                |
       Teclado Bluetooth LE HID
                |
         Celular ou tablet
```

O dispositivo Bluetooth anunciado pelo firmware se chama:

```text
Leitor QR ESP32
```

## Requisitos

- ESP-IDF **5.5.4**.
- Alvo ESP-IDF **esp32s3**.
- Componente gerenciado `espressif/usb_host_hid` **1.2.0**.
- ESP32-S3-N16R8.
- Leitor configurado como **USB HID Keyboard**.
- Conector USB-A femea.
- Fonte regulada de 5 V capaz de alimentar o ESP32-S3 e o leitor.

## Ligacoes

| USB-A femea | ESP32-S3 / fonte |
|---|---|
| VBUS | +5 V da fonte |
| D+ | GPIO19, de preferencia com 22 ohms em serie |
| D- | GPIO20, de preferencia com 22 ohms em serie |
| GND | GND comum |

Nao alimente o leitor pelo pino de 3,3 V. Coloque um capacitor de 470 uF e um
de 100 nF proximos ao conector USB-A.

## Compilar

No Windows, use o ESP-IDF 5.5.4 instalado em `D:\Espressif` e compile a partir
dos fontes deste repositorio:

```powershell
$env:IDF_TOOLS_PATH='D:\Espressif'
$env:IDF_PATH='D:\Espressif\frameworks\esp-idf-v5.5.4'
$env:IDF_PYTHON_ENV_PATH='D:\Espressif\python_env\idf5.5_py3.11_env'
. 'D:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1'

cd D:\Developer\esp32-s3-usb-barcode-bridge
idf.py -B build_d set-target esp32s3
idf.py -B build_d build
```

O build validado gerou:

```text
build_d\esp32_s3_usb_barcode_bridge.bin
```

## Gravar na placa

Descubra a porta serial:

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID, Name
```

Grave e abra o monitor, substituindo `COMx` pela porta real:

```powershell
idf.py -B build_d -p COMx flash monitor
```

Normalmente nao e preciso apertar botoes. Se a gravacao falhar por timeout ou
`Failed to connect`, entre no modo download manual:

1. segure `BOOT`;
2. pressione e solte `RST`/`RESET`;
3. solte `BOOT`;
4. rode novamente o comando `flash monitor`.

Use a porta USB-C de UART/serial para gravacao e monitor. A USB nativa/OTG usa
os mesmos GPIOs 19/20 do leitor.

## Uso

1. Ligue o ESP32-S3 e o leitor.
2. No celular/tablet/computador, abra as configuracoes de Bluetooth.
3. Pareie com **Leitor QR ESP32**.
4. Abra um campo de texto.
5. Leia um codigo.

O projeto repassa diretamente os relatorios HID do leitor. Assim, o `Enter`
configurado no leitor tambem e enviado ao celular/tablet/computador.

## Perfil BLE validado

O firmware foi validado funcionando como teclado no **iOS** e no
**Samsung Galaxy A54**.

A compatibilidade depende do perfil HOGP minimo implementado em
`main/ble_hid_keyboard.c`:

- um unico Input Report de teclado com 8 bytes;
- Report Protocol sem Report ID;
- Report Reference com ID `0` e tipo Input;
- Report Map e Report Reference acessiveis somente com link criptografado;
- sem Boot Report, Output Report ou referencia externa para Battery Level;
- pareamento com bonding, criptografia Level 2 e sem MITM.

Essas caracteristicas nao devem ser substituidas pelo Report ID 1 do exemplo
generico sem repetir os testes em iOS e Android. A versao anterior conectava e
enviava notificacoes BLE, mas o iOS nao reconhecia os dados como teclado.

## Limitacao inicial

Esta versao aceita leitores que se apresentam como **HID Boot Keyboard**, o modo
mais comum para leitores configurados como teclado USB. Se o log mostrar que a
subclasse nao e `BOOT`, sera necessario adicionar o parser do Report Descriptor
especifico do leitor.

## Depuracao

Para programar e acompanhar o monitor sem ocupar os GPIOs 19/20, prefira uma
placa que tenha conversor USB-UART separado. O USB nativo do ESP32-S3 usa os
mesmos pinos do leitor.

## Estado validado

Em 2026-07-30, o firmware foi compilado com ESP-IDF 5.5.4 usando `build_d`,
gravado na placa e confirmado funcionando no iOS e no Samsung Galaxy A54.
