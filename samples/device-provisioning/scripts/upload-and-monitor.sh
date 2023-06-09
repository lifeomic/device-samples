#!/bin/sh
pio run && scripts/flash-app.sh && pio device monitor
