# Sample code for connecting devices to the LifeOmic Platform

The LifeOmic Platform uses MQTT to communicate with devices. Certificates are used to secure the connection and there are 2 types of certificates: claim and device. Claim certificates are put on many devices to allow them to self provision and obtain a device certificate that is specific to the device. Once a device has a device certificate, it may then securely communicate with the platform. In these examples we use typescript but any language can connect and send data to the MQTT topics.

_Terminology note: Sometimes you will see "Thing" used in place of device. They are synonymous._

## Provisioning

Provisioning is done through a claim certificate. Claim certificates are generated in the UI and any device that possesses a claim certificate may provision itself with the LifeOmic Platform. Claim certificates consist of a certificate file and a private key. These two files can be put on the device the same time device code is uploaded. Initially devices all have the same claim certificate but through the provisioning process they will be given a device specific certificate.

Run the provisioning script with the following and replace the values inside of the <>:

```bash
yarn provision \
  --claimCert=./certs/<cert_name>.pem \
  --claimKey=./certs/<key_name>.key \
  --serialNumber=12345 \
  --manufacturer=my-company \
  --name=my-device \
  --model=prototype
```

This will create the following files:

- ./certs/<DeviceName>.pem
- ./certs/<DeviceName>.key

### Creating Device Certificate

First, an empty object is sent to the topic `$aws/certificates/create/json` to initiate the creation of the certificate, private key, and a certificate ownership token. The created certificates are sent back on the topic `$aws/certificates/create/json/accepted`. The full response payload looks like:

```json
{
  "certificateId": "<certificate_id>",
  "certificatePem": "<certificate_pem>",
  "privateKey": "<certificate_private_key>",
  "certificateOwnershipToken": "<certificate_ownership_token>"
}
```

If the creation fails, the error response is written to the topic `$aws/certificates/create/json/rejected` with the payload:

```json
{
  "statusCode": <number>,
  "errorMessage": "<error_message>",
  "errorCode": "<error_code>",
}
```

### Saving Device Details

Second, the device registers itself to the topic `$aws/provisioning-templates/DefaultProvisioningTemplate/provision/json` with the newly created certificate ownership token and the following payload:

```json
{
  "parameters": {
    "SerialNumber": "<device_serial>",
    "Manufacturer": "<device_manufacturer>",
    "Name": "<device_name>",
    "Model": "<device_model>"
  },
  "templateName": "DefaultProvisioningTemplate",
  "certificateOwnershipToken": "<certificate_ownership_token>"
}
```

The success response is written to the topic `$aws/provisioning-templates/DefaultProvisioningTemplate/provision/json/accepted` and with the following payload:

```json
{
  "thingName": "<thing_name>"
}
```

An error response will be written to the topic `$aws/provisioning-templates/DefaultProvisioningTemplate/provision/json/rejected` with the format:

```json
{
  "statusCode": <number>,
  "errorMessage": "<error_message>",
  "errorCode": "<error_code>",
}
```

## Save a FHIR Observation

In this script we can upload an observation from the device. We will need the device name that was output from the provisioning step. The MQTT topic to upload data to is `$aws/rules/FHIRIngest/`. You will need to know the FHIR code for the value you are uploading. In the example below we use our internal battery observation code.

Run the save observation script with the following:

```bash
yarn uploadObservation \
  --deviceName=<device_name> \
  --payloadValue=100 \
  --unit=% \
  --code=9750d5e5-d766-4793-a97d-16179578cc4d \
  --codeSystem=http://lifeomic.com/fhir/devices \
  --codeDisplay="Battery Percentage"
```

The payload being sent the payload:

```json
{
  "value": "<value>",
  "unit": "<unit>",
  "coding": [
    {
      "code": "<FHIR_code>",
      "system": "<FHIR_system>",
      "display": "<FHIR_display>"
    }
  ],
  "patientId": "<OPTIONAL_patient_id>"
}
```

## File Upload

To save a file we will need to request and receive a signed URL. The MQTT topic to request a signed URL is `$aws/rules/CreateFileUploadLink/` and the response will be published to the topic that is the same as the device's name.

```bash
yarn uploadFile \
  --deviceName=<device_name> \
  --fileName=dice \
  --filePath=./data/pic.png
```

The payload being sent is:

```json
{
  "fileName": "<file_name>",
  "contentType": "<content_type>"
}
```

The response payload has the format:

```json
{
  "uploadUrl": "<URL_to_PUT_file_to>"
}
```
