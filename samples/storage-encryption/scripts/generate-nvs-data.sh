#!/bin/sh

$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py \
  encrypt main/nvs.csv encrypted_nvs.bin 0x6000 --keygen --keyfile nvs_keys.bin
