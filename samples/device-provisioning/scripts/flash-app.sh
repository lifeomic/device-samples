#!/bin/sh

# For encryption to work, the bootloader needs to be built from an ESP-IDF
# project that has encryption enabled. PlatformIO does not support building
# the bootloader binary with encryption support. That's why we flash the
# bootloader binary from the ESP-IDF project directly.

echo "Flashing partitions and firmware with encryption-enabled bootloader to device at port $PORT"

python $IDF_PATH/components/esptool_py/esptool/esptool.py \
  --chip esp32 \
  --port $PORT \
  --baud 460800 --before default_reset \
  --after hard_reset \
    write_flash -z --flash_mode dio --flash_freq 80m --flash_size 16MB --encrypt \
    0x1000 esp-idf-build/bootloader.bin \
    0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
    0x8000 .pio/build/m5stack-core2/partitions.bin \
    0x20000 .pio/build/m5stack-core2/firmware.bin
