# GUI v1.0 build, wiring, and smoke test

The active hardware is an Adafruit ESP32-S3 Reverse TFT Feather connected to
the lab bias board. There is no Arduino Nano in this setup. External SPI uses
Mode 0, MSB first, and 1 MHz.

## Feather wiring

Power down before changing wiring. Keep the three DAC I2C banks connected as
shown; none of these pins is SPI chip-select.

| Feather pin | Bias-board connection | Purpose |
| --- | --- | --- |
| GPIO3 / SDA | SDA0 | DAC bank 0 |
| GPIO4 / SCL | SCL0 | DAC bank 0 |
| D5 / GPIO5 | SDA1 | DAC bank 1 |
| D6 / GPIO6 | SCL1 | DAC bank 1 |
| D9 / GPIO9 | SDA2 | DAC bank 2 |
| D10 / GPIO10 | SCL2 | DAC bank 2 |
| MOSI / GPIO35 | translator SPI input path | controller data output |
| SCK / GPIO36 | translator SPI input path | controller clock output |
| MISO / GPIO37 | translator SPI output path, when used | controller data input |
| GND | GND | common reference |

The firmware preserves the original D13 state as a continuously HIGH output,
but does not toggle D13 or any other pin as SPI chip-select. Assign and test CS
separately only after the real peripheral CS net is identified. The level
translator's Feather-facing rail must be 3.3 V; never drive a voltage above
3.3 V into GPIO37/MISO.

## Build and upload

Compile the Feather sketch with Arduino IDE using board **Adafruit Feather
ESP32-S3 Reverse TFT**, or with Arduino CLI:

```sh
arduino-cli compile --warnings all \
  --fqbn esp32:esp32:adafruit_feather_esp32s3_reversetft \
  esp32_voltage_generator
```

Upload `esp32_voltage_generator/esp32_voltage_generator.ino`, press Reset once,
then open Serial Monitor at 115200 baud with the newline line ending.

Build a candidate GUI without replacing the released Windows executable:

```sh
mkdir -p build
gcc -std=c11 -Wall -Wextra -Wpedantic \
  $(pkg-config --cflags gtk4) main.c -o build/ReferenceVoltageGenerator-v1-test \
  $(pkg-config --libs gtk4) -lm
```

Do not replace `windows_app/ReferenceVoltageGenerator.exe` until the candidate
has launched, connected, transmitted, and received successfully.

## SPI smoke test

Send this line through Serial Monitor:

```text
0 A5 3C F0
```

The Feather must print `CMD: 0 A5 3C F0` and `TX: A5 3C F0`. Three `RX:` lines
also appear, but their values are undefined until a peripheral drives MISO.
Measure GPIO36/SCK and GPIO35/MOSI relative to GND. There must be 24 clocks,
with these MOSI bits sampled on rising SCK edges:

| Byte | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `A5` | 1 | 0 | 1 | 0 | 0 | 1 | 0 | 1 |
| `3C` | 0 | 0 | 1 | 1 | 1 | 1 | 0 | 0 |
| `F0` | 1 | 1 | 1 | 1 | 0 | 0 | 0 | 0 |

SCK idles low, data is sampled on rising edges, and the three bytes are sent
without the former one-second gaps. Repeat using one and two selected GUI
bytes. Blank or malformed selected fields must show an error and send nothing.

## DAC and rail check

After opening Serial Monitor, press Reset. Startup prints one status line for
each DAC address on each bank. `status=OK` means the device acknowledged and
initialized; `NO_ACK` means the electrical I2C path or bias-board power is not
available. A GUI voltage command prints a `DAC:` line with `status=OK` or
`status=NO_ACK`, and the TFT displays `I2C ERROR` instead of a false voltage on
failure.

If all banks say `NO_ACK` and the board's fixed rails are also absent, check the
bias-board supply and common ground before changing signal pins. Check for I2C
activity and a low ACK bit on:

- Bank 0: GPIO4/SCL and GPIO3/SDA
- Bank 1: GPIO6/SCL and GPIO5/SDA
- Bank 2: GPIO10/SCL and GPIO9/SDA
