#!/bin/sh

echo "Flashing encrypted NVS data at offset $NVS_OFFSET in device at port $PORT"
python $IDF_PATH/components/esptool_py/esptool/esptool.py \
    -p $PORT \
    --before default_reset \
    --after no_reset \
    write_flash $NVS_OFFSET encrypted_nvs.bin

echo "Flashing NVS encryption keys at offset $NVS_KEYS_OFFSET in device at port $PORT"
python $IDF_PATH/components/esptool_py/esptool/esptool.py \
    -p $PORT \
    --before default_reset \
    --after no_reset \
    write_flash --encrypt $NVS_KEYS_OFFSET keys/nvs_keys.bin
