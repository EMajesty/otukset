# ESP32-C5 Hello World (bare bones)

Minimal ESP-IDF project for ESP32-C5 that prints a single line on boot.

## Build/Flash

```sh
idf.py set-target esp32c5
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

You should see:

```
Hello, ESP32-C5!
```
