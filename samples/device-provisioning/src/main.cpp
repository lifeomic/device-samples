#include <stdio.h>
#include "sdkconfig.h"
#include <Arduino.h>
#include <cstdio>
#include <WifiClientSecure.h>
#include <M5Core2.h>
#include <MQTTClient.h>
#include <ArduinoJSON.h>
#include <nvs.h>
#include <nvs_flash.h>

// Types and shared state:
WiFiClientSecure wifiClient = WiFiClientSecure();
MQTTClient mqttClient = MQTTClient(5120);
bool shouldReconnect = false;
bool shouldRegisterThing = false;

const char AWS_IOT_ENDPOINT[] = "data.iot.us.lifeomic.com";
// Amazon Root CA 1
static const char AWS_CERT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)EOF";

// Give an initial randomized deviceId.
// After provisioning, this ID will be replaced with an ID provided
// by the LifeOmic Platform, so this value is arbitrary.
// The deviceId is used as the clientId for MQTT connections.
String deviceId = "demo_device_" + String(random(1000));

// Topics
String CREATE_KEYS_AND_CERTIFICATE_TOPIC = "$aws/certificates/create/json";
String CREATE_KEYS_AND_CERTIFICATE_ACCEPTED = CREATE_KEYS_AND_CERTIFICATE_TOPIC + "/accepted";
String CREATE_KEYS_AND_CERTIFICATE_REJECTED = CREATE_KEYS_AND_CERTIFICATE_TOPIC + "/rejected";

String TEMPLATE_NAME = "DefaultProvisioningTemplate";
String REGISTER_THING_TOPIC = "$aws/provisioning-templates/" + TEMPLATE_NAME + "/provision/json";
String REGISTER_THING_ACCEPTED = REGISTER_THING_TOPIC + "/accepted";
String REGISTER_THING_REJECTED = REGISTER_THING_TOPIC + "/rejected";

// Function Declarations

// Setup
void setupWifi(bool forceReconnect);
void setupMqtt(String deviceId, bool forceReconnect);
bool isProvisioned();

// Storage
nvs_handle secrets_nvs_handle;
char *nvs_read_value(nvs_handle handle, const char *key);
bool nvs_write_value(nvs_handle handle, const char *key, const char *value);
esp_err_t nvs_secure_initialize();
void init_secrets_storage(void);

// Topic Handlers
void handleMessages(String topic, String payload);
void handleError(String topic, String payload);
void createKeysAndCertificateAccepted(String payload);
void registerThingAccepted(String payload);

// Topic Publishers
void createKeysAndCertificate();
void registerThing();

void setup()
{
  M5.begin();
  init_secrets_storage();

  setupWifi(false);

  // Initialize provisioning state.
  String isProvisioned = nvs_read_value(secrets_nvs_handle, "is_provisioned");
  isProvisioned = (isProvisioned != nullptr) ? isProvisioned : "false";
  nvs_write_value(secrets_nvs_handle, "is_provisioned", isProvisioned.c_str());

  // Use the device ID that's in NVS or if not use the initialDeviceId.
  char *existingDeviceId = nvs_read_value(secrets_nvs_handle, "device_id");
  deviceId = (existingDeviceId != nullptr) ? existingDeviceId : deviceId;
  nvs_write_value(secrets_nvs_handle, "device_id", deviceId.c_str());
  setupMqtt(deviceId, false);
}

void loop()
{
  mqttClient.loop();
  // mqttClient recommends calling this on esp32
  delay(10);
  M5.update();

  setupWifi(false);
  setupMqtt(deviceId, false);

  if (M5.BtnA.wasReleased() || M5.BtnA.pressedFor(1000, 200))
  {
    M5.Lcd.println("Starting provisioning process");
    delay(1000);

    if (!isProvisioned())
    {
      createKeysAndCertificate();
      return;
    }
  }

  if (shouldReconnect)
  {
    M5.Lcd.println("Reconnecting...");
    setupWifi(true);
    setupMqtt(deviceId, true);
    shouldReconnect = false;
  }

  if (shouldRegisterThing)
  {
    registerThing();
    shouldRegisterThing = false;
  }
}

// function definitions:
void setupWifi(bool forceReconnect)
{
  if (WiFi.status() == WL_CONNECTED && forceReconnect == false)
  {
    return;
  }

  if (forceReconnect)
  {
    WiFi.disconnect();
  }

  M5.Lcd.print("\nConnecting to WiFi");

  char *wifiSsid = nvs_read_value(secrets_nvs_handle, "wifi_ssid");
  char *wifiPassword = nvs_read_value(secrets_nvs_handle, "wifi_password");
  WiFi.begin(wifiSsid, wifiPassword);
  free(wifiSsid);
  free(wifiPassword);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    M5.Lcd.print(".");
  }

  M5.Lcd.println("\nWiFi Connected");
  wifiClient.setCACert(AWS_CERT_CA);
  char *cert = nvs_read_value(secrets_nvs_handle, "cert");
  char *privateKey = nvs_read_value(secrets_nvs_handle, "private_key");
  wifiClient.setCertificate(cert);
  wifiClient.setPrivateKey(privateKey);
}

void setupMqtt(String deviceId, bool forceReconnect)
{
  if (mqttClient.connected() == true)
  {
    return;
  }

  Serial.printf("mqttError=%d\n", mqttClient.lastError());
  Serial.printf("mqttConnected=%s\n\n", String(mqttClient.connected()).c_str());
  Serial.printf("Connecting to MQTT broker with clientId=%s\n", deviceId.c_str());
  mqttClient.begin(AWS_IOT_ENDPOINT, 8883, wifiClient);
  mqttClient.setCleanSession(false);

  mqttClient.subscribe(CREATE_KEYS_AND_CERTIFICATE_ACCEPTED.c_str());
  mqttClient.subscribe(CREATE_KEYS_AND_CERTIFICATE_REJECTED.c_str());
  mqttClient.subscribe(REGISTER_THING_ACCEPTED.c_str());
  mqttClient.subscribe(REGISTER_THING_REJECTED.c_str());

  while (!mqttClient.connect(deviceId.c_str()))
  {
    Serial.print(".");
    delay(1000);
  }

  Serial.println("");

  if (!mqttClient.connected())
  {
    Serial.println("Broker timeout!");
    return;
  }

  Serial.println("Connected to MQTT broker");
  mqttClient.onMessage(handleMessages);
}

bool isProvisioned()
{
  // Read is_provisioned from NVS and return false if it is not equal to "true".
  char *isProvisioned = nvs_read_value(secrets_nvs_handle, "is_provisioned");
  bool _isProvisioned = (isProvisioned != nullptr) ? strcmp(isProvisioned, "true") == 0 : false;
  free(isProvisioned);

  return _isProvisioned;
}

char *nvs_read_value(nvs_handle handle, const char *key)
{
  // Try to get the size of the item
  size_t value_size;
  if (nvs_get_str(handle, key, NULL, &value_size) != ESP_OK)
  {
    Serial.printf("Failed to get NVS size for %s\n", key);
    return nullptr;
  }

  char *value = new char[value_size];
  if (nvs_get_str(handle, key, value, &value_size) != ESP_OK)
  {
    delete[] value;
    Serial.printf("Failed to initialize NVS for %s\n", key);
    return nullptr;
  }

  return value;
}

bool nvs_write_value(nvs_handle handle, const char *key, const char *value)
{
  esp_err_t write_result = nvs_set_str(handle, key, value);
  if (write_result != ESP_OK)
  {
    Serial.printf("Failed to write value to NVS for %s\n", key);
    return false;
  }

  esp_err_t commit_result = nvs_commit(handle);
  if (commit_result != ESP_OK)
  {
    Serial.printf("Failed to commit NVS changes for %s\n", key);
    return false;
  }

  return true;
}

esp_err_t nvs_secure_initialize()
{
  esp_err_t err = ESP_OK;

  // 1. find partition with nvs_keys
  const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                              ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS,
                                                              "nvs_key");
  if (partition == nullptr)
  {
    Serial.println("Could not locate nvs_key partition. Aborting.");
    return ESP_FAIL;
  }

  // 2. read nvs_keys from key partition
  nvs_sec_cfg_t cfg;
  if (ESP_OK != (err = nvs_flash_read_security_cfg(partition, &cfg)))
  {
    Serial.printf("Failed to initialize nvs (rc=0x%x)\n", err);
    return err;
  }

  // 3. initialize nvs partition
  if (ESP_OK != (err = nvs_flash_secure_init(&cfg)))
  {
    Serial.printf("Failed to initialize nvs (rc=0x%x)\n", err);
    return err;
  }

  return err;
}

void init_secrets_storage(void)
{
  esp_err_t err = nvs_secure_initialize();
  if (err != ESP_OK)
  {
    Serial.printf("Failed to initialize nvs (rc=0x%x)\n", err);
  }

  if (nvs_open("secrets", NVS_READWRITE, &secrets_nvs_handle) != ESP_OK)
  {
    Serial.println("Failed to open NVS namespace: secrets");
    return;
  }
  else
  {
    Serial.println("Opened NVS namespace: secrets");
  }
}

// See https://docs.aws.amazon.com/iot/latest/developerguide/fleet-provision-api.html#create-keys-cert
void createKeysAndCertificate()
{
  Serial.println("Registering keys and certificate");
  // This payload is intentially empty and doesn't require any parameters.
  String payload = "{}";
  mqttClient.publish(CREATE_KEYS_AND_CERTIFICATE_TOPIC, payload, false, 1);
}

void createKeysAndCertificateAccepted(String payload)
{
  Serial.println("Received keys and certificate");

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, payload);
  if (err)
  {
    Serial.print("Deserialization Error: ");
    Serial.println(err.f_str());
  }

  // Remember original values.
  char *claim_cert = nvs_read_value(secrets_nvs_handle, "cert");
  char *claim_key = nvs_read_value(secrets_nvs_handle, "private_key");
  nvs_write_value(secrets_nvs_handle, "claim_cert", claim_cert);
  nvs_write_value(secrets_nvs_handle, "claim_key", claim_key);
  free(claim_cert);
  free(claim_key);

  // Set new values.
  nvs_write_value(secrets_nvs_handle, "cert", doc["certificatePem"]);
  nvs_write_value(secrets_nvs_handle, "private_key", doc["privateKey"]);
  nvs_write_value(secrets_nvs_handle, "crt_ownrshp_tkn", doc["certificateOwnershipToken"]);

  // The mqtt client advises against calling publish in a topic handler,
  // so we'll set this variable and call registerThing in the next loop.
  shouldRegisterThing = true;
}

// See: https://docs.aws.amazon.com/iot/latest/developerguide/fleet-provision-api.html#register-thing
void registerThing()
{
  Serial.println("Registering Thing");

  // These parameters are required by the LifeOmic Platform
  StaticJsonDocument<128> parameters;
  parameters["SerialNumber"] = "1234";
  parameters["Manufacturer"] = "LifeOmic";
  parameters["Name"] = "LifeOmic_Demo_Device" + String(random(1000));
  parameters["Model"] = "LifeOmic_Demo_Device";

  DynamicJsonDocument doc(1024);
  doc["templateName"] = TEMPLATE_NAME;
  doc["parameters"] = parameters;
  char *ownershipToken = nvs_read_value(secrets_nvs_handle, "crt_ownrshp_tkn");
  doc["certificateOwnershipToken"] = ownershipToken;

  String payload;
  serializeJson(doc, payload);
  mqttClient.publish(REGISTER_THING_TOPIC, payload, false, 1);
  free(ownershipToken);
}

void registerThingAccepted(String payload)
{
  Serial.println("Register Thing accepted");

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, payload);
  if (err)
  {
    Serial.print("Deserialization Error: ");
    Serial.println(err.f_str());
    return;
  }

  String thingName = doc["thingName"];

  deviceId = thingName;
  nvs_write_value(secrets_nvs_handle, "device_id", thingName.c_str());
  nvs_write_value(secrets_nvs_handle, "is_provisioned", "true");

  // The mqtt client advises against calling subscribe and connecting in a
  // topic handler, so we'll set this variable and reconnect in the next loop.
  shouldReconnect = true;
}

void handleError(String topic, String payload)
{
  Serial.println("error from topic.");
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Payload: ");
  Serial.println(payload);
  M5.Lcd.print("Failed to provision device. Error=");
  M5.Lcd.println(payload);
};

void handleMessages(String topic, String payload)
{
  if (topic == CREATE_KEYS_AND_CERTIFICATE_ACCEPTED)
  {
    createKeysAndCertificateAccepted(payload);
    return;
  }
  else if (topic == CREATE_KEYS_AND_CERTIFICATE_REJECTED)
  {
    handleError(topic, payload);
    return;
  }
  else if (topic == REGISTER_THING_ACCEPTED)
  {
    registerThingAccepted(payload);
    return;
  }
  else if (topic == REGISTER_THING_REJECTED)
  {
    // Restore claim cert values so the device can try again.
    char *claim_cert = nvs_read_value(secrets_nvs_handle, "claim_cert");
    char *claim_key = nvs_read_value(secrets_nvs_handle, "claim_key");
    nvs_write_value(secrets_nvs_handle, "cert", claim_cert);
    nvs_write_value(secrets_nvs_handle, "private_key", claim_key);
    handleError(topic, payload);
    free(claim_cert);
    free(claim_key);
    return;
  }
}