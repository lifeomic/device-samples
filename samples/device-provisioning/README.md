# Device Provisioning

This application implements the device provisioning flow in Arduino and C/C++.

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

This project assumes the ESP32 device has enabled Flash Encryption and NVS
Encryption in development mode. The code assumes a claim certificate and
private key are stored in the device's NVS before being provisioned. The project
also provides the scripts needed to generate and load that data:
1. `scripts/generate-nvs-data`: generates encrypted NVS data assuming the claim
cert, private key, WiFi network name, and WiFi password are stored in
`src/secrets` with the names specified in `src/nvs.csv`. To easily populate
these files, run `cp -r src/secrets-example src/secrets` and fill in each
file with real data.
2. `scripts/flash-nvs-data`: actually loads the generated NVS data onto the
device. This is what needs to happen before actually turning on the device for
the first time.
3. The above commands assume that `$PORT`, `$NVS_OFFSET`, and `$NVS_KEYS_OFFSET`
are set within your active terminal. To get the port of your connected device,
run `pio device list` within a PlatformIO terminal; the port should look like
`/dev/cu.usbserial-00000000`. To get the NVS offsets, run `pio run` followed by
`./scripts/inspect-partitions.sh`. This will print out the partitions table,
along with the offsets of the NVS and NVS keys partitions.

PlatformIO does not support building a bootloader with encryption support, so
this project provides pre-built bootloader binary that does support it. It's
under `esp-idf-build/bootloader.bin`. For generating your own bootloader, and
to better understand how the NVS encryption works, please look at our other
example project [here](https://github.com/lifeomic/device-samples/tree/master/samples/storage-encryption).

Once all the initial data has been flahsed onto the device, upload and monitor
the firmware by running:

`./scripts/upload-and-monitor.sh`

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
