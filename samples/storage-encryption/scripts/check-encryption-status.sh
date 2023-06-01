#!/bin/sh

echo "Checking device at port $PORT"
python $IDF_PATH/components/esptool_py/esptool/espefuse.py --port $PORT summary
