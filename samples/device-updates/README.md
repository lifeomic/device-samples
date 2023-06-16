# Device Updates

This application implements the device update flow with Arduino and C/C++.

## Prerequisites

### Device

The [M5Stack Core2 for AWS][m5core2_for_aws]. (The standard [M5Core2][m5core2]
should also work, but it has not been tested.)

### Software

You will need to have [PlatformIO][platformio] installed, and ideally configured
to work with your editor.

[m5core2_for_aws]: https://docs.m5stack.com/en/core/core2_for_aws
[m5core2]: https://docs.m5stack.com/en/core/core2
[platformio]: https://platformio.org/

## Running

Copy `lib/SampleConfig.h` to `lib/Config.h` and fill in the needed values.

Change the `version` variable in `src/main.cpp` then execute `pio run` (or
`PlatformIO: Build` in VSCode). Once this completes navigate to
`./pio/build/m5stack-core2/firmware.bin`. Upload this file to the LifeOmic
Platform, and create a deployment target with your device.

Revert the `version` number in `src/main.cpp` then run the PlatformIO "Upload
and Monitor Command" (In VSCode this can be access by opening the Command Palette
and typing "PlatformIO: Upload and Monitor").

Once the program is uploaded to the device, click on the left most button on the
device screen to initiate the update process. Watch the Serial Monitor in
your editor to see the steps it is taking. You should see the `version` number
printed, then the update will take place, and you will see the updated `version`
number.
