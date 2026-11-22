#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .pio/i2c-host
g++ -std=c++11 -Wall -Wextra -Werror -Wno-unused-variable -fsanitize=undefined \
  -Itests/i2c_mocks -Isrc -Iinclude tests/tcs34725_sensor.cpp -o .pio/i2c-host/sensors
.pio/i2c-host/sensors
python3 tests/check_oled_parser.py
echo 'I2C sensor driver: shared OLED bus, timing/cache, NACK/short read, recovery and wraparound PASS'
