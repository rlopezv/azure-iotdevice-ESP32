/*
 * An Azure IoT program for sending telemetry of an ESP32 with si7021.
 */

#include <Wire.h>
#include <WiFi.h>
#include "Esp32MQTTClient.h"
#include <ezTime.h>


// SI7021 I2C address is 0x40(64)
#define Addr 0x40

#define INTERVAL 10000
#define MESSAGE_MAX_LEN 256

const char *deviceId = "esp32-si7021";
// Please input the SSID and password of WiFi
//const char *ssid = "SSID";
//const char *password = "password";
const char *ssid = "";
const char *password = "";


/*String containing Hostname, Device Id & Device Key in the format:                         */
/*  "HostName=<host_name>;DeviceId=<device_id>;SharedAccessKey=<device_key>"                */
/*  "HostName=<host_name>;DeviceId=<device_id>;SharedAccessSignature=<device_sas_token>"    */
static const char *connectionString = "HostName=g5-iothub.azure-devices.net;DeviceId=g5-iotdevice-esp32-si7021;SharedAccessKey=evGX2N7HE0olNvoEiuBcqfr0QGQd6Dp1cQcBGFPiOCc=";
const char *messageData = "{\"deviceId\":\"%s\",\"messageId\":%d, \"temperature\":%f, \"humidity\":%f, \"deviceTime\":\"%s\"}";
static bool hasIoTHub = false;
static bool hasWifi = false;
int messageCount = 1;
static bool messageSending = true;
static uint64_t send_interval_ms;


static void SendConfirmationCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result)
{
  if (result == IOTHUB_CLIENT_CONFIRMATION_OK)
  {
    Serial.println("Send Confirmation Callback finished.");
  }
}

static void MessageCallback(const char *payLoad, int size)
{
  Serial.println("Message callback:");
  Serial.println(payLoad);
}

static void DeviceTwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char *payLoad, int size)
{
  char *temp = (char *)malloc(size + 1);
  if (temp == NULL)
  {
    return;
  }
  memcpy(temp, payLoad, size);
  temp[size] = '\0';
  // Display Twin message.
  Serial.println(temp);
  free(temp);
}

static int DeviceMethodCallback(const char *methodName, const unsigned char *payload, int size, unsigned char **response, int *response_size)
{
  LogInfo("Try to invoke method %s", methodName);
  const char *responseMessage = "\"Successfully invoke device method\"";
  int result = 200;

  if (strcmp(methodName, "start") == 0)
  {
    LogInfo("Start sending temperature and humidity data");
    messageSending = true;
  }
  else if (strcmp(methodName, "stop") == 0)
  {
    LogInfo("Stop sending temperature and humidity data");
    messageSending = false;
  }
  else
  {
    LogInfo("No method %s found", methodName);
    responseMessage = "\"No method found\"";
    result = 404;
  }

  *response_size = strlen(responseMessage) + 1;
  *response = (unsigned char *)strdup(responseMessage);

  return result;
}


void setup()
{
  // Initialise I2C communication as MASTER
  Wire.begin();
  // Initialise serial communication, set baud rate = 9600
  Serial.begin(115200);

  // Start I2C transmission
  Wire.beginTransmission(Addr);
  // Stop I2C transmission
  Wire.endTransmission();

  Serial.println("ESP32 Device");
  Serial.println("Initializing...");
  Serial.println(" > WiFi");
  Serial.println("Starting connecting WiFi.");

  delay(10);
  WiFi.mode(WIFI_AP);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    hasWifi = false;
  }
  hasWifi = true;

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  waitForSync();
  Serial.println("Started on " +  UTC.dateTime(ISO8601));
    
  Serial.println(" > IoT Hub");
  if (!Esp32MQTTClient_Init((const uint8_t *)connectionString, true))
  {
    hasIoTHub = false;
    Serial.println("Initializing IoT hub failed.");
    return;
  }
  hasIoTHub = true;
  Esp32MQTTClient_SetSendConfirmationCallback(SendConfirmationCallback);
  Esp32MQTTClient_SetMessageCallback(MessageCallback);
  Esp32MQTTClient_SetDeviceTwinCallback(DeviceTwinCallback);
  Esp32MQTTClient_SetDeviceMethodCallback(DeviceMethodCallback);
  Serial.println("Start sending events.");
  randomSeed(analogRead(0));
  send_interval_ms = millis();
  
}

void loop()
{
  unsigned int data[2];
  
  // Start I2C transmission
  Wire.beginTransmission(Addr);
  // Send humidity measurement command, NO HOLD MASTER
  Wire.write(0xF5);
  // Stop I2C transmission
  Wire.endTransmission();
  delay(500);
    
  // Request 2 bytes of data
  Wire.requestFrom(Addr, 2);

  // Read 2 bytes of data
  // humidity msb, humidity lsb 
  if(Wire.available() == 2)
  {
    data[0] = Wire.read();
    data[1] = Wire.read();
  }
    
  // Convert the data
  float humidity  = ((data[0] * 256.0) + data[1]);
  humidity = ((125 * humidity) / 65536.0) - 6;

  // Start I2C transmission
  Wire.beginTransmission(Addr);
  // Send temperature measurement command, NO HOLD MASTER
  Wire.write(0xF3);
  // Stop I2C transmission
  Wire.endTransmission();
  delay(500);
    
  // Request 2 bytes of data
  Wire.requestFrom(Addr, 2);
  
  // Read 2 bytes of data
  // temp msb, temp lsb
  if(Wire.available() == 2)
  {
    data[0] = Wire.read();
    data[1] = Wire.read();
  }

  // Convert the data
  float temp  = ((data[0] * 256.0) + data[1]);
  float cTemp = ((175.72 * temp) / 65536.0) - 46.85;
   
  // Output data to serial monitor
  Serial.print("Relative humidity : ");
  Serial.print(humidity);
  Serial.println(" % RH");
  Serial.print("Temperature in Celsius : ");
  Serial.print(cTemp);
  Serial.println(" C");

  if (hasWifi && hasIoTHub)
  {
    if (messageSending &&
        (int)(millis() - send_interval_ms) >= INTERVAL)
    {

      // Send teperature data
      char messagePayload[MESSAGE_MAX_LEN];
      snprintf(messagePayload, MESSAGE_MAX_LEN, messageData, deviceId, messageCount++, cTemp, humidity, UTC.dateTime(ISO8601).c_str());
      Serial.println(messagePayload);
      EVENT_INSTANCE *message = Esp32MQTTClient_Event_Generate(messagePayload, MESSAGE);
      Esp32MQTTClient_SendEventInstance(message);
      send_interval_ms = millis();
    }
    else
    {
      Esp32MQTTClient_Check();
    }
  }
}