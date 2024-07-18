// Weather icon credits: https://github.com/SeBassTian23/ESP32-WeatherDisplay/tree/master

// base class GxEPD2_GFX can be used to pass references or pointers to the display instance as parameter, uses ~1.2k more code
// enable or disable GxEPD2_GFX base class
#define ENABLE_GxEPD2_GFX 0

#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <Wire.h>  // Used to establish serial communication on the I2C bus
#include <SparkFun_TMP117.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"
#include "RTClib.h"
#include "image.h"  //for sleep icon
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include <BH1750.h>
#include <TimeLib.h>

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Preferences.h>

Preferences pref;
// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Wi-Fi Manager</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
html {
  font-family: Arial, Helvetica, sans-serif; 
  display: inline-block; 
  text-align: center;
}

h1 {
  font-size: 1.8rem; 
  color: white;
}

p { 
  font-size: 1.4rem;
}

.topnav { 
  overflow: hidden; 
  background-color: #0A1128;
}

body {  
  margin: 0;
}

.content { 
  padding: 5%;
}

.card-grid { 
  max-width: 800px; 
  margin: 0 auto; 
  display: grid; 
  grid-gap: 2rem; 
  grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
}

.card { 
  background-color: white; 
  box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5);
}

.card-title { 
  font-size: 1.2rem;
  font-weight: bold;
  color: #034078
}

input[type=submit] {
  border: none;
  color: #FEFCFB;
  background-color: #034078;
  padding: 15px 15px;
  text-align: center;
  text-decoration: none;
  display: inline-block;
  font-size: 16px;
  width: 100px;
  margin-right: 10px;
  border-radius: 4px;
  transition-duration: 0.4s;
  }

input[type=submit]:hover {
  background-color: #1282A2;
}

input[type=text], input[type=number], select {
  width: 50%;
  padding: 12px 20px;
  margin: 18px;
  display: inline-block;
  border: 1px solid #ccc;
  border-radius: 4px;
  box-sizing: border-box;
}

label {
  font-size: 1.2rem; 
}
.value{
  font-size: 1.2rem;
  color: #1282A2;  
}
.state {
  font-size: 1.2rem;
  color: #1282A2;
}
button {
  border: none;
  color: #FEFCFB;
  padding: 15px 32px;
  text-align: center;
  font-size: 16px;
  width: 100px;
  border-radius: 4px;
  transition-duration: 0.4s;
}
.button-on {
  background-color: #034078;
}
.button-on:hover {
  background-color: #1282A2;
}
.button-off {
  background-color: #858585;
}
.button-off:hover {
  background-color: #252524;
} 
  </style>
</head>
<body>
  <div class="topnav">
    <h1>Weather Station Wi-Fi Manager</h1>
  </div>
  <div class="content">
    <div class="card-grid">
      <div class="card">
        <form action="/" method="POST">
          <p>
            <label for="ssid">SSID</label>
            <input type="text" id ="ssid" name="ssid"><br>
            <label for="pass">Password</label>
            <input type="text" id ="pass" name="pass"><br>
            <input type ="submit" value ="Submit">
          </p>
        </form>
      </div>
    </div>
  </div>
</body>
</html>
)rawliteral";

// Search for parameter in HTTP POST request
const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";

//your wifi name and password
String ssid;
String password;

// openWeatherMap Api Key from your profile in account section
String openWeatherMapApiKey="";

// Replace with your lat and lon
String lat = "22.5895515";
String lon = "88.2876455";

RTC_DS3231 rtc;  //Initalize rtc

TMP117 sensor;            // Initalize temperature sensor
Adafruit_BME680 bme;      // Initalize environmental sensor
BH1750 lightMeter(0x23);  //Initalize light sensor

GxEPD2_3C<GxEPD2_420c_Z21, GxEPD2_420c_Z21::HEIGHT> display(GxEPD2_420c_Z21(/*CS=5*/ SS, /*DC=*/3, /*RST=*/4, /*BUSY=*/5));  //400x300, UC8276
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

//#define SEALEVELPRESSURE_HPA (1013.25)
#define BATPIN A0  //battery voltage divider connection pin (1M Ohm with 104 Capacitor)
#define DEBUG_PIN 21
#define BATTERY_LEVEL_SAMPLING 32
#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */
int TIME_TO_SLEEP = 900;

//battery related settings
#define battChangeThreshold 0.15
#define battUpperLim 4.19
#define battHigh 4.2
#define battLow 2.9

int nightFlag = 0;  //preserves data in rtc memory from deep sleep loss
float battLevel;
bool DEBUG_MODE = false, BATTERY_CRITICAL = false;

String jsonBuffer;

//for storing highest temp and lowest temp of the day
float hTemp, lTemp;

char daysOfTheWeek[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
char monthName[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

int httpResponseCode;

//takes samples based on BATTERY_LEVEL_SAMPLING, averages them and returns actual battery voltage
float batteryLevel() {
  uint32_t Vbatt = 0;
  for (int i = 0; i < BATTERY_LEVEL_SAMPLING; i++) {
    Vbatt = Vbatt + analogReadMilliVolts(BATPIN);  // ADC with correction
  }
  float Vbattf = 2 * Vbatt / BATTERY_LEVEL_SAMPLING / 1000.0;  // attenuation ratio 1/2, mV --> V
  //Serial.println(Vbattf);
  return (Vbattf);
}

//forward declaration
void tempPrint(byte offset = 0);

void setup() {
  Serial.begin(115200);
  Serial.println("Setup");
  pinMode(BATPIN, INPUT);
  pinMode(DEBUG_PIN, INPUT);
  /*if (digitalRead(DEBUG_PIN) == 1)
    DEBUG_MODE = true;*/
  Wire.begin();
  Wire.setClock(400000);  // Set clock speed to be the fastest for better communication (fast mode)

  display.init(115200, true, 2, false);  // USE THIS for Waveshare boards with "clever" reset circuit, 2ms reset pulse

  u8g2Fonts.begin(display);  // connect u8g2 procedures to Adafruit GFX

  pref.begin("database", false);
  //preferences.end();
  BATTERY_CRITICAL = pref.isKey("battCrit");
  if (!BATTERY_CRITICAL)
    pref.putBool("battCrit", "");
  BATTERY_CRITICAL = pref.getBool("battCrit", false);
  bool tempBATTERY_CRITICAL = BATTERY_CRITICAL;

  bool checkFlag = pref.isKey("nightFlag");
  if (!checkFlag) {  //create key:value pair
    pref.putBool("nightFlag", "");
  }
  nightFlag = pref.getBool("nightFlag", false);

  if (!BATTERY_CRITICAL) {
    bool wifiConfigExist = pref.isKey("ssid");
    if (!wifiConfigExist) {  //create key:value pairs
      pref.putString("ssid", "");
      pref.putString("password", "");
    }

    ssid = pref.getString("ssid", "");
    password = pref.getString("password", "");

    if (ssid == "" || password == "") {
      Serial.println("No values saved for ssid or password");
      // Connect to Wi-Fi network with SSID and password
      Serial.println("Setting AP (Access Point)");
      // NULL sets an open Access Point
      WiFi.softAP("WCLOCK-WIFI-MANAGER", "79756622761");

      IPAddress IP = WiFi.softAPIP();
      Serial.print("AP IP address: ");
      Serial.println(IP);

      debugPrinter("Connect to 'WCLOCK-WIFI-MANAGER' \nfrom your phone or computer (Wifi).\nUse password 79756622761.\nThen go to " + IP.toString() + "\nfrom your browser.");

      // Web Server Root URL
      server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", index_html);
      });

      server.on("/", HTTP_POST, [](AsyncWebServerRequest* request) {
        int params = request->params();
        for (int i = 0; i < params; i++) {
          const AsyncWebParameter* p = request->getParam(i);
          if (p->isPost()) {
            // HTTP POST ssid value
            if (p->name() == PARAM_INPUT_1) {
              ssid = p->value();
              Serial.print("SSID set to: ");
              Serial.println(ssid);
              pref.putString("ssid", ssid);
            }
            // HTTP POST pass value
            if (p->name() == PARAM_INPUT_2) {
              password = p->value();
              Serial.print("Password set to: ");
              Serial.println(password);
              pref.putString("password", password);
            }
            //Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
          }
        }
        request->send(200, "text/html", "<h2>Done. Weather Station will now restart</h2>");
        delay(3000);
        ESP.restart();
      });
      server.begin();
      while (true)
        ;
    }
  }
  if (lightMeter.begin(BH1750::ONE_TIME_HIGH_RES_MODE)) {
    Serial.println(F("BH1750 Advanced begin"));
  } else {
    Serial.println(F("Error initialising BH1750"));
    errMsg("Error BH1750");
    while (1)
      ;  // Runs forever
  }
  float lux = 0;
  while (!lightMeter.measurementReady(true)) {
    yield();
  }
  lux = lightMeter.readLightLevel();
  Serial.print("Light: ");
  Serial.print(lux);
  Serial.println(" lx");

  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    errMsg("Error RTC");
    while (1)
      ;  // Runs forever
  }
  Serial.println("RTC Ready");

  DateTime now = rtc.now();

  if ((now.hour() == 0) && (now.minute() >= 0 && now.minute() < 15)) {  //reset high low at midnight
    pref.putFloat("hTemp", 0.0);
    pref.putFloat("lTemp", 60.0);
  }

  if (lux != 0 || DEBUG_MODE == true) {
    if (sensor.begin() == true)  // Function to check if the sensor will correctly self-identify with the proper Device ID/Address
    {
      Serial.println("Lux Begin");
    } else {
      Serial.println("Device failed to setup- Freezing code.");
      errMsg("Error TMP117");
      while (1)
        ;  // Runs forever
    }

    if (!bme.begin()) {
      Serial.println(F("Could not find a valid BME680 sensor, check wiring!"));
      errMsg("Error BME680");
      while (1)
        ;  // Runs forever
    }

    Serial.println("BME Ready");

    // Set up oversampling and filter initialization
    bme.setTemperatureOversampling(BME680_OS_2X);
    bme.setHumidityOversampling(BME680_OS_16X);
    bme.setPressureOversampling(BME680_OS_16X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_7);
    bme.setGasHeater(0, 0);  // 0*C for 0 ms

    if (!BATTERY_CRITICAL) {
      WiFi.mode(WIFI_STA);
      WiFi.begin(ssid.c_str(), password.c_str());
      while (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("Connection Failed");
        break;
      }

      bool apiConfigExist = pref.isKey("api");
      if (!apiConfigExist)  //create key:value pairs
        pref.putString("api", openWeatherMapApiKey);

      openWeatherMapApiKey = pref.getString("api", "");
    } else {
      //wifioff cpu speed reduced
      WiFi.disconnect(true);  // Disconnect from the network
      WiFi.mode(WIFI_OFF);    // Switch WiFi off
      setCpuFrequencyMhz(40);
    }

    hTemp = pref.getFloat("hTemp", -1.0);
    lTemp = pref.getFloat("lTemp", -1.0);
    battLevel = pref.getFloat("battLevel", -1.0);
    if (hTemp == -1.0 || lTemp == -1.0 || battLevel == -1.0) {
      Serial.println("No values saved for hTemp, lTemp or battLevel");
      pref.putFloat("hTemp", 0.0);
      pref.putFloat("lTemp", 60.0);
      pref.putFloat("battLevel", 4.2);
    }
  }
  float hTempHold = hTemp, lTempHold = lTemp, tempBattLevel = battLevel;
  bool tempNightFlag = nightFlag;

  Serial.println("Setup done");

  if (DEBUG_MODE) {
    errMsg("DEBUG MODE");
  } else {
    if (lux == 0) {
      TIME_TO_SLEEP = 300;   //5 min wake period while darkness sleeping
      if (nightFlag == 0) {  //prevents unnecessary redrawing of same thing
        nightFlag = 1;
        display.setRotation(0);
        display.setFullWindow();
        display.firstPage();
        do {
          display.fillScreen(GxEPD_WHITE);
          display.drawInvertedBitmap(0, 0, nightMode, 400, 300, GxEPD_BLACK);
        } while (display.nextPage());
      }
      display.hibernate();
      display.powerOff();
    } else {
      nightFlag = 0;
      display.setRotation(0);
      display.setFullWindow();
      display.firstPage();
      do {
        display.fillScreen(GxEPD_WHITE);
        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("Time And Weather");
          tempPrint();
          openWeatherPrint();
          Serial.println("Time And Weather Done");
        } else {
          Serial.println("Time");
          display.drawBitmap(270, 0, wifiOff, 12, 12, GxEPD_BLACK);
          tempPrint(40);
          Serial.println("Time Done");
        }
      } while (display.nextPage());
      display.hibernate();
      display.powerOff();
    }

    Serial.println("Data Write");

    if (lux != 0) {
      if (hTempHold != hTemp)
        pref.putFloat("hTemp", hTemp);
      if (lTempHold != lTemp)
        pref.putFloat("lTemp", lTemp);
      if (tempBattLevel != battLevel)
        pref.putFloat("battLevel", battLevel);
      if (tempBATTERY_CRITICAL != BATTERY_CRITICAL)
        pref.putBool("battCrit", BATTERY_CRITICAL);
    }
    if (tempNightFlag != nightFlag)
      pref.putBool("nightFlag", nightFlag);

    Serial.println("Data Write Done");

    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP / 60) + " Mins");
    //Go to sleep now
    Serial.println("Going to sleep now");
    Serial.flush();
    delay(100);
    esp_deep_sleep_start();
  }
}

void loop() {}

//Handles the httpresponse and returns the data as json payload
String httpGETRequest(const char* serverName) {
  WiFiClient client;
  HTTPClient http;

  // Your Domain name with URL path or IP address with path
  http.begin(client, serverName);

  // Send HTTP POST request
  httpResponseCode = http.GET();

  String payload = "{}";

  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();

  return payload;
}

/*tempPrint function prints internal sensor based readings. If offset is provided then it shifts in y axis.
Provide offset when wifi is not connected and online weather data cannot be printed.*/
void tempPrint(byte offset) {
  uint16_t bg = GxEPD_WHITE;
  uint16_t fg = GxEPD_BLACK;
  u8g2Fonts.setFontMode(1);          // use u8g2 transparent mode (this is default)
  u8g2Fonts.setFontDirection(0);     // left to right (this is default)
  u8g2Fonts.setForegroundColor(fg);  // apply Adafruit GFX color
  u8g2Fonts.setBackgroundColor(bg);  // apply Adafruit GFX color
                                     // select u8g2 font from here: https://github.com/olikraus/u8g2/wiki/fntlistall
  float tempC;
  if (sensor.dataReady() == true)  // Function to make sure that there is data ready to be printed, only prints temperature values when data is ready
  {
    tempC = sensor.readTempC();
    Serial.println();  // Create a white space for easier viewing
    Serial.print("Temperature in Celsius: ");
    Serial.println(tempC);
    if (tempC > hTemp)
      hTemp = tempC;
    if (tempC < lTemp)
      lTemp = tempC;
  }
  
  float newBattLevel = batteryLevel();
  if (newBattLevel < battLevel)  //to maintain steady decrease in battery level
    battLevel = newBattLevel;

  if (((newBattLevel - battLevel) >= battChangeThreshold) || newBattLevel > battUpperLim)  //to update the battery level in case of charging
    battLevel = newBattLevel;

  u8g2Fonts.setFont(u8g2_font_luRS08_tf);
  u8g2Fonts.setCursor(28, 11);
  u8g2Fonts.print(battLevel, 2);
  u8g2Fonts.print("V");

  int percent = ((battLevel - battLow) / (battHigh - battLow)) * 100;  //range is battHigh - 100% and battLow - 0%
  BATTERY_CRITICAL = false;
  if (percent < 1) {
    BATTERY_CRITICAL = true;
    percent = 0;  //for battry icon
  } else if (percent > 100)
    percent = 100;

  u8g2Fonts.setCursor(63, 11);
  if (!BATTERY_CRITICAL) {
    u8g2Fonts.print(percent, 1);
    u8g2Fonts.print("%");
  } else
    u8g2Fonts.print("BATTERY CRITICAL, WIFI TURNED OFF");
  iconBattery(percent);

  DateTime now = rtc.now();

  u8g2Fonts.setFont(u8g2_font_luRS08_tf);
  u8g2Fonts.setCursor(295, 11);
  u8g2Fonts.print("Last Update: ");
  u8g2Fonts.print(now.hour() < 10 ? "0" + String(now.hour()) : now.hour());
  u8g2Fonts.print(now.minute() < 10 ? ":0" + String(now.minute()) : ":" + String(now.minute()));

  u8g2Fonts.setFont(u8g2_font_logisoso20_tf);
  u8g2Fonts.setCursor(10, 75 + offset);
  u8g2Fonts.print(now.day() < 10 ? "0" + String(now.day()) : now.day());
  u8g2Fonts.print(", ");
  u8g2Fonts.print(monthName[now.month() - 1]);
  u8g2Fonts.setCursor(10, 105 + offset);
  u8g2Fonts.print(daysOfTheWeek[now.dayOfTheWeek()]);

  u8g2Fonts.setFont(u8g2_font_inb19_mf);
  u8g2Fonts.setCursor(320, 60 + offset);  // start writing at this position
  u8g2Fonts.print("o");

  u8g2Fonts.setFont(u8g2_font_logisoso58_tf);
  u8g2Fonts.setCursor(150, 110 + offset);  // start writing at this position

  u8g2Fonts.print(String(tempC));

  u8g2Fonts.setFont(u8g2_font_logisoso58_tf);
  u8g2Fonts.setCursor(330, 110 + offset);
  u8g2Fonts.print(String("C"));

  if (!BATTERY_CRITICAL) {
    display.drawLine(0, 121 + offset, 400, 121 + offset, GxEPD_RED);
    display.drawLine(0, 122 + offset, 400, 122 + offset, GxEPD_RED);

    display.drawLine(0, 154 + offset, 400, 154 + offset, GxEPD_RED);
    display.drawLine(0, 155 + offset, 400, 155 + offset, GxEPD_RED);
  } else {
    display.drawLine(0, 121 + offset, 400, 121 + offset, GxEPD_BLACK);
    display.drawLine(0, 122 + offset, 400, 122 + offset, GxEPD_BLACK);

    display.drawLine(0, 154 + offset, 400, 154 + offset, GxEPD_BLACK);
    display.drawLine(0, 155 + offset, 400, 155 + offset, GxEPD_BLACK);
  }

  unsigned long endTime = bme.beginReading();
  if (endTime == 0) {
    Serial.println(F("Failed to begin reading :("));
    return;
  }
  if (!bme.endReading()) {
    Serial.println(F("Failed to complete reading :("));
    return;
  }

  u8g2Fonts.setFont(u8g2_font_logisoso20_tf);
  u8g2Fonts.setCursor(2, 150 + offset);  // start writing at this position
  u8g2Fonts.print(bme.humidity);
  //u8g2Fonts.setCursor(67, 200);
  u8g2Fonts.print(String("%"));

  u8g2Fonts.setCursor(264, 150 + offset);  // start writing at this position
  u8g2Fonts.print(bme.pressure / 100.0);
  //u8g2Fonts.setCursor(186, 200);
  u8g2Fonts.print(String("hPa"));

  u8g2Fonts.setFont(u8g2_font_logisoso16_tf);
  u8g2Fonts.setCursor(85, 148 + offset);  // start writing at this position
  u8g2Fonts.print("H:");
  //u8g2Fonts.setCursor(30, 250);  // start writing at this position
  u8g2Fonts.print(hTemp);
  u8g2Fonts.setFont(u8g2_font_fub11_tf);
  u8g2Fonts.setCursor(148, 138 + offset);  // start writing at this position
  u8g2Fonts.print("o");
  u8g2Fonts.setFont(u8g2_font_logisoso16_tf);
  u8g2Fonts.setCursor(158, 148 + offset);  // start writing at this position
  u8g2Fonts.print("C");

  u8g2Fonts.setCursor(180, 148 + offset);  // start writing at this position
  u8g2Fonts.print("L:");
  u8g2Fonts.setFont(u8g2_font_logisoso16_tf);
  //u8g2Fonts.setCursor(30, 280);  // start writing at this position
  u8g2Fonts.print(lTemp);
  u8g2Fonts.setFont(u8g2_font_fub11_tf);
  u8g2Fonts.setCursor(242, 138 + offset);  // start writing at this position
  u8g2Fonts.print("o");
  u8g2Fonts.setFont(u8g2_font_logisoso16_tf);
  u8g2Fonts.setCursor(252, 148 + offset);  // start writing at this position
  u8g2Fonts.print("C");
}

//works only if wifi is connected. Prints data from openweather api.
void openWeatherPrint() {
  String serverPath = "http://api.openweathermap.org/data/3.0/onecall?lat=" + lat + "&lon=" + lon + "&exclude=hourly,minutely&units=metric&appid=" + openWeatherMapApiKey;

  jsonBuffer = httpGETRequest(serverPath.c_str());
  if (httpResponseCode == -1 || httpResponseCode == -11)
    ESP.restart();
  Serial.println(jsonBuffer);
  JSONVar myObject = JSON.parse(jsonBuffer);

  // JSON.typeof(jsonVar) can be used to get the type of the var
  if (JSON.typeof(myObject) == "undefined") {
    Serial.println("Parsing input failed!");
    ESP.restart();
    return;
  }

  /*Serial.print("JSON object = ");
  Serial.println(myObject);
  Serial.print("Temperature: ");
  Serial.println(myObject["current"]["temp"]);
  Serial.print("Pressure: ");
  Serial.println(myObject["current"]["pressure"]);
  Serial.print("Humidity: ");
  Serial.println(myObject["current"]["humidity"]);*/

  if (myObject["current"]["temp"] == null) {
    networkInfo();
  } else {
    wifiStatus();
    u8g2Fonts.setFont(u8g2_font_helvB10_tf);
    u8g2Fonts.setCursor(29, 170);
    u8g2Fonts.print("OUTDOOR");
    u8g2Fonts.setFont(u8g2_font_fub20_tf);  //u8g2_font_fub30_tf
    uint16_t width;
    width = u8g2Fonts.getUTF8Width(JSON.stringify(myObject["current"]["temp"]).c_str());
    u8g2Fonts.setCursor(20, 200);  // start writing at this position
    u8g2Fonts.print(myObject["current"]["temp"]);
    u8g2Fonts.setCursor(30 + width, 200);
    u8g2Fonts.print("C");
    u8g2Fonts.setFont(u8g2_font_fub11_tf);
    u8g2Fonts.setCursor(22 + width, 185);  // start writing at this position
    u8g2Fonts.print("o");

    u8g2Fonts.setFont(u8g2_font_fur11_tf);  //u8g2_font_fur14_tf
    width = u8g2Fonts.getUTF8Width(("Real Feel:" + JSON.stringify(myObject["current"]["feels_like"])).c_str());
    u8g2Fonts.setCursor(5, 220);  // start writing at this position
    u8g2Fonts.print("Real Feel:");
    u8g2Fonts.setCursor(75, 220);
    u8g2Fonts.print(myObject["current"]["feels_like"]);
    u8g2Fonts.setCursor(width + 16, 220);
    u8g2Fonts.print(String("C"));
    u8g2Fonts.setFont(u8g2_font_baby_tf);  //u8g2_font_robot_de_niro_tf
    u8g2Fonts.setCursor(13 + width, 211);  // start writing at this position
    u8g2Fonts.print("o");

    u8g2Fonts.setFont(u8g2_font_fur14_tf);
    u8g2Fonts.setCursor(5, 245);  // start writing at this position
    u8g2Fonts.print(myObject["current"]["humidity"]);
    u8g2Fonts.print(String("%"));

    u8g2Fonts.setCursor(5, 270);  // start writing at this position
    u8g2Fonts.print(myObject["current"]["pressure"]);
    u8g2Fonts.print(String("hPa"));
    u8g2Fonts.setFont(u8g2_font_helvB10_tf);
    u8g2Fonts.setCursor(5, 294);  // start writing at this position
    u8g2Fonts.print("UVI: ");
    u8g2Fonts.print(myObject["current"]["uvi"]);
    u8g2Fonts.setFont(u8g2_font_fur11_tf);
    double uv = double(myObject["current"]["uvi"]);
    if (uv < 2)
      u8g2Fonts.print(" Low");
    else if (uv < 5)
      u8g2Fonts.print(" Medium");
    else if (uv <= 7)
      u8g2Fonts.print(" High");
    else if (uv > 7)
      u8g2Fonts.print(" Danger");
    display.drawLine(136, 155, 136, 299, GxEPD_RED);
    display.drawLine(137, 155, 137, 299, GxEPD_RED);

    //Sunset sunrise print
    time_t t = strtoll(JSON.stringify(myObject["current"]["sunrise"]).c_str(), nullptr, 10);
    setTime(t);
    adjustTime(19800);
    iconSunRise(152, 170, true);
    u8g2Fonts.setCursor(166, 175);  // start writing at this position
    u8g2Fonts.print("0");
    u8g2Fonts.print(hour());
    u8g2Fonts.print(":");
    u8g2Fonts.print(minute() < 10 ? "0" + String(minute()) : minute());

    t = strtoll(JSON.stringify(myObject["current"]["sunset"]).c_str(), nullptr, 10);
    setTime(t);
    adjustTime(19800);
    iconSunRise(267, 170, false);
    u8g2Fonts.setCursor(281, 175);
    u8g2Fonts.print(hour());
    u8g2Fonts.print(":");
    u8g2Fonts.print(minute() < 10 ? "0" + String(minute()) : minute());

    display.drawLine(320, 155, 320, 299, GxEPD_RED);
    display.drawLine(321, 155, 321, 299, GxEPD_RED);
    display.drawLine(320, 230, 400, 230, GxEPD_RED);
    display.drawLine(320, 231, 400, 231, GxEPD_RED);

    iconMoonPhase(360, 260, 20, double(myObject["daily"][0]["moon_phase"]));
    u8g2Fonts.setFont(u8g2_font_luRS08_tf);
    u8g2Fonts.setCursor(330, 297);
    u8g2Fonts.print("Moon Phase");

    String s = JSON.stringify(myObject["current"]["weather"][0]["icon"]);
    int lastIndex = s.length() - 1;
    s.remove(lastIndex);
    s.remove(0, 1);

    if (s == "01d") {  //Clear Day
      iconSun(361, 189, 15);
      //iconSleet(x,y,r);//iconHail(x,y,r);//same
      //iconWind(x,y,r);
      //iconTornado(x,y,r);
    } else if (s == "01n")  //Clear Night
      iconMoon(361, 189, 15);
    else if (s == "02d")  //few clouds
      iconCloudyDay(330, 160, 60);
    else if (s == "02n")
      iconCloudyNight(330, 160, 60);
    else if (s == "03d")  //scattered clouds
      iconCloud(361, 189, 15);
    else if (s == "03n")
      iconCloud(361, 189, 15);
    else if (s == "04d")  //broken clouds (two clouds)
      iconCloudy(330, 160, 60);
    else if (s == "04n")
      iconCloudy(330, 160, 60);
    else if (s == "09d")  //shower rain
      iconSleet(330, 160, 60);
    else if (s == "09n")
      iconSleet(330, 160, 60);
    else if (s == "10d")  //snow
      iconRain(330, 160, 60);
    else if (s == "10n")
      iconRain(330, 160, 60);
    else if (s == "11d")  //thunderstorm
      iconThunderstorm(330, 160, 60);
    else if (s == "11n")
      iconThunderstorm(330, 160, 60);
    else if (s == "13d")  //snow
      iconSnow(330, 160, 60);
    else if (s == "13n")
      iconSnow(330, 160, 60);
    else if (s == "50d")  //mist
      iconFog(330, 160, 60);
    else if (s == "50n")
      iconFog(330, 160, 60);

    u8g2Fonts.setFont(u8g2_font_luRS08_tf);  //u8g2_font_fur11_tf
    s = JSON.stringify(myObject["current"]["weather"][0]["main"]);
    lastIndex = s.length() - 1;
    s.remove(lastIndex);
    s.remove(0, 1);
    u8g2Fonts.setCursor(330, 227);
    u8g2Fonts.print(s);

    //u8g2Fonts.setCursor(186, 200);
    s = JSON.stringify(myObject["alerts"][0]["event"]);
    lastIndex = s.length() - 1;
    s.remove(lastIndex);
    s.remove(0, 1);
    if (s != "ul") {
      int16_t tbx, tby;
      uint16_t tbw, tbh;
      display.getTextBounds("Alerts: " + s, 0, 0, &tbx, &tby, &tbw, &tbh);
      // center the bounding box by transposition of the origin:
      uint16_t x = ((display.width() - tbw) / 2) - tbx;
      u8g2Fonts.setCursor(x, 25);  // start writing at this position
      u8g2Fonts.print("Alerts: ");
      u8g2Fonts.print(s);
    }
  }
}

//Separate the icons in future update to separate file

void iconCloud(uint16_t x, uint16_t y, uint16_t r) {
  // top circle
  display.fillCircle(x, y, r, GxEPD_BLACK);
  // left circle
  display.fillCircle(x - r * 0.85, y + r * 0.8, r * 0.85, GxEPD_BLACK);
  // right circle
  display.fillCircle(x + r * 1.1, y + r * 0.8, r * 0.85, GxEPD_BLACK);
  // rectangle
  display.fillRect(x - r * 0.85, y + r * 0.8, (x + r * 1.1) - (x - r * 0.85), r * 0.9, GxEPD_BLACK);

  // top circle
  float offset = 0.8;
  display.fillCircle(x, y, r * offset, GxEPD_WHITE);
  // left circle
  display.fillCircle(x - r * 0.85, y + r * 0.8, r * 0.85 * offset, GxEPD_WHITE);
  // right circle
  display.fillCircle(x + r * 1.1, y + r * 0.8, r * 0.85 * offset, GxEPD_WHITE);
  // rectangle
  display.fillRect(x - r * 0.85, y + r * 0.7, (x + r * 1.1) - (x - r * 0.85), r * offset, GxEPD_WHITE);
}

void iconSun(uint16_t x, uint16_t y, uint16_t r) {
  display.drawLine(x - r * 1.75, y, x + r * 1.75, y, GxEPD_BLACK);
  display.drawLine(x, y - r * 1.75, x, y + r * 1.75, GxEPD_BLACK);
  display.drawLine(x - r * 1.25, y - r * 1.25, x + r * 1.25, y + r * 1.25, GxEPD_BLACK);
  display.drawLine(x - r * 1.25, y + r * 1.25, x + r * 1.25, y - r * 1.25, GxEPD_BLACK);
  display.fillCircle(x, y, r * 1.2, GxEPD_WHITE);
  display.fillCircle(x, y, r, GxEPD_BLACK);
  float offset = 0.9;
  display.fillCircle(x, y, r * offset, GxEPD_RED);
}

void iconMoon(uint16_t x, uint16_t y, uint16_t r) {
  float offset = 0.9;
  display.fillCircle(x, y, r, GxEPD_BLACK);
  display.fillCircle(x, y, r * offset, GxEPD_RED);
  display.fillCircle(x + r, y - r, r, GxEPD_BLACK);
  display.fillCircle(x + r, y - r, r * offset, GxEPD_WHITE);
  display.fillRect(x, y - r * 2, r * 2.5, r, GxEPD_WHITE);
  display.fillRect(x + r + 1, y - r, r * 1.5, r * 1.5, GxEPD_WHITE);
}

void iconClearDay(uint16_t x, uint16_t y, uint16_t s) {
  iconSun(x + s / 2, y + s / 2, s / 5);
}

void iconClearNight(uint16_t x, uint16_t y, uint16_t s) {
  iconMoon(x + s / 2, y + s / 2, s / 5);
}

void iconRain(uint16_t x, uint16_t y, uint16_t s) {
  iconCloud(x + s / 2.2, y + s / 2.5, s / 5);
  display.fillRect(x + s * 0.275, y + s * 0.6, s / 2.5, s / 5, GxEPD_WHITE);

  float offset = 0.8;
  for (int i = 0; i <= s * 0.1; i++) {
    display.fillCircle(x + s * 0.4 - i * 0.5, y + s * 0.65 + i, s * 0.02, GxEPD_BLACK);
    display.fillCircle(x + s * 0.6 - i * 0.5, y + s * 0.65 + i, s * 0.02, GxEPD_BLACK);
  }
  for (int i = 0; i <= s * 0.16; i++) {
    display.fillCircle(x + s * 0.5 - i * 0.5, y + s * 0.65 + i, s * 0.02, GxEPD_BLACK);
  }
}

void iconSleet(uint16_t x, uint16_t y, uint16_t s) {
  iconCloud(x + s / 2.2, y + s / 2.5, s / 5);
  display.fillRect(x + s * 0.275, y + s * 0.6, s / 2.5, s / 5, GxEPD_WHITE);

  float offset = 0.8;
  for (int i = 0; i <= s * 0.1; i++) {
    if (i < 1 || i > s * 0.1 * 0.5) {
      display.fillCircle(x + s * 0.4 - i * 0.5, y + s * 0.65 + i, s * 0.02, GxEPD_BLACK);
      display.fillCircle(x + s * 0.6 - i * 0.5, y + s * 0.65 + i, s * 0.02, GxEPD_BLACK);
    }
  }
  for (int i = 0; i <= s * 0.16; i++) {
    if (i < s * 0.16 * 0.5 || i > s * 0.16 * 0.8)
      display.fillCircle(x + s * 0.5 - i * 0.5, y + s * 0.65 + i, s * 0.02, GxEPD_BLACK);
  }
}

void iconSnow(uint16_t x, uint16_t y, uint16_t s) {
  iconCloud(x + s / 2.2, y + s / 2.5, s / 5);
  display.fillRect(x + s * 0.275, y + s * 0.6, s / 2.5, s / 5, GxEPD_WHITE);

  float offset = 0.8;
  display.fillCircle(x + s / 2.75, y + s * 0.7, s * 0.02, GxEPD_BLACK);
  display.fillCircle(x + s / 1.75, y + s * 0.7, s * 0.02, GxEPD_BLACK);

  display.fillCircle(x + s / 2.75, y + s * 0.8, s * 0.02, GxEPD_BLACK);
  display.fillCircle(x + s / 1.75, y + s * 0.8, s * 0.02, GxEPD_BLACK);

  display.fillCircle(x + s / 2.15, y + s * 0.65, s * 0.02, GxEPD_BLACK);
  display.fillCircle(x + s / 2.15, y + s * 0.75, s * 0.02, GxEPD_BLACK);
  display.fillCircle(x + s / 2.15, y + s * 0.85, s * 0.02, GxEPD_BLACK);
}

void iconWind(uint16_t x, uint16_t y, uint16_t s) {
  float offset = 0.8;
  for (int i = 0; i <= s * 0.7; i++) {
    if (i < s * 0.6)
      display.fillCircle(x + s * 0.15 + i, y + s * 0.4, s * 0.02, GxEPD_BLACK);
    if (i < s * 0.5)
      display.fillCircle(x + s * 0.1 + i, y + s * 0.5, s * 0.02, GxEPD_BLACK);
    if (i < s * 0.2)
      display.fillCircle(x + s * 0.7 + i, y + s * 0.5, s * 0.02, GxEPD_BLACK);
    if (i < s * 0.6)
      display.fillCircle(x + s * 0.2 + i, y + s * 0.6, s * 0.02, GxEPD_BLACK);
  }
}

void iconFog(uint16_t x, uint16_t y, uint16_t s) {
  iconCloud(x + s / 2.2, y + s / 2.5, s / 5);
  display.fillRect(x + s * 0.1, y + s * 0.55, s * 0.75, s / 5, GxEPD_WHITE);

  float offset = 0.8;
  for (int i = 0; i <= s * 0.7; i++) {
    display.fillCircle(x + s * 0.1 + i, y + s * 0.6, s * 0.02, GxEPD_BLACK);
    display.fillCircle(x + s * 0.2 + i, y + s * 0.7, s * 0.02, GxEPD_BLACK);
    display.fillCircle(x + s * 0.15 + i, y + s * 0.8, s * 0.02, GxEPD_BLACK);
  }
}

void iconCloudy(uint16_t x, uint16_t y, uint16_t s) {
  iconCloud(x + (s / 4) * 3, y + s / 4, s / 10);
  iconCloud(x + s / 2.1, y + s / 2.2, s / 5);
}

void iconCloudyDay(uint16_t x, uint16_t y, uint16_t s) {
  iconSun(x + (s / 3) * 2, y + s / 2.5, s / 6);
  iconCloud(x + s / 2.2, y + s / 2.2, s / 5);
}

void iconCloudyNight(uint16_t x, uint16_t y, uint16_t s) {
  iconMoon(x + (s / 3) * 2, y + s / 3, s / 6);
  iconCloud(x + s / 2.2, y + s / 2.2, s / 5);
}

void iconHail(uint16_t x, uint16_t y, uint16_t s) {
  iconCloud(x + s / 2.2, y + s / 2.5, s / 5);
  display.fillRect(x + s * 0.275, y + s * 0.6, s / 2.5, s / 5, GxEPD_WHITE);

  float offset = 0.8;
  for (int i = 0; i <= s * 0.1; i++) {
    if (i < s * 0.1 * 0.5 || i == s * 0.1) {
      display.fillCircle(x + s * 0.4 - i * 0.5, y + s * 0.65 + i, s * 0.02, GxEPD_BLACK);
      display.fillCircle(x + s * 0.6 - i * 0.5, y + s * 0.65 + i, s * 0.02, GxEPD_BLACK);
    }
  }
  for (int i = 0; i <= s * 0.16; i++) {
    if (i < s * 0.16 * 0.7 || i == s * 0.16)
      display.fillCircle(x + s * 0.5 - i * 0.5, y + s * 0.65 + i, s * 0.02, GxEPD_BLACK);
  }
}

void iconThunderstorm(uint16_t x, uint16_t y, uint16_t s) {
  iconCloud(x + s / 2.2, y + s / 2.5, s / 5);
  display.fillRect(x + s * 0.275, y + s * 0.6, s / 2.5, s / 5, GxEPD_WHITE);

  float offset = 0.8;
  for (int i = 0; i <= s * 0.1; i++) {
    display.fillCircle(x + s * 0.6 - i * 0.5, y + s * 0.65 + i, s * 0.02, GxEPD_BLACK);
  }
  for (int i = 0; i <= s * 0.16; i++) {
    display.fillCircle(x + s * 0.5 - i * 0.5, y + s * 0.65 + i, s * 0.02, GxEPD_BLACK);
  }
  display.fillTriangle(x + s * 0.3, y + s * 0.75, x + s * 0.325, y + s * 0.65, x + s * 0.375, y + s * 0.65, GxEPD_RED);
  display.fillTriangle(x + s * 0.3, y + s * 0.75, x + s * 0.4, y + s * 0.7, x + s * 0.33, y + s * 0.7, GxEPD_RED);
  display.fillTriangle(x + s * 0.3, y + s * 0.85, x + s * 0.35, y + s * 0.7, x + s * 0.4, y + s * 0.7, GxEPD_RED);
}

void fillEllipsis(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c) {
  for (int yi = -h; yi <= h; yi++) {
    for (int xi = -w; xi <= w; xi++) {
      if (xi * xi * h * h + yi * yi * w * w <= h * h * w * w)
        display.writePixel(x + xi, y + yi, c);
    }
  }
}

void iconTornado(uint16_t x, uint16_t y, uint16_t s) {
  // 1
  fillEllipsis(x + s * 0.33, y + s * 0.7, s / 12 * 1.2, s / 18 * 1.2, GxEPD_BLACK);
  fillEllipsis(x + s * 0.33, y + s * 0.7, s / 12, s / 18, GxEPD_WHITE);
  // 2
  fillEllipsis(x + s * 0.32, y + s * 0.65, s / 9 * 1.2, s / 16 * 1.2, GxEPD_BLACK);
  fillEllipsis(x + s * 0.32, y + s * 0.65, s / 9, s / 16, GxEPD_WHITE);
  // 3
  fillEllipsis(x + s * 0.35, y + s * 0.55, s / 7 * 1.2, s / 12 * 1.2, GxEPD_BLACK);
  fillEllipsis(x + s * 0.35, y + s * 0.55, s / 7, s / 12, GxEPD_WHITE);
  // 4
  fillEllipsis(x + s * 0.425, y + s * 0.425, s / 5 * 1.2, s / 8 * 1.2, GxEPD_BLACK);
  fillEllipsis(x + s * 0.425, y + s * 0.425, s / 5, s / 8, GxEPD_WHITE);
  // 5
  fillEllipsis(x + s * 0.5, y + s * 0.3, s / 4 * 1.2, s / 7 * 1.2, GxEPD_BLACK);
  fillEllipsis(x + s * 0.5, y + s * 0.3, s / 4, s / 7, GxEPD_WHITE);
}

//Takes x,y coordinates and radius r and phase. Phase denotes Moons current shape
void iconMoonPhase(uint16_t x, uint16_t y, uint16_t r, float phase) {
  display.fillCircle(x, y, r, GxEPD_WHITE);
  display.drawCircle(x, y, r, GxEPD_BLACK);
  if (phase == 0)
    display.fillCircle(x, y, r, GxEPD_BLACK);  //New Moon
  else if (phase > 0 && phase < 0.5) {
    for (int i = 0; i < r + 1; i++) {
      float cx = sqrt((r * r) - (i * i));
      float c2 = cx * 2 * (1 - (phase * 2));
      display.drawLine(x - cx, y + i, x - cx + c2, y + i, GxEPD_BLACK);
      display.drawLine(x - cx, y - i, x - cx + c2, y - i, GxEPD_BLACK);
    }
  } else if (phase == 0.5)
    ;  //display.fillCircle(x, y, r, GxEPD_RED);  //Full Moon
  else {
    display.fillCircle(x, y, r, GxEPD_BLACK);
    for (int i = 0; i < r + 1; i++) {
      float cx = sqrt((r * r) - (i * i));
      float c2 = cx * 2 * ((1 - phase) * 2);
      display.drawLine(x - cx, y + i, x - cx + c2, y + i, GxEPD_WHITE);
      display.drawLine(x - cx, y - i, x - cx + c2, y - i, GxEPD_WHITE);
    }
    display.drawCircle(x, y, r, GxEPD_BLACK);
  }

  // Add moon surface on top
  display.drawPixel(x - r + 22, y - r + 1, GxEPD_BLACK);
  display.drawPixel(x - r + 12, y - r + 3, GxEPD_BLACK);
  display.drawPixel(x - r + 24, y - r + 3, GxEPD_BLACK);
  display.drawPixel(x - r + 13, y - r + 4, GxEPD_BLACK);
  display.drawPixel(x - r + 15, y - r + 4, GxEPD_BLACK);
  display.drawPixel(x - r + 17, y - r + 4, GxEPD_BLACK);
  display.drawPixel(x - r + 24, y - r + 4, GxEPD_BLACK);
  display.drawPixel(x - r + 26, y - r + 4, GxEPD_BLACK);
  display.drawPixel(x - r + 10, y - r + 5, GxEPD_BLACK);
  display.drawPixel(x - r + 17, y - r + 5, GxEPD_BLACK);
  display.drawPixel(x - r + 26, y - r + 5, GxEPD_BLACK);
  display.drawPixel(x - r + 15, y - r + 6, GxEPD_BLACK);
  display.drawPixel(x - r + 17, y - r + 6, GxEPD_BLACK);
  display.drawPixel(x - r + 19, y - r + 6, GxEPD_BLACK);
  display.drawPixel(x - r + 7, y - r + 7, GxEPD_BLACK);
  display.drawPixel(x - r + 14, y - r + 7, GxEPD_BLACK);
  display.drawPixel(x - r + 16, y - r + 7, GxEPD_BLACK);
  display.drawPixel(x - r + 18, y - r + 7, GxEPD_BLACK);
  display.drawPixel(x - r + 19, y - r + 7, GxEPD_BLACK);
  display.drawPixel(x - r + 23, y - r + 7, GxEPD_BLACK);
  display.drawPixel(x - r + 25, y - r + 7, GxEPD_BLACK);
  display.drawPixel(x - r + 28, y - r + 7, GxEPD_BLACK);
  display.drawPixel(x - r + 30, y - r + 7, GxEPD_BLACK);
  display.drawPixel(x - r + 6, y - r + 8, GxEPD_BLACK);
  display.drawPixel(x - r + 9, y - r + 8, GxEPD_BLACK);
  display.drawPixel(x - r + 12, y - r + 8, GxEPD_BLACK);
  display.drawPixel(x - r + 15, y - r + 8, GxEPD_BLACK);
  display.drawPixel(x - r + 17, y - r + 8, GxEPD_BLACK);
  display.drawPixel(x - r + 19, y - r + 8, GxEPD_BLACK);
  display.drawPixel(x - r + 20, y - r + 8, GxEPD_BLACK);
  display.drawPixel(x - r + 21, y - r + 8, GxEPD_BLACK);
  display.drawPixel(x - r + 23, y - r + 8, GxEPD_BLACK);
  display.drawPixel(x - r + 26, y - r + 8, GxEPD_BLACK);
  display.drawPixel(x - r + 28, y - r + 8, GxEPD_BLACK);
  display.drawPixel(x - r + 29, y - r + 8, GxEPD_BLACK);
  display.drawPixel(x - r + 31, y - r + 8, GxEPD_BLACK);
  display.drawPixel(x - r + 8, y - r + 9, GxEPD_BLACK);
  display.drawPixel(x - r + 10, y - r + 9, GxEPD_BLACK);
  display.drawPixel(x - r + 14, y - r + 9, GxEPD_BLACK);
  display.drawPixel(x - r + 15, y - r + 9, GxEPD_BLACK);
  display.drawPixel(x - r + 17, y - r + 9, GxEPD_BLACK);
  display.drawPixel(x - r + 18, y - r + 9, GxEPD_BLACK);
  display.drawPixel(x - r + 20, y - r + 9, GxEPD_BLACK);
  display.drawPixel(x - r + 22, y - r + 9, GxEPD_BLACK);
  display.drawPixel(x - r + 23, y - r + 9, GxEPD_BLACK);
  display.drawPixel(x - r + 25, y - r + 9, GxEPD_BLACK);
  display.drawPixel(x - r + 26, y - r + 9, GxEPD_BLACK);
  display.drawPixel(x - r + 28, y - r + 9, GxEPD_BLACK);
  display.drawPixel(x - r + 29, y - r + 9, GxEPD_BLACK);
  display.drawPixel(x - r + 30, y - r + 9, GxEPD_BLACK);
  display.drawPixel(x - r + 6, y - r + 10, GxEPD_BLACK);
  display.drawPixel(x - r + 8, y - r + 10, GxEPD_BLACK);
  display.drawPixel(x - r + 11, y - r + 10, GxEPD_BLACK);
  display.drawPixel(x - r + 15, y - r + 10, GxEPD_BLACK);
  display.drawPixel(x - r + 16, y - r + 10, GxEPD_BLACK);
  display.drawPixel(x - r + 18, y - r + 10, GxEPD_BLACK);
  display.drawPixel(x - r + 20, y - r + 10, GxEPD_BLACK);
  display.drawPixel(x - r + 23, y - r + 10, GxEPD_BLACK);
  display.drawPixel(x - r + 24, y - r + 10, GxEPD_BLACK);
  display.drawPixel(x - r + 26, y - r + 10, GxEPD_BLACK);
  display.drawPixel(x - r + 29, y - r + 10, GxEPD_BLACK);
  display.drawPixel(x - r + 6, y - r + 11, GxEPD_BLACK);
  display.drawPixel(x - r + 7, y - r + 11, GxEPD_BLACK);
  display.drawPixel(x - r + 9, y - r + 11, GxEPD_BLACK);
  display.drawPixel(x - r + 10, y - r + 11, GxEPD_BLACK);
  display.drawPixel(x - r + 12, y - r + 11, GxEPD_BLACK);
  display.drawPixel(x - r + 16, y - r + 11, GxEPD_BLACK);
  display.drawPixel(x - r + 18, y - r + 11, GxEPD_BLACK);
  display.drawPixel(x - r + 21, y - r + 11, GxEPD_BLACK);
  display.drawPixel(x - r + 24, y - r + 11, GxEPD_BLACK);
  display.drawPixel(x - r + 28, y - r + 11, GxEPD_BLACK);
  display.drawPixel(x - r + 30, y - r + 11, GxEPD_BLACK);
  display.drawPixel(x - r + 31, y - r + 11, GxEPD_BLACK);
  display.drawPixel(x - r + 3, y - r + 12, GxEPD_BLACK);
  display.drawPixel(x - r + 5, y - r + 12, GxEPD_BLACK);
  display.drawPixel(x - r + 7, y - r + 12, GxEPD_BLACK);
  display.drawPixel(x - r + 9, y - r + 12, GxEPD_BLACK);
  display.drawPixel(x - r + 11, y - r + 12, GxEPD_BLACK);
  display.drawPixel(x - r + 16, y - r + 12, GxEPD_BLACK);
  display.drawPixel(x - r + 19, y - r + 12, GxEPD_BLACK);
  display.drawPixel(x - r + 21, y - r + 12, GxEPD_BLACK);
  display.drawPixel(x - r + 23, y - r + 12, GxEPD_BLACK);
  display.drawPixel(x - r + 24, y - r + 12, GxEPD_BLACK);
  display.drawPixel(x - r + 25, y - r + 12, GxEPD_BLACK);
  display.drawPixel(x - r + 32, y - r + 12, GxEPD_BLACK);
  display.drawPixel(x - r + 5, y - r + 13, GxEPD_BLACK);
  display.drawPixel(x - r + 7, y - r + 13, GxEPD_BLACK);
  display.drawPixel(x - r + 8, y - r + 13, GxEPD_BLACK);
  display.drawPixel(x - r + 10, y - r + 13, GxEPD_BLACK);
  display.drawPixel(x - r + 13, y - r + 13, GxEPD_BLACK);
  display.drawPixel(x - r + 17, y - r + 13, GxEPD_BLACK);
  display.drawPixel(x - r + 22, y - r + 13, GxEPD_BLACK);
  display.drawPixel(x - r + 1, y - r + 14, GxEPD_BLACK);
  display.drawPixel(x - r + 5, y - r + 14, GxEPD_BLACK);
  display.drawPixel(x - r + 6, y - r + 14, GxEPD_BLACK);
  display.drawPixel(x - r + 8, y - r + 14, GxEPD_BLACK);
  display.drawPixel(x - r + 10, y - r + 14, GxEPD_BLACK);
  display.drawPixel(x - r + 12, y - r + 14, GxEPD_BLACK);
  display.drawPixel(x - r + 14, y - r + 14, GxEPD_BLACK);
  display.drawPixel(x - r + 17, y - r + 14, GxEPD_BLACK);
  display.drawPixel(x - r + 18, y - r + 14, GxEPD_BLACK);
  display.drawPixel(x - r + 25, y - r + 14, GxEPD_BLACK);
  display.drawPixel(x - r + 28, y - r + 14, GxEPD_BLACK);
  display.drawPixel(x - r + 31, y - r + 14, GxEPD_BLACK);
  display.drawPixel(x - r + 1, y - r + 15, GxEPD_BLACK);
  display.drawPixel(x - r + 2, y - r + 15, GxEPD_BLACK);
  display.drawPixel(x - r + 5, y - r + 15, GxEPD_BLACK);
  display.drawPixel(x - r + 7, y - r + 15, GxEPD_BLACK);
  display.drawPixel(x - r + 9, y - r + 15, GxEPD_BLACK);
  display.drawPixel(x - r + 11, y - r + 15, GxEPD_BLACK);
  display.drawPixel(x - r + 13, y - r + 15, GxEPD_BLACK);
  display.drawPixel(x - r + 15, y - r + 15, GxEPD_BLACK);
  display.drawPixel(x - r + 29, y - r + 15, GxEPD_BLACK);
  display.drawPixel(x - r + 3, y - r + 16, GxEPD_BLACK);
  display.drawPixel(x - r + 5, y - r + 16, GxEPD_BLACK);
  display.drawPixel(x - r + 7, y - r + 16, GxEPD_BLACK);
  display.drawPixel(x - r + 8, y - r + 16, GxEPD_BLACK);
  display.drawPixel(x - r + 10, y - r + 16, GxEPD_BLACK);
  display.drawPixel(x - r + 14, y - r + 16, GxEPD_BLACK);
  display.drawPixel(x - r + 16, y - r + 16, GxEPD_BLACK);
  display.drawPixel(x - r + 1, y - r + 17, GxEPD_BLACK);
  display.drawPixel(x - r + 2, y - r + 17, GxEPD_BLACK);
  display.drawPixel(x - r + 4, y - r + 17, GxEPD_BLACK);
  display.drawPixel(x - r + 6, y - r + 17, GxEPD_BLACK);
  display.drawPixel(x - r + 8, y - r + 17, GxEPD_BLACK);
  display.drawPixel(x - r + 10, y - r + 17, GxEPD_BLACK);
  display.drawPixel(x - r + 12, y - r + 17, GxEPD_BLACK);
  display.drawPixel(x - r + 15, y - r + 17, GxEPD_BLACK);
  display.drawPixel(x - r + 18, y - r + 17, GxEPD_BLACK);
  display.drawPixel(x - r + 1, y - r + 18, GxEPD_BLACK);
  display.drawPixel(x - r + 3, y - r + 18, GxEPD_BLACK);
  display.drawPixel(x - r + 5, y - r + 18, GxEPD_BLACK);
  display.drawPixel(x - r + 7, y - r + 18, GxEPD_BLACK);
  display.drawPixel(x - r + 14, y - r + 18, GxEPD_BLACK);
  display.drawPixel(x - r + 16, y - r + 18, GxEPD_BLACK);
  display.drawPixel(x - r + 2, y - r + 19, GxEPD_BLACK);
  display.drawPixel(x - r + 6, y - r + 19, GxEPD_BLACK);
  display.drawPixel(x - r + 8, y - r + 19, GxEPD_BLACK);
  display.drawPixel(x - r + 10, y - r + 19, GxEPD_BLACK);
  display.drawPixel(x - r + 14, y - r + 19, GxEPD_BLACK);
  display.drawPixel(x - r + 16, y - r + 19, GxEPD_BLACK);
  display.drawPixel(x - r + 1, y - r + 20, GxEPD_BLACK);
  display.drawPixel(x - r + 2, y - r + 20, GxEPD_BLACK);
  display.drawPixel(x - r + 3, y - r + 20, GxEPD_BLACK);
  display.drawPixel(x - r + 5, y - r + 20, GxEPD_BLACK);
  display.drawPixel(x - r + 7, y - r + 20, GxEPD_BLACK);
  display.drawPixel(x - r + 13, y - r + 20, GxEPD_BLACK);
  display.drawPixel(x - r + 15, y - r + 20, GxEPD_BLACK);
  display.drawPixel(x - r + 3, y - r + 21, GxEPD_BLACK);
  display.drawPixel(x - r + 5, y - r + 21, GxEPD_BLACK);
  display.drawPixel(x - r + 7, y - r + 21, GxEPD_BLACK);
  display.drawPixel(x - r + 9, y - r + 21, GxEPD_BLACK);
  display.drawPixel(x - r + 10, y - r + 21, GxEPD_BLACK);
  display.drawPixel(x - r + 17, y - r + 21, GxEPD_BLACK);
  display.drawPixel(x - r + 2, y - r + 22, GxEPD_BLACK);
  display.drawPixel(x - r + 3, y - r + 22, GxEPD_BLACK);
  display.drawPixel(x - r + 5, y - r + 22, GxEPD_BLACK);
  display.drawPixel(x - r + 7, y - r + 22, GxEPD_BLACK);
  display.drawPixel(x - r + 11, y - r + 22, GxEPD_BLACK);
  display.drawPixel(x - r + 13, y - r + 22, GxEPD_BLACK);
  display.drawPixel(x - r + 15, y - r + 22, GxEPD_BLACK);
  display.drawPixel(x - r + 18, y - r + 22, GxEPD_BLACK);
  display.drawPixel(x - r + 20, y - r + 22, GxEPD_BLACK);
  display.drawPixel(x - r + 2, y - r + 23, GxEPD_BLACK);
  display.drawPixel(x - r + 4, y - r + 23, GxEPD_BLACK);
  display.drawPixel(x - r + 5, y - r + 23, GxEPD_BLACK);
  display.drawPixel(x - r + 6, y - r + 23, GxEPD_BLACK);
  display.drawPixel(x - r + 10, y - r + 23, GxEPD_BLACK);
  display.drawPixel(x - r + 13, y - r + 23, GxEPD_BLACK);
  display.drawPixel(x - r + 16, y - r + 23, GxEPD_BLACK);
  display.drawPixel(x - r + 19, y - r + 23, GxEPD_BLACK);
  display.drawPixel(x - r + 3, y - r + 24, GxEPD_BLACK);
  display.drawPixel(x - r + 4, y - r + 24, GxEPD_BLACK);
  display.drawPixel(x - r + 6, y - r + 24, GxEPD_BLACK);
  display.drawPixel(x - r + 7, y - r + 24, GxEPD_BLACK);
  display.drawPixel(x - r + 8, y - r + 24, GxEPD_BLACK);
  display.drawPixel(x - r + 10, y - r + 24, GxEPD_BLACK);
  display.drawPixel(x - r + 12, y - r + 24, GxEPD_BLACK);
  display.drawPixel(x - r + 14, y - r + 24, GxEPD_BLACK);
  display.drawPixel(x - r + 15, y - r + 24, GxEPD_BLACK);
  display.drawPixel(x - r + 17, y - r + 24, GxEPD_BLACK);
  display.drawPixel(x - r + 19, y - r + 24, GxEPD_BLACK);
  display.drawPixel(x - r + 3, y - r + 25, GxEPD_BLACK);
  display.drawPixel(x - r + 5, y - r + 25, GxEPD_BLACK);
  display.drawPixel(x - r + 7, y - r + 25, GxEPD_BLACK);
  display.drawPixel(x - r + 9, y - r + 25, GxEPD_BLACK);
  display.drawPixel(x - r + 11, y - r + 25, GxEPD_BLACK);
  display.drawPixel(x - r + 15, y - r + 25, GxEPD_BLACK);
  display.drawPixel(x - r + 18, y - r + 25, GxEPD_BLACK);
  display.drawPixel(x - r + 20, y - r + 25, GxEPD_BLACK);
  display.drawPixel(x - r + 21, y - r + 25, GxEPD_BLACK);
  display.drawPixel(x - r + 4, y - r + 26, GxEPD_BLACK);
  display.drawPixel(x - r + 7, y - r + 26, GxEPD_BLACK);
  display.drawPixel(x - r + 9, y - r + 26, GxEPD_BLACK);
  display.drawPixel(x - r + 10, y - r + 26, GxEPD_BLACK);
  display.drawPixel(x - r + 11, y - r + 26, GxEPD_BLACK);
  display.drawPixel(x - r + 12, y - r + 26, GxEPD_BLACK);
  display.drawPixel(x - r + 13, y - r + 26, GxEPD_BLACK);
  display.drawPixel(x - r + 14, y - r + 26, GxEPD_BLACK);
  display.drawPixel(x - r + 17, y - r + 26, GxEPD_BLACK);
  display.drawPixel(x - r + 19, y - r + 26, GxEPD_BLACK);
  display.drawPixel(x - r + 6, y - r + 27, GxEPD_BLACK);
  display.drawPixel(x - r + 7, y - r + 27, GxEPD_BLACK);
  display.drawPixel(x - r + 9, y - r + 27, GxEPD_BLACK);
  display.drawPixel(x - r + 11, y - r + 27, GxEPD_BLACK);
  display.drawPixel(x - r + 14, y - r + 27, GxEPD_BLACK);
  display.drawPixel(x - r + 16, y - r + 27, GxEPD_BLACK);
  display.drawPixel(x - r + 20, y - r + 27, GxEPD_BLACK);
  display.drawPixel(x - r + 7, y - r + 28, GxEPD_BLACK);
  display.drawPixel(x - r + 8, y - r + 28, GxEPD_BLACK);
  display.drawPixel(x - r + 10, y - r + 28, GxEPD_BLACK);
  display.drawPixel(x - r + 14, y - r + 28, GxEPD_BLACK);
  display.drawPixel(x - r + 18, y - r + 28, GxEPD_BLACK);
  display.drawPixel(x - r + 9, y - r + 29, GxEPD_BLACK);
  display.drawPixel(x - r + 11, y - r + 29, GxEPD_BLACK);
  display.drawPixel(x - r + 14, y - r + 29, GxEPD_BLACK);
  display.drawPixel(x - r + 15, y - r + 29, GxEPD_BLACK);
  display.drawPixel(x - r + 16, y - r + 29, GxEPD_BLACK);
  display.drawPixel(x - r + 15, y - r + 30, GxEPD_BLACK);
  display.drawPixel(x - r + 19, y - r + 30, GxEPD_BLACK);
  display.drawPixel(x - r + 17, y - r + 31, GxEPD_BLACK);
}

//direction=true (UP), direction=false (DOWN)
void iconSunRise(uint16_t x, uint16_t y, bool direction) {
  uint16_t r = 7;

  // Horizontal
  display.drawLine(x - r * 2 + 2, y, x + r * 2 - 2, y, GxEPD_BLACK);
  // Vertical
  display.drawLine(x, y - r * 2 + 2, x, y, GxEPD_BLACK);
  // Angle Top right
  display.drawLine(x - r * 2 + 5, y - r * 2 + 5, x, y, GxEPD_BLACK);
  // Angle Top left
  display.drawLine(x, y, x + r * 2 - 5, y - r * 2 + 5, GxEPD_BLACK);
  // Remove lines inside
  display.fillCircle(x, y, r + 1, GxEPD_WHITE);
  // Empty inside
  display.fillCircle(x, y, r - 1, GxEPD_RED);
  display.drawCircle(x, y, r - 1, GxEPD_BLACK);
  // Overwrite the bottom
  display.fillRect(x - r, y + 4, r * 2, r, GxEPD_WHITE);

  // Arrow up
  if (direction == true) {
    display.fillTriangle(x - r / 2 - 1, y + r - 2, x, y + r - 7, x + r / 2 + 1, y + r - 2, GxEPD_WHITE);
    display.drawLine(x - r / 2, y + r - 2, x, y + r - 6, GxEPD_BLACK);
    display.drawLine(x, y + r - 6, x + r / 2, y + r - 2, GxEPD_BLACK);
  }
  // Arrow DOWN
  if (direction == false) {
    display.drawLine(x - r / 2, y + r - 2, x, y + r + 2, GxEPD_BLACK);
    display.drawLine(x, y + r + 2, x + r / 2, y + r - 2, GxEPD_BLACK);
  }
  // Horizon line
  display.drawLine(x - r, y + r - 2, x - r / 2, y + r - 2, GxEPD_BLACK);
  display.drawLine(x + r / 2, y + r - 2, x + r, y + r - 2, GxEPD_BLACK);
}

//takes battery percent (integer) as input and prints battery icon
void iconBattery(byte percent) {
  display.drawRect(8, 4, 12, 7, GxEPD_BLACK);
  display.drawRect(6, 5, 2, 5, GxEPD_BLACK);

  if (percent >= 95)  //Full
    display.fillRect(9, 4, 10, 6, GxEPD_BLACK);
  else if (percent >= 85 && percent < 95)  //ful-Med
    display.fillRect(10, 4, 9, 6, GxEPD_BLACK);
  else if (percent > 65 && percent < 85)  //Med
    display.fillRect(11, 4, 9, 6, GxEPD_BLACK);
  else if (percent > 40 && percent <= 65)  //half
    display.fillRect(13, 4, 7, 6, GxEPD_BLACK);
  else if (percent > 20 && percent <= 40)  //low
    display.fillRect(15, 4, 5, 6, GxEPD_BLACK);
  else if (percent > 8 && percent <= 20)  //critical-low
    display.fillRect(16, 5, 3, 5, GxEPD_RED);
  else {  //near empty
    display.drawRect(8, 4, 12, 7, GxEPD_RED);
    display.drawRect(6, 5, 2, 5, GxEPD_RED);
  }
}

//In case of api failure, it displays network info for debugging
void networkInfo() {
  display.drawBitmap(270, 0, wifiError, 13, 13, GxEPD_BLACK);
  display.drawBitmap(100, 160, net, 29, 28, GxEPD_BLACK);
  u8g2Fonts.setFont(u8g2_font_logisoso20_tf);
  u8g2Fonts.setCursor(145, 184);  // start writing at this position
  u8g2Fonts.print("Network Debug");

  u8g2Fonts.setFont(u8g2_font_logisoso16_tf);
  u8g2Fonts.setCursor(5, 220);  // start writing at this position
  u8g2Fonts.print("Connected: ");
  if (WiFi.status() == WL_CONNECTED)
    u8g2Fonts.print("Yes (" + String(WiFi.SSID()) + ")");
  else
    u8g2Fonts.print("No");
  u8g2Fonts.setCursor(5, 245);  // start writing at this position
  u8g2Fonts.print("HTTP Code: " + String(httpResponseCode));
  u8g2Fonts.setCursor(5, 270);  // start writing at this position
  u8g2Fonts.print("WiFi RSSI: " + String(WiFi.RSSI()));

  if (WiFi.RSSI() > -50)
    u8g2Fonts.print(" Excellent");
  else if (WiFi.RSSI() > -60)
    u8g2Fonts.print(" Good");
  else if (WiFi.RSSI() > -70)
    u8g2Fonts.print(" Fair");
  else
    u8g2Fonts.print(" Poor");
}

//Checks for wifi signal level and prints one of the two icons accordingly. High and Avg.
void wifiStatus() {
  if (WiFi.RSSI() >= -60)
    display.drawBitmap(270, 0, wifiOn, 12, 12, GxEPD_BLACK);
  else
    display.drawBitmap(270, 0, wifiAvg, 12, 12, GxEPD_BLACK);
}

//Prints Alert icon and the passed message all over the screen. Implement a infinite while loop after calling this function
void errMsg(String msg) {
  display.setRotation(0);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_BLACK);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(msg, 0, 0, &tbx, &tby, &tbw, &tbh);
  // center the bounding box by transposition of the origin:
  uint16_t x = ((display.width() - tbw) / 2) - tbx;
  uint16_t y = ((display.height() - tbh) / 2) - tby;
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawInvertedBitmap(151, 40, alert, 100, 90, GxEPD_BLACK);
    display.setCursor(x, y);
    display.print(msg);
  } while (display.nextPage());
}

//prints debug related msgs
void debugPrinter(String msg) {
  display.setRotation(0);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(0, 20);
    display.print(msg);
  } while (display.nextPage());
}