#include <Arduino.h>
#include <cstdio>
#include <WifiClientSecure.h>

// See lib_deps in platformio.ini more details about these:
#include <M5Core2.h>
#include <MQTTClient.h>
#include <ArduinoJSON.h>

#include "Config.h"

// types and shared state:
WiFiClientSecure wifiClient = WiFiClientSecure();
MQTTClient mqttClient = MQTTClient(4096);
bool _isProvisioned = false;
bool shouldReconnect = false;
bool shouldRegisterThing = false;

// Initialize these to claim certificate.
// After provisioning replace with device specific keys.
const char *certificate = LO_CLAIM_CERTIFICATE;
const char *privateKey = LO_CLAIM_PRIVATE_KEY;
const char *certificateOwnershipToken;

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

// functions declarations:
void setupWifi(const char *certificate, const char *privateKey, bool forceReconnect);
void setupMqtt(String deviceId, bool forceReconnect);
bool isProvisioned();

// topic handlers
void handleMessages(String topic, String payload);
void handleError(String topic, String payload);
void createKeysAndCertificateAccepted(String payload);
void registerThingAccepted(String payload);

// topic publishers
void createKeysAndCertificate();
void registerThing();

void setup()
{
  M5.begin();
  setupWifi(certificate, privateKey, false);
  setupMqtt(deviceId, false);
}

void loop()
{
  mqttClient.loop();
  delay(10); // mqttClient recommends calling this on esp32
  M5.update();

  setupWifi(certificate, privateKey, false);
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
    setupWifi(certificate, privateKey, true);
    setupMqtt(deviceId, true);
    shouldReconnect = false;
  }

  if (shouldRegisterThing)
  {
    M5.Lcd.println("Registering thing");
    registerThing();
    shouldRegisterThing = false;
  }
}

// function definitions:
void setupWifi(const char *certificate, const char *privateKey, bool forceReconnect)
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
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    M5.Lcd.print(".");
  }
  M5.Lcd.println("\nWiFi Connected");
  wifiClient.setCACert(AWS_CERT_CA);
  wifiClient.setCertificate(certificate);
  wifiClient.setPrivateKey(privateKey);
}

void setupMqtt(String deviceId, bool forceReconnect)
{
  if (mqttClient.connected() == true)
  {
    return;
  }
  Serial.printf("mqttError=%d\n", mqttClient.lastError());
  Serial.printf("mqttConnected=%s\n\n", String(mqttClient.connected()));

  Serial.printf("Connecting to MQTT broker with clientId=%s", deviceId.c_str());
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
  return _isProvisioned;
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

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, payload);
  if (err)
  {
    Serial.print("Deserialization Error: ");
    Serial.println(err.f_str());
  }

  certificate = doc["certificatePem"];
  privateKey = doc["privateKey"];
  certificateOwnershipToken = doc["certificateOwnershipToken"];

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
  doc["certificateOwnershipToken"] = certificateOwnershipToken;

  String payload;
  serializeJson(doc, payload);
  mqttClient.publish(REGISTER_THING_TOPIC, payload, false, 1);
}

void registerThingAccepted(String payload)
{
  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, payload);
  if (err)
  {
    Serial.print("Deserialization Error: ");
    Serial.println(err.f_str());
    return;
  }
  Serial.println("Register Thing accepted");

  String deviceConfiguration = doc["deviceConfiguration"];
  String thingName = doc["thingName"];

  // TODO: store deviceId in long term storage
  deviceId = thingName;
  _isProvisioned = true;

  // The mqtt client advises against calling subscribe and connecting in a topic handler,
  // so we'll set this variable and reconnect in the next loop.
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

    handleError(topic, payload);
    return;
  }
}