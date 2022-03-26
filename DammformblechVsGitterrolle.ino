// standard C++ date/time
#include <time.h>
#include <sys/time.h>

// soilMoisture sensor
#include <SPI.h>
#include <Wire.h>

// DHT22
#include <DHT.h>

// WiFi
#include <WiFi.h>

// SPI, SD card reader & file system
#include "FS.h"
#include "SD.h"
#include "SPI.h"

// DHT22
#define DHT_SENSOR_1_PIN  21 // ESP32 pin GIOP21 connected to first DHT22 sensor
#define DHT_SENSOR_2_PIN  22 // ESP32 pin GIOP19 connected to second DHT22 sensor
#define DHT_SENSOR_TYPE DHT22

// WiFi
// Replace with your network credentials
const char* ssid     = "DammformblechGitterrolle";
const char* password = "123456789";

// Webserver
// Set web server port number to 80
WiFiServer server(80);
// Variable to store the HTTP request
String header;

// soilMoisture sensor config
struct soilMoistureSensor {
  int airValue;
  int waterValue;
  int sensorPin;
};
struct soilMoistureSensor soilMoistureSensor1 = {.airValue = 2363, .waterValue = 435, .sensorPin = 34};
struct soilMoistureSensor soilMoistureSensor2 = {.airValue = 2363, .waterValue = 435, .sensorPin = 35};

// DHT22
DHT dht_sensor_1(DHT_SENSOR_1_PIN, DHT_SENSOR_TYPE);
DHT dht_sensor_2(DHT_SENSOR_2_PIN, DHT_SENSOR_TYPE);


void setup() {

  // assumed initial date
  struct tm tm;
  tm.tm_year = 2022 - 1900;
  tm.tm_mon = 3;
  tm.tm_mday = 26;
  tm.tm_hour = 14;
  tm.tm_min = 00;
  tm.tm_sec = 00;
  time_t t = mktime(&tm);
  Serial.printf("Setting time: %s", asctime(&tm));
  struct timeval now = { .tv_sec = t };
  settimeofday(&now, NULL);

  // when to activate WiFipower
  struct tm activateWifi;
  activateWifi.tm_year = 2022 - 1900;
  activateWifi.tm_mon = 3;
  activateWifi.tm_mday = 26;
  activateWifi.tm_hour = 14;
  activateWifi.tm_min = 00;
  activateWifi.tm_sec = 00;

  // USB serial output
  Serial.begin(115200); // open serial port, set the baud rate

  // DHT22
  dht_sensor_1.begin(); // initialize the first DHT sensor
  dht_sensor_2.begin(); // initialize the second DHT sensor

  // WiFi
  setupWifi();

  // measurement data structure
  //struct

  // SD Card
  if(!SD.begin(5)){
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();

  if(cardType == CARD_NONE){
    Serial.println("No SD card attached");
    return;
  }

  writeFile(SD, "/hello.txt", "Hello ");
  appendFile(SD, "/hello.txt", "World!\n");

  Serial.println("Setup done");
}


void loop()
{
  measureDht22(dht_sensor_1);
  measureDht22(dht_sensor_2);

  measureSoilMoisture(soilMoistureSensor1);
  measureSoilMoisture(soilMoistureSensor2);

  delay(5000);
}

void setupWifi()
{
  // Connect to Wi-Fi network with SSID and password
  Serial.print("Setting AP (Access Point)...");
  // Remove the password parameter, if you want the AP (Access Point) to be open
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  server.begin();
}

void measureSoilMoisture(soilMoistureSensor sensor) {

  // soilMoisture initial values
  int soilMoistureValue;
  int soilmoisturePercent;

  soilMoistureValue = analogRead(sensor.sensorPin);  //put Sensor insert into soil
  Serial.print("         Pin: ");
  Serial.print(sensor.sensorPin);
  Serial.print("  |  Percentage: ");
  Serial.print("Analog value: ");
  Serial.print(soilMoistureValue);
  Serial.print("  |  Percentage: ");
  soilmoisturePercent = map(soilMoistureValue, sensor.airValue, sensor.waterValue, 0, 100);

  if (soilmoisturePercent > 100)
  {
    Serial.println("100 %");
  }
  else if (soilmoisturePercent < 0)
  {
    Serial.println("0 %");
  }
  else if (soilmoisturePercent >= 0 && soilmoisturePercent <= 100)
  {
    Serial.print(soilmoisturePercent);
    Serial.println("%");
  }
}

void measureDht22(DHT dhtSensor) {

  // DHT22 ========================================
  // read temperature in Celsius
  float tempC = dhtSensor.readTemperature();

  // check whether the reading is successful or not
  if ( isnan(tempC) ) {
    Serial.println("Failed to read from DHT sensor!");
  } else {
    Serial.print("Temperature: ");
    Serial.print(tempC);
    Serial.println("°C");
  }
}

void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if(!file){
    Serial.println("Failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

void appendFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if(!file){
    Serial.println("Failed to open file for appending");
    return;
  }
  if(file.print(message)){
      Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}
