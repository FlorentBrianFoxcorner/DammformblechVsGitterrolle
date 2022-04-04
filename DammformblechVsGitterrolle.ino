// standard C++ date/time
#include <time.h>
#include <sys/time.h>

// soilMoisture sensor
#include <Wire.h>

// DHT22
#include <DHT.h>

// SD card reader & file system
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <SPIFFS.h>

// WiFi
#include <WiFi.h>

//RTC
#include <RTClib.h>

//Webserver
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// DHT22
#define DHT_SENSOR_1_PIN  26 // ESP32 pin connected to first DHT22 sensor
#define DHT_SENSOR_2_PIN  27 // ESP32 pin connected to second DHT22 sensor
#define DHT_SENSOR_TYPE DHT22

#define ABSOLUTE_ZERO -273

// LED
#define LED_PIN 2

#define DATA_FILENAME "/measurements.csv"
#define CONFIG_FILENAME "/config.txt"

#define DELIMITER ";"

#define CONFIG_SSID "wifiSsid"
#define CONFIG_PASS "wifiPass"
#define CONFIG_AP "ap"
#define MOISTURE_ONE_AIR_VALUE "moistureOneAirValue"
#define MOISTURE_ONE_WATER_VALUE "moistureOneWaterValue"
#define MOISTURE_TWO_AIR_VALUE "moistureTwoAirValue"
#define MOISTURE_TWO_WATER_VALUE "moistureTwoWaterValue"

//TODO destroy objects / check for leaks
//TODO: ABSOLUTE_ZERO as tempC is error state, show warning in website
//TODO: Set date via website, //rtc.adjust(DateTime(2022, 3, 30, 21, 29, 0));
//TODO: Karte entfernen und wieder einlegen -> keine Werte geschrieben: Prüfen ob Wert geschrieben, sonst error state
//TODO: "Failed to open file for writing" -> wie behandeln?
RTC_DS3231 rtc;

// WiFi
char ssid[] = "DammformblechVsGitterrolle";
char password[] = "";
boolean accessPoint = false;

AsyncWebServer server(80);

struct Measurement {
  int timestamp;
  float tempC1;
  float tempC2;
  int soilMoistureValue1;
  int soilmoisturePercent1;
  int soilMoistureValue2;
  int soilmoisturePercent2;
};

struct SoilMoistureMeasurement {
  int soilMoistureValue;
  int soilmoisturePercent;
};

struct SoilMoistureSensor {
  int airValue;
  int waterValue;
  int sensorPin;
};

SoilMoistureSensor soilMoistureSensor1;
//= {.airValue = 2363, .waterValue = 435, .sensorPin = 34};
SoilMoistureSensor soilMoistureSensor2;
//= {.airValue = 2363, .waterValue = 435, .sensorPin = 35};
SoilMoistureSensor& sensor1 = soilMoistureSensor1;
SoilMoistureSensor& sensor2 = soilMoistureSensor2;

// DHT22
DHT dht_sensor_1(DHT_SENSOR_1_PIN, DHT_SENSOR_TYPE);
DHT dht_sensor_2(DHT_SENSOR_2_PIN, DHT_SENSOR_TYPE);

RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int state = 0;

/**
   Log wake up cause to serial.
*/
void serialWakeUpCause() {
  esp_sleep_wakeup_cause_t wakeUpCause = esp_sleep_get_wakeup_cause();
  Serial.print("Woke up ");

  switch (wakeUpCause)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("from deep sleep, caused by GPIO_NUM_25 (RTC_IO)"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("from deep sleep, caused by ESP_SLEEP_WAKEUP_TIMER"); break;
    default : Serial.printf("caused by %d.\n", wakeUpCause); break;
  }
}

void controlActivity() {

  esp_sleep_wakeup_cause_t wakeUpCause = esp_sleep_get_wakeup_cause();
  switch (wakeUpCause)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : mountSdCard(); measurement(); startWiFi(); startWebServer(); break; // button was pressed, enter WiFi mode
    case ESP_SLEEP_WAKEUP_TIMER : mountSdCard(); measurement(); goToSleep(); break; // timer woke up, measurement only
    default : mountSdCard(); measurement(); goToSleep(); break; // boot case
  }

  // instead of waiting one cycle, directly show errror state
  if (state != 0) {
    showStatusWithLed();
    return;
  }
}

void setTimeFromClock() {
  if (! rtc.begin()) {
    Serial.println("Error reading from real time clock.");
    Serial.flush();
    switchToErrorState(2);
  }
  //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  timeval tv;
  DateTime now = rtc.now();
  tv.tv_sec = now.unixtime();
  tv.tv_usec = 0;
  settimeofday(&tv, NULL);
}

void getFormattedDateTime(char* result) {
  struct timeval readNow;
  gettimeofday(&readNow, NULL); // get from ESP32 RTC
  time_t readTime = readNow.tv_sec;
  char buf[20];
  strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", gmtime(&readTime));
  for (int i = 0; i < 20; ++i) {
    result[i] = buf[i];
  }
}

void logTime() {
  char buf[20];
  getFormattedDateTime(buf);
  Serial.printf("Current date and time: %s\n", buf);
}

void measurement() {

  struct Measurement current = {0, 0, 0, 0, 0, 0, 0};

  dht_sensor_1.begin(); // initialize first DHT sensor
  dht_sensor_2.begin(); // initialize second DHT sensor

  current.tempC1 = measureDht22(dht_sensor_1);
  current.tempC2 = measureDht22(dht_sensor_2);

  SoilMoistureMeasurement soulMoisture1 = measureSoilMoisture(soilMoistureSensor1);
  current.soilMoistureValue1 = soulMoisture1.soilMoistureValue;
  current.soilmoisturePercent1 = soulMoisture1.soilmoisturePercent;

  SoilMoistureMeasurement soulMoisture2 = measureSoilMoisture(soilMoistureSensor2);
  current.soilMoistureValue2 = soulMoisture2.soilMoistureValue;
  current.soilmoisturePercent2 = soulMoisture2.soilmoisturePercent;

  struct timeval readNow;
  gettimeofday(&readNow, NULL);
  current.timestamp = readNow.tv_sec;

  Serial.printf("timestamp: %i, tempC1: %f, soilMoistureValue1: %i, soilmoisturePercent1 %i, tempC2: %f, soilMoistureValue2: %i, soilmoisturePercent2 %i \n", current.timestamp, current.tempC1, current.soilMoistureValue1, current.soilmoisturePercent1, current.tempC2, current.soilMoistureValue2, current.soilmoisturePercent2 );

  saveMeasurement(current);

  blink(1, 1000);
}

void saveMeasurement(Measurement measurement) {
  File dataFile = SD.open(DATA_FILENAME, FILE_APPEND);
  if (!dataFile) {
    Serial.println("Failed to open file for writing");
    return;
  }
  String delimiter = DELIMITER;

  time_t measurementTime = measurement.timestamp;
  char buf[20];
  strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", gmtime(&measurementTime));

  String output = String(buf + delimiter + measurement.tempC1 + delimiter + measurement.soilMoistureValue1 + delimiter + measurement.soilmoisturePercent1 + delimiter + measurement.tempC2 + delimiter + measurement.soilMoistureValue2 + delimiter + measurement.soilmoisturePercent2) ;
  Serial.println(output);
  dataFile.println(output);
  dataFile.close();
}

void readConfig() {
  Serial.println("\nReading config:");
  File configFile = SD.open(CONFIG_FILENAME, FILE_READ);
  if (!configFile) {
    Serial.printf("Failed to read config file '%s'.\n", CONFIG_FILENAME);
    switchToErrorState(3);
    return;
  }

  String line;
  String key;
  String value;
  while (configFile.available() != 0) {
    line = configFile.readStringUntil('\n');
    line.trim();
    int index = line.indexOf('=');
    key = line.substring(0, index);
    value = line.substring(index + 1, line.length() + 1);

    if (key.length() == 0 && value.length() == 0) {
      continue;
    }

    if (key.equals(CONFIG_SSID)) {
      value.toCharArray(ssid, value.length() + 1);
      Serial.printf("SSID: %s\n", ssid);

    }
    else if (key.equals(CONFIG_PASS)) {
      value.toCharArray(password, value.length() + 1);
      Serial.printf("Password: %s \n", password);
    }
    else if (key.equals(CONFIG_AP)) {
      if (value.equals("true")) {
        accessPoint = true;
      }
      else {
        accessPoint = false;
      }
      Serial.printf("Access Point: %i\n", accessPoint);

    }
    else if (key.equals(MOISTURE_ONE_AIR_VALUE)) {
      sensor1.airValue = value.toInt();
      Serial.printf("moistureOneAirValue: %i\n", sensor1.airValue);
    }
    else if (key.equals(MOISTURE_ONE_WATER_VALUE)) {
      sensor1.waterValue = value.toInt();
      Serial.printf("moistureOneWaterValue: %i\n", sensor1.waterValue);
    }
    else if (key.equals(MOISTURE_TWO_AIR_VALUE)) {
      sensor2.airValue = value.toInt();
      Serial.printf("moistureTwoAirValue: %i\n", sensor2.airValue);
    }
    else if (key.equals(MOISTURE_TWO_WATER_VALUE)) {
      sensor2.waterValue = value.toInt();
      Serial.printf("moistureTwoWaterValue: %i\n", sensor2.waterValue);
    }
    else {
      Serial.println("No match: " + line);
    }
  }

  configFile.close();
  Serial.println();
}

boolean mountSdCard() {
  if (!SD.begin(5)) {
    Serial.println("Card Mount Failed");
    switchToErrorState(1);
  }
  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    switchToErrorState(1);
  }

  if (state != 0) {
    return false;
  }
  else {
    return true;
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  pinMode(25, INPUT_PULLUP);
  pinMode(26, INPUT_PULLUP);
  pinMode(27, INPUT_PULLUP);
  pinMode(34, INPUT_PULLDOWN);
  pinMode(35, INPUT_PULLDOWN);

  Serial.println("setup()");

  // LED blink setup
  pinMode(LED_PIN, OUTPUT);

  //Increment boot number and print it every reboot
  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));

  serialWakeUpCause();
  setTimeFromClock();
  logTime();

  mountSdCard();
  readConfig();

  if (state != 0) {
    showStatusWithLed();
    Serial.printf("Error state: %i \n", state);
    goToSleep();
  }
  else {
    controlActivity();
  }

}

void goToSleep() {

  int sleepSecs;
  if (state != 0) {
    sleepSecs = 3; // time to sleep in error state
  }
  else {
    sleepSecs = 300; // time to sleep in working state
  }
  esp_sleep_enable_timer_wakeup(sleepSecs * 1000000);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_25, 0); //1 = High, 0 = Low

  Serial.println("Going to sleep now.");
  delay(1000);
  esp_deep_sleep_start();
}

/**
   Executed in webserver active state
*/
void loop() {
  logTime();

  int awakeSeconds = 300;
  Serial.printf("Staying awake for %i seconds.\n", awakeSeconds);
  delay(awakeSeconds * 1000);

  server.end();
  WiFi.mode( WIFI_MODE_NULL );
  btStop();

  measurement();
  goToSleep();
}

/**
   Blinks LED at GPIO 2 with given interval, needs GPIO
   Needs LED_PIN defined and setup with pinMode(LED_PIN, OUTPUT);
   int count: how many times to blink
   int intervall: time in millis
*/
void blink(int count, int interval) {
  for (int pos = 0; pos < count; pos++) {
    digitalWrite(LED_PIN, HIGH);
    delay(interval);
    digitalWrite(LED_PIN, LOW);
    delay(interval);
  }
}

void switchToErrorState(int i) {
  Serial.println("In error state now.");
  state = i;
}

void showStatusWithLed() {
  if (state != 0) {
    blink(9 + state, 200);
  }
}

SoilMoistureMeasurement measureSoilMoisture(SoilMoistureSensor sensor) {

  SoilMoistureMeasurement result = {0, 0};

  // soilMoisture initial values
  int soilMoistureValue;
  int soilmoisturePercent;

  soilMoistureValue = analogRead(sensor.sensorPin);
  result.soilMoistureValue = soilMoistureValue;

  Serial.print("         Pin: ");
  Serial.print(sensor.sensorPin);
  Serial.print("  |  Analog value: ");
  Serial.print(soilMoistureValue);
  Serial.print("  |  Percentage: ");
  soilmoisturePercent = map(soilMoistureValue, sensor.airValue, sensor.waterValue, 0, 100);
  result.soilmoisturePercent = soilmoisturePercent;

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

  return result;
}

float measureDht22(DHT dhtSensor) {

  // read temperature in Celsius
  float tempC = dhtSensor.readTemperature();

  // check whether the reading is successful or not
  if ( isnan(tempC) ) {
    Serial.println("Failed to read from DHT sensor!");
    return ABSOLUTE_ZERO;
  } else {
    Serial.print("Temperature: ");
    Serial.print(tempC);
    Serial.println("°C");
    return tempC;
  }
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.printf("SSID: '%s' password: '%s' \n", ssid, password);
  Serial.println("AP mode: " + accessPoint);
  Serial.print("Connecting to WiFi ..");

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

void setupAccessPoint()
{
  Serial.print("Setting up Access Point.. ");
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("IP address: ");
  Serial.println(IP);
}

void startWiFi() {
  if (accessPoint) {
    setupAccessPoint();
  }
  else {
    connectToWiFi();
  }
}
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Blech vs. Rolle</title>
</head>
<body>
  <h1>Dammformblech vs. Gitterrolle</h1>
  <p>%TIMEPLACEHOLDER%</p>
  <p><a href ="./measurements.csv">measurements.csv</a></p>
</body>
</html>
)rawliteral";

String processor(const String& var){
  if(var == "TIMEPLACEHOLDER"){
    char buf[20];
    getFormattedDateTime(buf);
    String formattedDateTime = "";
    formattedDateTime += buf;
    return formattedDateTime;
  }
  return String();
}

void startWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });
  server.on(DATA_FILENAME, HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SD, DATA_FILENAME, "text/csv");
  });
  server.serveStatic("/", SD, "/");
  server.begin();

}
