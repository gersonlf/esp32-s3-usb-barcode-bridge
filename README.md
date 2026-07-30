# ESP32-S3: leitor USB-A para teclado Bluetooth LE

Projeto para o **ESP32-S3-N16R8** atuar como adaptador:

```text
Leitor de código de barras/QR USB HID
                ↓
        ESP32-S3 USB Host
                ↓
       Teclado Bluetooth LE HID
                ↓
         Celular ou tablet
```

## Requisitos

- ESP-IDF **5.5.x**.
- ESP32-S3-N16R8.
- Leitor configurado como **USB HID Keyboard**.
- Conector USB-A fêmea.
- Fonte regulada de 5 V capaz de alimentar o ESP32-S3 e o leitor.

## Ligações

| USB-A fêmea | ESP32-S3 / fonte |
|---|---|
| VBUS | +5 V da fonte |
| D- | GPIO19, de preferência com 22 Ω em série |
| D+ | GPIO20, de preferência com 22 Ω em série |
| GND | GND comum |

Não alimente o leitor pelo pino de 3,3 V. Coloque um capacitor de 470 µF e um de 100 nF próximos ao conector USB-A.

## Compilar

Abra o terminal do ESP-IDF dentro da pasta do projeto:

```bash
idf.py set-target esp32s3
idf.py fullclean
idf.py build
idf.py -p COM5 flash monitor
```

No Linux, a porta normalmente será algo como `/dev/ttyUSB0` ou `/dev/ttyACM0`.

## Uso

1. Ligue o ESP32-S3 e o leitor.
2. No celular/tablet, abra as configurações de Bluetooth.
3. Pareie com **Leitor QR ESP32**.
4. Abra um campo de texto.
5. Leia um código.

O projeto repassa diretamente os relatórios HID do leitor. Assim, o `Enter` configurado no leitor também é enviado ao celular/tablet.

## Limitação inicial

Esta versão aceita leitores que se apresentam como **HID Boot Keyboard**, o modo mais comum para leitores configurados como teclado USB. Se o log mostrar que a subclasse não é `BOOT`, será necessário adicionar o parser do Report Descriptor específico do leitor.

## Depuração

Para programar e acompanhar o monitor sem ocupar os GPIOs 19/20, prefira uma placa que tenha conversor USB-UART separado. O USB nativo do ESP32-S3 usa os mesmos pinos do leitor.
