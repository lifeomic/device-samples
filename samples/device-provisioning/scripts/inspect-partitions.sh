#!/bin/sh
$IDF_PATH/components/partition_table/gen_esp32part.py \
  .pio/build/m5stack-core2/partitions.bin
