#!/bin/sh
python $IDF_PATH/components/esptool_py/esptool/espefuse.py \
  --port $PORT summary
