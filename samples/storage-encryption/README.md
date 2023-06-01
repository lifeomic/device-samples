# Storage Encryption Example

This example project shows how to use NVS encryption on ESP-32 chips for
securely provisioning the claim certificate and private key when provisioning
a device. The supported targets are ESP32, ESP32-C2, ESP32-C3, ESP32-C6,
ESP32-H2, ESP32-S2, and ESP32-S3.

The general overview of how the device is securely provisioned consists of the
following:

1. The claim certificate and private keys are transformed to an encrypted binary
blob using a set of generated encryption keys.

2. The generated encryption keys that were used to encrypt the binary of the
claim certificate and private keys are stored in a custom encrypted partition
on the device.

3. Flash encryption is enabled on the device. This is a pre-requisite to make
NVS encryption work, since the generated encryption keys are stored in an
encrypted partition in flash storage. The NVS partition itself does not support
flash encryption, which is why the claim certificate and private key are
encrypted in binary format before being stored in the NVS partition.

4. The device is flashed and is ready to be used. After this step, the code
can securely access the claim certificate and private key from NVS storage, and
they will be decrypted automatically at runtime. This happens because at this
point Flash encryption and NVS encryption have both been enabled.

A Medium article that gets into all the details was published on the LifeOmic
blog. Follow this link to read it: [TODO](https://medium.com/lifeomic).

# Pre-requisites

1. Have Python 3 installed.

2. Install the ESP-IDF framework. The recommended way of doing so is to install
it via the [VSCode plugin](https://github.com/espressif/vscode-esp-idf-extension/blob/master/docs/tutorial/install.md).

3. Set the `$IDF_PATH` in your bash/zsh profile. When installing ESP-IDF with
the VSCode plugin, the path should look like
`export IDF_PATH=$HOME/esp/esp-idf`.

# Guide

This repo shows how to fully encrypte Non-Volatile Storage (NVS) on any ESP32
chip. The file at `main/nvs.csv` defines an NVS namespace called `certs` and defines two key/value pairs that represent a claim certificate and a
private key. This file is used to generate the encrypted binary that will be
flashed to the NVS partition of the device. The file definition assumes the
following:
1. A claim certificate will be stored under `main/certs/claim_cert.pem`.
2. A private key will be stored under `main/certs/private.key`.\
3. Both of these files are ignored from version control. They are provided
by the user when following this guide.

The following steps will show how to generate the encrypted NVS binary data and how to get it into the device. It will also show how to enable flash encryption and NVS encryption on the device so it is able to securely read/write data
to/from the NVS partition.

## Steps

1. Open the VSCode command palette and run `ESP-IDF: Open ESP-IDF Terminal`.
This will open a terminal that has access to executing `idf.py`.

2. Connect an ESP32 device and get the port number where it connected to. It
will be needed in the next steps.

3. Open the VSCode command palette and run `ESP-IDF: Build your project`. This
will build the project for the first time and generate a `.bin` file for the
partition table under the `build/partition_table` directory.

4. Inspect the partition table of the build by running:

`./scripts/inspect-partitions.sh`

The output will look like the following:

```
Parsing binary partition input...
Verifying table...
# ESP-IDF Partition Table
# Name, Type, SubType, Offset, Size, Flags
nvs,data,nvs,0x11000,24K,
storage,data,255,0x17000,4K,
factory,app,factory,0x20000,1M,
nvs_key,data,nvs_keys,0x120000,4K,encrypted
```

Export the `Offset` for the `nvs` and `nvs_key` to shell variables in a
terminal:
- `export NVS_OFFSET=0x11000`
- `export NVS_KEYS_OFFSET=0x120000`

It's important to correctly define these variables as they are needed by
the scripts that will be run in the following steps.

5. Generate the NVS data that will be flashed into the device:

`./scripts/generate-nvs-data.sh`

This will output the following files:
- `encrypted_nvs.bin`: the encrypted binary of the NVS data to be flashed into the NVS partition of the device.
- `keys/nvs_keys.bin`: the plain text binary of the encryption keys used to
encrypt the NVS data.

6. Flash the two generated binaries into the device by running:

`./scripts/flash-nvs-data.sh`

7. At this point, the data required for correctly provisioning the device has
already been loaded into it. Now, the device needs to be flashed for the first
time to enable flash encryption. It can be done by running the following
command:

`idf.py -p $PORT flash monitor`

At this point, flash encryption should have been enabled in the device and the
program should have printed the decrypted claim certificate and private key to
the output in the console.

8. From now own, when reprogramming the device, the following command should be
used:

`idf.py -p $PORT encrypted-flash monitor`

# Troubleshooting

When flashing the device for the first time with encryption enabled, certain
eFuses are burned (their value is changed from 0 to 1), which is what turns on
encryption. If things go wrong and encryption is not enabled, it's possible to
check the eFuse values by running the following command:

`python $IDF_PATH/components/esptool_py/esptool/espefuse.py --port $PORT summary`

See the links in the `References` section for more information about how the
flash encryption process works.

# References
- [How to encrypt the NVS volume on the ESP32](https://dev.to/kkentzo/how-to-encrypt-the-nvs-volume-on-the-esp32-4n9k)
- [ESP-IDF: Storing AWS IoT certificates in the NVS partition (for OTA)](https://savjee.be/blog/esp-idf-store-aws-iot-certificates-in-nvs-partition/)
- [ESP-IDF Flash Encryption](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/flash-encryption.html#flash-encryption)
- [ESP-IDF NVS Encryption](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html#nvs-encryption)
- [ESP-IDF Example](https://github.com/espressif/esp-idf/tree/efe919275e8f4516ffd5f99f9a59e9d3efbae281/examples/security/flash_encryption)
