#!/bin/sh

$IDF_PATH/components/partition_table/gen_esp32part.py \
  build/partition_table/partition-table.bin
