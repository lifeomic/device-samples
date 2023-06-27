#include <Arduino.h>
#include <M5Core2.h>
#include <WiFi.h>
#include <MQTTClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "Config.h"

WiFiClientSecure wifiClient = WiFiClientSecure();
MQTTClient mqttClient = MQTTClient(2048);

String diagFileBuffer = "";
std::string fhirIngestAcceptedTopic = DEVICE_ID + "/" + LO_FHIR_INGEST_TOPIC_NAME + "/accepted";
std::string fhirIngestRejectedTopic = DEVICE_ID + "/" + LO_FHIR_INGEST_TOPIC_NAME + "/rejected";
std::string fileUploadAcceptedTopic = DEVICE_ID + "/" + LO_FILE_UPLOAD_TOPIC_NAME + "/accepted";
std::string fileUploadRejectedTopic = DEVICE_ID + "/" + LO_FILE_UPLOAD_TOPIC_NAME + "/rejected";

// put function declarations here:
void handleButton(Button btn);
void handleErrorButton(Button btn);
void resetDisplay();
void defaultDisplay();
void setupWifi();
void mqttConnect();
void recordResult(String val, String code, String system, String display);
void updateDiagnostic(String val);
void handleMessage(String &topic, String &payload);
void startFileUpload();

void setup()
{
  M5.begin();
  setupWifi();
  defaultDisplay();
}

void loop()
{
  mqttConnect();
  mqttClient.loop();
  M5.update();
  if (WiFi.status() != WL_CONNECTED)
  {
    M5.Lcd.println("error, wifi not connected");
  }
  handleButton(m5.BtnA);
  handleErrorButton(m5.BtnB);
  handleButton(m5.BtnC);
  resetDisplay();
}

// put function definitions here:
void setupWifi()
{
  M5.Lcd.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    M5.Lcd.print(".");
  }
  M5.Lcd.println("");
  M5.Lcd.println("Wifi Connected");

  wifiClient.setCACert(AWS_CERT_CA);
  wifiClient.setCertificate(LO_DEVICE_CERTIFICATE);
  wifiClient.setPrivateKey(LO_DEVICE_PRIVATE_KEY);
  mqttConnect();
  delay(2000);
}

void mqttConnect()
{
  if (mqttClient.connected())
  {
    return;
  }
  Serial.print("Last MQTT Error: ");
  Serial.println(mqttClient.lastError());

  Serial.println("Connecting to MQTT broker");
  mqttClient.begin(LO_IOT_ENDPOINT.c_str(), 8883, wifiClient);
  mqttClient.setCleanSession(false);
  while (!mqttClient.connect(DEVICE_ID.c_str()))
  {
    Serial.print(".");
    delay(100);
  }
  if (!mqttClient.connected())
  {
    Serial.println("");
    Serial.println("Broker timeout!");
  }
  else
  {
    Serial.println("Connected to MQTT broker");

    Serial.printf("Subscribing to topic %s\n", fhirIngestAcceptedTopic.c_str());
    mqttClient.subscribe(fhirIngestAcceptedTopic.c_str());

    Serial.printf("Subscribing to topic %s\n", fhirIngestRejectedTopic.c_str());
    mqttClient.subscribe(fhirIngestRejectedTopic.c_str());

    Serial.printf("Subscribing to topic %s\n", fileUploadAcceptedTopic.c_str());
    mqttClient.subscribe(fileUploadAcceptedTopic.c_str());

    Serial.printf("Subscribing to topic %s\n", fileUploadRejectedTopic.c_str());
    mqttClient.subscribe(fileUploadRejectedTopic.c_str());

    mqttClient.onMessage(handleMessage);
  }
}

void defaultDisplay()
{
  M5.Lcd.clearDisplay();
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Press a button to get results");
}

void handleButton(Button btn)
{
  if (btn.wasReleased() || btn.pressedFor(1000, 200))
  {
    M5.Lcd.print("Calculating Results...");
    delay(500);

    String result = "success";
    recordResult(result, LO_RESULT_CODE, LO_CODE_SYSTEM, "");
    M5.Lcd.printf("%s: %s\n", btn.label(), result.c_str());
    updateDiagnostic("Btn=" + String(btn.label()) + ";result=" + result);
  }
}

void handleErrorButton(Button btn)
{
  if (btn.wasReleased() || btn.pressedFor(1000, 200))
  {
    M5.Lcd.print("Calculating Results...");
    delay(500);

    String result = "fail";
    M5.Lcd.println("error detected. Uploading diagnostic log file");
    startFileUpload();
    recordResult(result, LO_RESULT_CODE, LO_CODE_SYSTEM, "");
    updateDiagnostic("Btn=" + String(btn.label()) + ";result=" + result);
  }
}

void resetDisplay()
{
  if (M5.Lcd.getCursorY() > 225)
  {
    defaultDisplay();
  }
}

void recordResult(String val, String code, String system, String display)
{
  int value = 0;
  if (val == "fail")
  {
    value = 1;
  }
  StaticJsonDocument<200> doc;
  doc["value"] = value;
  doc["unit"] = "number";
  StaticJsonDocument<200> codeObj;
  codeObj["code"] = code;
  codeObj["system"] = system;
  codeObj["display"] = "btn_press_event"; // fn argument display variable results in null.
  JsonArray coding = doc.createNestedArray("coding");
  coding.add(codeObj);
  char jsonBuffer[1024];
  serializeJson(doc, jsonBuffer);
  // retained and qos are required.
  bool pubResp = mqttClient.publish(LO_FHIR_INGEST_RULES_TOPIC.c_str(), jsonBuffer, false, 1);
  Serial.print("pub response ");
  Serial.println(pubResp);
}

void updateDiagnostic(String val)
{
  diagFileBuffer += val + "\n";
}

void startFileUpload()
{
  StaticJsonDocument<200> doc;
  doc["fileName"] = String(DEVICE_ID.c_str()) + "_" + String(millis()) + "_device_diagnostic.txt";
  doc["contentType"] = "text/plain";
  char jsonBuffer[1024];
  serializeJson(doc, jsonBuffer);
  Serial.println(jsonBuffer);
  bool pubResp = mqttClient.publish(LO_FILE_UPLOAD_RULES_TOPIC.c_str(), jsonBuffer, false, 1);
  Serial.print("pub response ");
  Serial.println(pubResp);
}

void handleMessage(String &topic, String &payload)
{
  Serial.print("Message received on topic: ");
  Serial.println(topic);
  Serial.print("Payload: ");
  Serial.println(payload);

  // unmarshall payload, extract uploadUrl
  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, payload);
  if (err)
  {
    Serial.print("Deserialization Error: ");
    Serial.println(err.c_str());
  }


  if (!doc["uploadUrl"].isNull())
  {
    HTTPClient http;
    http.begin(wifiClient, doc["uploadUrl"]);
    http.addHeader("Content-Type", "text/plain");
    http.addHeader("Content-Length", String(diagFileBuffer.length()));
    int response = http.PUT(diagFileBuffer);
    if (response >= 200 && response < 300)
    {
      diagFileBuffer = "";
    }
    else
    {
      Serial.printf("Error uploading diagnostic file: %d\n", response);
    }
  }
}
