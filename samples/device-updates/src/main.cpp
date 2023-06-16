#include <Arduino.h>
#include <cstdio>
#include <WifiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>

// See lib_deps in platformio.ini more details about these:
#include <M5Core2.h>
#include <MQTTClient.h>
#include <ArduinoJSON.h>

#include "Config.h"

// types and shared state:
WiFiClientSecure wifiClient = WiFiClientSecure();
MQTTClient mqttClient = MQTTClient(5120); // 5120 = buffer size; we need something big enough to handle the mqtt messages for a single loop execution

// For demo: change this value, build the project, and upload binary. Then revert this value.
const char *version = "v1.0.0";

const char *certificate = LO_DEVICE_CERTIFICATE;
const char *privateKey = LO_DEVICE_PRIVATE_KEY;
const char *deviceId = LO_DEVICE_ID;

enum UpdateState
{
  Idle,
  StartUpdate,
  UpdateStatusInProgress,
  DownloadAndApplyUpdate,
  Success,
  Failure,
  Restart
};

UpdateState updateState = Idle;

String firmwareUrl = "";
String firmwareId = "";
String updatePayload = "";
String jobId = "";

// Topics
// https://docs.aws.amazon.com/iot/latest/developerguide/jobs-workflow-device-online.html

String JOBS_TOPIC = "$aws/things/" + String(deviceId) + "/jobs";
String JOBS_NOTIFY_NEXT = JOBS_TOPIC + "/notify-next";
String JOBS_DESCRIBE_EXECUTION_NEXT = JOBS_TOPIC + "/$next/get";
String JOBS_DESCRIBE_EXECUTION_NEXT_ACCEPTED = JOBS_DESCRIBE_EXECUTION_NEXT + "/accepted";
String JOBS_DESCRIBE_EXECUTION_NEXT_REJECTED = JOBS_DESCRIBE_EXECUTION_NEXT + "/rejected";
String currentJobTopic = "";

// functions declarations:
void setupWifi(const char *certificate, const char *privateKey, bool forceReconnect);
void setupMqtt(String deviceId, bool forceReconnect);
int downloadAndApply(String url);

// - topic handlers
void handleNotifyNext(String payload);
void handleJobDescribeJobExecution(String payload);

// -- generic helpers
void handleMessages(String topic, String payload);
void handleError(String topic, String payload);

// - topic publishers
void publishDescribeExecution();
void publishUpdateExecution(String jobTopic, String jobId, String status);

void setup()
{
  M5.begin();
  setupWifi(certificate, privateKey, false);
  setupMqtt(deviceId, false);
  M5.Lcd.printf("Current version is: %s", version);
  Serial.printf("Current version is: %s", version);
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
    publishDescribeExecution(); // If there is an update available, handleDescribeJobExecution will change updateState to StartUpdate
  }

  switch (updateState)
  {
  case StartUpdate:
    currentJobTopic = JOBS_TOPIC + "/" + jobId + "/update";
    Serial.printf("Current job topic %s\n", currentJobTopic.c_str());
    setupMqtt(deviceId, true); // Force reconnect mqtt to subscribe to the currentJobTopic
    updateState = UpdateStatusInProgress;
    break;
  case UpdateStatusInProgress:
    publishUpdateExecution(currentJobTopic, jobId, "IN_PROGRESS");
    updateState = DownloadAndApplyUpdate;
    break;
  case DownloadAndApplyUpdate:
    int result = downloadAndApply(firmwareUrl);
    if (result != 0)
    {
      updateState = Failure;
    }
    else
    {
      updateState = Success;
    }
    break;
  case Success:
    publishUpdateExecution(currentJobTopic, jobId, "SUCCEEDED");
    updateState = Restart;
    break;
  case Failure:
    publishUpdateExecution(currentJobTopic, jobId, "FAILED");
    updateState = Idle;
    break;
  case Restart:
    Serial.println("Restarting...");
    ESP.restart();
    break;
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
  if (!forceReconnect && mqttClient.connected() == true)
  {
    return;
  }
  Serial.printf("mqttError=%d\n", mqttClient.lastError());
  Serial.printf("mqttConnected=%s\n\n", String(mqttClient.connected()));

  Serial.printf("Connecting to MQTT broker with clientId=%s\n", deviceId.c_str());
  mqttClient.begin(IOT_ENDPOINT, 8883, wifiClient);
  mqttClient.setCleanSession(false);

  // Subscribe to topics
  mqttClient.subscribe(JOBS_NOTIFY_NEXT);
  mqttClient.subscribe(JOBS_DESCRIBE_EXECUTION_NEXT_ACCEPTED);
  mqttClient.subscribe(JOBS_DESCRIBE_EXECUTION_NEXT_REJECTED);
  if (currentJobTopic != "")
  {
    mqttClient.subscribe(currentJobTopic + "/accepted");
    mqttClient.subscribe(currentJobTopic + "/rejected");
  }

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

// Handlers
void handleError(String topic, String payload)
{
  Serial.println("error from topic.");
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Payload: ");
  Serial.println(payload);
  M5.Lcd.print("\nError=");
  M5.Lcd.println(payload);
};

void handleNotifyNext(String payload)
{
  Serial.println("Message from notify-next:");
  Serial.println(payload);
}

void handleDescribeJobExecution(String payload)
{
  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, payload);
  if (err)
  {
    Serial.print("Deserialization Error: ");
    Serial.println(err.f_str());
    return;
  }
  const char *_jobId = doc["execution"]["jobId"];
  const char *status = doc["execution"]["status"];
  const char *operation = doc["execution"]["jobDocument"]["operation"];
  const char *_firmwareId = doc["execution"]["jobDocument"]["firmwareId"];
  const char *_firmwareUrl = doc["execution"]["jobDocument"]["firmwareUrl"];
  Serial.println(_firmwareId);
  Serial.println(_firmwareUrl);
  Serial.println(_jobId);
  if (_firmwareUrl != NULL && _jobId != NULL)
  {
    Serial.println("scheduling update");
    firmwareId = String(_firmwareId);
    firmwareUrl = String(_firmwareUrl);
    jobId = _jobId;
    Serial.printf("firmwareId=%s;jobId=%s\n", firmwareId.c_str(), jobId.c_str());
    updateState = StartUpdate;
  }
  else
  {
    Serial.println("no job to execute");
  }
}

void handleMessages(String topic, String payload)
{
  Serial.printf("Received message from topic=%s\n", topic.c_str());
  if (topic == JOBS_NOTIFY_NEXT)
  {
    handleNotifyNext(payload);
    return;
  }
  else if (topic == JOBS_DESCRIBE_EXECUTION_NEXT_ACCEPTED)
  {
    handleDescribeJobExecution(payload);
  }
  else
  {

    handleError(topic, payload);
    return;
  }
}

// Publishers
void publishDescribeExecution()
{
  String payload = "{\"jobId\": \"$next\", \"thingName\": \"" + String(deviceId) + "\"}";
  mqttClient.publish(JOBS_DESCRIBE_EXECUTION_NEXT, payload, false, 1);
}

void publishUpdateExecution(String jobTopic, String jobId, String status)
{
  String payload = "{\"jobId\": \"" + jobId + "\", \"status\": \"" + status + "\"}";
  Serial.printf("Updating job execution. Status=%s\n", status);
  mqttClient.publish(jobTopic, payload, false, 1);
}

int downloadAndApply(String url)
{
  Serial.println("starting file download");
  HTTPClient https;
  if (https.begin(wifiClient, url))
  {
    https.setTimeout(10000); // miliseconds
    int httpCode = https.GET();
    if (httpCode <= 0 || httpCode != HTTP_CODE_OK)
    {
      Serial.printf("HTTP error %d\n", httpCode);
      https.end();
      return 1;
    }

    Serial.println("Writing update");
    // See https://github.com/espressif/arduino-esp32/tree/master/libraries/Update
    // for details about the Update library.
    bool canBegin = Update.begin(https.getSize());
    if (!canBegin)
    {
      https.end();
      return 1;
    }
    size_t written = Update.writeStream(https.getStream());
    if (written == https.getSize())
    {
      Serial.println("Written : " + String(written) + " successfully");
    }
    else
    {
      Serial.println("Written only : " + String(written) + "/" + String(https.getSize()) + ". Retry?");
    }
    if (Update.end())
    {
      Serial.println("Update end");
      if (Update.isFinished())
      {
        Serial.println("Update is finished");
      }
    }
    https.end();
  }
  return 0;
}