# Device Provisioning

This application implements the device provisioning flow in Arduino and C/C++.

## Prerequisites

### Device

The [M5Stack Core2 for AWS][m5core2_for_aws]. (The standard [M5Core2][m5core2] should also
work, but it has not been tested.)

### Software

You will need to have [PlatformIO][platformio] installed, and ideally configured
to work with your editor.

[m5core2_for_aws]: https://docs.m5stack.com/en/core/core2_for_aws
[m5core2]: https://docs.m5stack.com/en/core/core2
[platformio]: https://platformio.org/

## Running

Copy `lib/SampleConfig.h` to `lib/Config.h` and fill in the needed values.

In your editor of choice, open this project through PlatformIO, then run the
PlatformIO "Upload and Monitor Command" (In VSCode this can be access by opening
the Command Palette and typing "PlatformIO: Upload and Monitor").

Once the program is uploaded to the device, click on the left most button on the
device screen to initiate the provisioning process. Watch the Serial Monitor in
your editor to see the steps it is taking. If everything is successful, you
should see something like this in the Serial Monitor:

```log
Connecting to MQTT broker with clientId=demo_device_446
Connected to MQTT broker
Registering keys and certificate
Registering Thing
Register Thing accepted
mqttError=0
mqttConnected=0

Connecting to MQTT broker with clientId=<YOUR_ACCOUNT_ID>:<UUID>
Connected to MQTT broker
```
