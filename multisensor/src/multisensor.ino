/*
  To use this code you will need the following dependancies: 
  
  - Support for the ESP8266 boards. 
        - You can add it to the board manager by going to File -> Preference and pasting http://arduino.esp8266.com/stable/package_esp8266com_index.json into the Additional Board Managers URL field.
        - Next, download the ESP8266 dependancies by going to Tools -> Board -> Board Manager and searching for ESP8266 and installing it.
  
  - You will also need to download the follow libraries by going to Sketch -> Include Libraries -> Manage Libraries
      - DHT sensor library 
      - Adafruit unified sensor
      - PubSubClient
      - ArduinoJSON
*/

#include <FS.h>
#include <ESP8266WiFi.h>
#include <DHT.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <string.h>


// Access point will be named "MultiSensorXXXXXX"
// Password is "multisensor"


char mqttServer[40];
char mqttPortString[6];
int mqttPort;
char mqttUser[30];
char mqttPassword[30];
char tempOffsetString[10] = "0";

char sensorName[20] = "sensor1";

// MQTT Topic
char topicStateFull[40];
char topicSetFull[40];
char topicOffsetFull[40];

//flag for saving data
bool shouldSaveConfig = false;

const char* on_cmd = "ON";
const char* off_cmd = "OFF";


/**************************** FOR OTA **************************************************/
// #define sensorName "node1"
#define OTApassword "63sB2O8DKJmLWNRBqmQX" // change this to whatever password you want to use when you upload OTA
int OTAport = 8266;



/**************************** PIN DEFINITIONS ********************************************/
const int redPin = D1;
const int greenPin = D2;
const int bluePin = D3;
#define PIRPIN    D5
#define DHTPIN    D7
#define DHTTYPE   DHT22
#define LDRPIN    A0



/**************************** SENSOR DEFINITIONS *******************************************/
float ldrValue;
int LDR;
float calcLDR;
float diffLDR = 25;

float diffTEMP = 0.2;
float tempValue;
float tempOffset = 0;

float diffHUM = 1;
float humValue;

int pirValue;
int pirStatus;
String motionStatus;

char message_buff[100];

int calibrationTime = 0;

const int BUFFER_SIZE = 300;

#define MQTT_MAX_PACKET_SIZE 512


/******************************** GLOBALS for fade/flash *******************************/
byte red = 255;
byte green = 255;
byte blue = 255;
byte brightness = 255;

byte realRed = 0;
byte realGreen = 0;
byte realBlue = 0;

bool stateOn = false;

bool startFade = false;
unsigned long lastLoop = 0;
int transitionTime = 0;
bool inFade = false;
int loopCount = 0;
int stepR, stepG, stepB;
int redVal, grnVal, bluVal;

bool flash = false;
bool startFlash = false;
int flashLength = 0;
unsigned long flashStartTime = 0;
byte flashRed = red;
byte flashGreen = green;
byte flashBlue = blue;
byte flashBrightness = brightness;



WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE);



/********************************** START SETUP*****************************************/
void setup() {

  Serial.begin(115200);

  pinMode(PIRPIN, INPUT);
  pinMode(DHTPIN, INPUT);
  pinMode(LDRPIN, INPUT);

  Serial.begin(115200);
  delay(10);

  //  Flash Chip ID: 1458415
  //  Chip ID: 6605548
  //  Mac Address: 60:01:94:64:CA:EC
  // sensorName = "MultiSensor64CAEC"

  sprintf(sensorName, "MultiSensor%X", ESP.getChipId());
  Serial.println(sensorName);

  sprintf(topicStateFull, "multi/%X/state", ESP.getChipId());
  sprintf(topicSetFull, "multi/%X/set", ESP.getChipId());
  sprintf(topicOffsetFull, "multi/%X/offset", ESP.getChipId());

  Serial.println(topicStateFull);
  Serial.println(topicSetFull);
  Serial.println(topicOffsetFull);

  readConfig();

  // setupWifi();
  // setConfigValues();
  // saveConfig(shouldSaveConfig)

  // force disconnect for testing
  // WiFi.disconnect();

  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqttServer, 40);
  wifiManager.addParameter(&custom_mqtt_server);

  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqttPortString, 6);
  wifiManager.addParameter(&custom_mqtt_port);

  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqttUser, 20);
  wifiManager.addParameter(&custom_mqtt_user);

  WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqttPassword, 30);
  wifiManager.addParameter(&custom_mqtt_password);

  // Orange for AP mode
  setColor(255,165,0);

  if (!wifiManager.autoConnect(sensorName, "multisensor")) {
    Serial.println("failed to connect and hit timeout");
    flashLED(255, 0, 0, 750, 3);
    //reset and try again, or maybe put it to deep sleep
    softwareReset();
    delay(5000);
  }

    //read updated parameters
  strcpy(mqttServer, custom_mqtt_server.getValue());
  strcpy(mqttPortString, custom_mqtt_port.getValue());
  strcpy(mqttUser, custom_mqtt_user.getValue());
  strcpy(mqttPassword, custom_mqtt_password.getValue());

    // Converts the char* to int
  sscanf(mqttPortString, "%d", &mqttPort);
  sscanf(tempOffsetString, "%f", &tempOffset);


  saveConfig(true);


  setupOTA();
  setupMQTT();

  reconnect();
}

void saveConfig(bool shouldSave) {
  //save the custom parameters to FS
  if (shouldSave) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqttServer;
    json["mqtt_port"] = mqttPortString;
    json["mqtt_user"] = mqttUser;
    json["mqtt_password"] = mqttPassword;
    json["temp_offset"] = tempOffsetString;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }
}

void setupMQTT() {
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
}

void setupOTA() {
  ArduinoOTA.setPort(OTAport);
  ArduinoOTA.setHostname(sensorName);
  ArduinoOTA.setPassword((const char *)OTApassword);
  ArduinoOTA.onStart([]() {
    Serial.println("Starting");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

/********************************** WIFI MANAGER SAVE CALLBACK *****************************************/
void saveConfigCallback () {
  flashLED(0, 255, 0, 375, 3);
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


void readConfig () {
  //clean FS, for testing
  // SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqttServer, json["mqtt_server"]);
          strcpy(mqttPortString, json["mqtt_port"]);
          strcpy(mqttUser, json["mqtt_user"]);
          strcpy(mqttPassword, json["mqtt_password"]);
          strcpy(tempOffsetString, json["temp_offset"]);

        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read
}


/********************************** MQTT CALLBACK*****************************************/
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  char message[length + 1];
  for (int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  Serial.println(message);

  Serial.println("incoming topic: ");
  Serial.print(topic);

  if (strcmp(topic, topicSetFull) == 0) {
    if (!processJson(message)) {
      return;
    }

    if (stateOn) {
      // Update lights
      realRed = map(red, 0, 255, 0, brightness);
      realGreen = map(green, 0, 255, 0, brightness);
      realBlue = map(blue, 0, 255, 0, brightness);
    }
    else {
      realRed = 0;
      realGreen = 0;
      realBlue = 0;
    }

    startFade = true;
    inFade = false; // Kill the current fade

    sendState();
  } else if (strcmp(topic, topicOffsetFull) == 0) {
    tempOffset = atof(message);

    Serial.println(tempOffset);

    sprintf(tempOffsetString, "%f", tempOffset);

    Serial.println("pre Save Config");
    saveConfig(true);

    Serial.print("Temperature offset to: ");
    Serial.println(tempOffset);
    flashLED(255, 255, 255, 200, 4);

  }
}



/********************************** START PROCESS JSON*****************************************/
bool processJson(char* message) {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.parseObject(message);

  if (!root.success()) {
    Serial.println("parseObject() failed");
    return false;
  }

  if (root.containsKey("state")) {
    if (strcmp(root["state"], on_cmd) == 0) {
      stateOn = true;
    }
    else if (strcmp(root["state"], off_cmd) == 0) {
      stateOn = false;
    }
  }

  // If "flash" is included, treat RGB and brightness differently
  if (root.containsKey("flash")) {
    flashLength = (int)root["flash"] * 1000;

    if (root.containsKey("brightness")) {
      flashBrightness = root["brightness"];
    }
    else {
      flashBrightness = brightness;
    }

    if (root.containsKey("color")) {
      flashRed = root["color"]["r"];
      flashGreen = root["color"]["g"];
      flashBlue = root["color"]["b"];
    }
    else {
      flashRed = red;
      flashGreen = green;
      flashBlue = blue;
    }

    flashRed = map(flashRed, 0, 255, 0, flashBrightness);
    flashGreen = map(flashGreen, 0, 255, 0, flashBrightness);
    flashBlue = map(flashBlue, 0, 255, 0, flashBrightness);

    flash = true;
    startFlash = true;
  }
  else { // Not flashing
    flash = false;

    if (root.containsKey("color")) {
      red = root["color"]["r"];
      green = root["color"]["g"];
      blue = root["color"]["b"];
    }

    if (root.containsKey("brightness")) {
      brightness = root["brightness"];
    }

    if (root.containsKey("transition")) {
      transitionTime = root["transition"];
    }
    else {
      transitionTime = 0;
    }
  }

  return true;
}



/********************************** START SEND STATE*****************************************/
void sendState() {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();

  root["state"] = (stateOn) ? on_cmd : off_cmd;
  JsonObject& color = root.createNestedObject("color");
  color["r"] = red;
  color["g"] = green;
  color["b"] = blue;


  root["brightness"] = brightness;
  root["humidity"] = (String)humValue;
  root["motion"] = (String)motionStatus;
  root["ldr"] = (String)LDR;

  // Apply temperture calibration offset
  float offsetTemp = tempValue + tempOffset;
  
  root["temperature"] = (String)offsetTemp;
  root["heatIndex"] = (String)calculateHeatIndex(humValue, offsetTemp);


  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));

  Serial.println(buffer);
  client.publish(topicStateFull, buffer, true);
}


/*
 * Calculate Heat Index value AKA "Real Feel"
 * NOAA heat index calculations taken from
 * http://www.wpc.ncep.noaa.gov/html/heatindex_equation.shtml
 */
float calculateHeatIndex(float humidity, float temp) {
  float heatIndex= 0;
  if (temp >= 80) {
    heatIndex = -42.379 + 2.04901523*temp + 10.14333127*humidity;
    heatIndex = heatIndex - .22475541*temp*humidity - .00683783*temp*temp;
    heatIndex = heatIndex - .05481717*humidity*humidity + .00122874*temp*temp*humidity;
    heatIndex = heatIndex + .00085282*temp*humidity*humidity - .00000199*temp*temp*humidity*humidity;
  } else {
     heatIndex = 0.5 * (temp + 61.0 + ((temp - 68.0)*1.2) + (humidity * 0.094));
  }

  if (humidity < 13 && 80 <= temp <= 112) {
     float adjustment = ((13-humidity)/4) * sqrt((17-abs(temp-95.))/17);
     heatIndex = heatIndex - adjustment;
  }

  return heatIndex;
}


/********************************** START SET COLOR *****************************************/
void setColor(int inR, int inG, int inB) {
  analogWrite(redPin, inR);
  analogWrite(greenPin, inG);
  analogWrite(bluePin, inB);

  Serial.println("Setting LEDs:");
  Serial.print("r: ");
  Serial.print(inR);
  Serial.print(", g: ");
  Serial.print(inG);
  Serial.print(", b: ");
  Serial.println(inB);
}

void flashLED(int inR, int inG, int inB, int speed, int count) {
  for(size_t i = 0; i < count+1; i++) {
    setColor(inR, inG, inB);
    delay(speed);
    setColor(0, 0, 0);
    delay(speed);
  }
}

/********************************** START RECONNECT*****************************************/
void reconnect() {
  // Loop until we're reconnected or too many retries
  
  int retryCount = 0;
  int retryMax = 5;

  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    flashLED(0, 0, 255, 375, 3);
    // Attempt to connect
    if (client.connect(sensorName, mqttUser, mqttPassword)) {
      Serial.println("connected");
      client.subscribe(topicSetFull);
      client.subscribe(topicOffsetFull);              
      setColor(0, 0, 0);
      sendState();
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");

      // this will take 2400 millis
      flashLED(255, 0, 0, 375, 3);
      // Wait 3 more seconds before retrying
      delay(3000);

      if (retryCount >= retryMax) {
        break;
      }
      retryCount = retryCount + 1;
    }
  }
  if (retryCount >= retryMax) {
    flashLED(255, 0, 0, 100, 10);
    // this will activate AP setup mode again.
    WiFi.disconnect();
    softwareReset();
  }
}



/********************************** START CHECK SENSOR **********************************/
bool checkBoundSensor(float newValue, float prevValue, float maxDiff) {
  return newValue < prevValue - maxDiff || newValue > prevValue + maxDiff;
}


/********************************** START MAIN LOOP***************************************/
void loop() {

  ArduinoOTA.handle();
  
  if (!client.connected()) {
    // reconnect();
    softwareReset();
  }
  client.loop();

  if (!inFade) {

    float newTempValue = dht.readTemperature(true); //to use celsius remove the true text inside the parentheses  
    float newHumValue = dht.readHumidity();

    //PIR CODE
    pirValue = digitalRead(PIRPIN); //read state of the

    if (pirValue == LOW && pirStatus != 1) {
      motionStatus = "standby";
      sendState();
      pirStatus = 1;
    }

    else if (pirValue == HIGH && pirStatus != 2) {
      motionStatus = "motion detected";
      sendState();
      pirStatus = 2;
    }

    delay(100);

    if (checkBoundSensor(newTempValue, tempValue, diffTEMP)) {
      tempValue = newTempValue;
      sendState();
    }

    if (checkBoundSensor(newHumValue, humValue, diffHUM)) {
      humValue = newHumValue;
      sendState();
    }


    int newLDR = analogRead(LDRPIN);

    if (checkBoundSensor(newLDR, LDR, diffLDR)) {
      LDR = newLDR;
      sendState();
    }

  }

  if (flash) {
    if (startFlash) {
      startFlash = false;
      flashStartTime = millis();
    }

    if ((millis() - flashStartTime) <= flashLength) {
      if ((millis() - flashStartTime) % 1000 <= 500) {
        setColor(flashRed, flashGreen, flashBlue);
      }
      else {
        setColor(0, 0, 0);
        // If you'd prefer the flashing to happen "on top of"
        // the current color, uncomment the next line.
        // setColor(realRed, realGreen, realBlue);
      }
    }
    else {
      flash = false;
      setColor(realRed, realGreen, realBlue);
    }
  }

  if (startFade) {
    // If we don't want to fade, skip it.
    if (transitionTime == 0) {
      setColor(realRed, realGreen, realBlue);

      redVal = realRed;
      grnVal = realGreen;
      bluVal = realBlue;

      startFade = false;
    }
    else {
      loopCount = 0;
      stepR = calculateStep(redVal, realRed);
      stepG = calculateStep(grnVal, realGreen);
      stepB = calculateStep(bluVal, realBlue);

      inFade = true;
    }
  }

  if (inFade) {
    startFade = false;
    unsigned long now = millis();
    if (now - lastLoop > transitionTime) {
      if (loopCount <= 1020) {
        lastLoop = now;

        redVal = calculateVal(stepR, redVal, loopCount);
        grnVal = calculateVal(stepG, grnVal, loopCount);
        bluVal = calculateVal(stepB, bluVal, loopCount);

        setColor(redVal, grnVal, bluVal); // Write current values to LED pins

        Serial.print("Loop count: ");
        Serial.println(loopCount);
        loopCount++;
      }
      else {
        inFade = false;
      }
    }
  }
}




/**************************** START TRANSITION FADER *****************************************/
// From https://www.arduino.cc/en/Tutorial/ColorCrossfader
/* BELOW THIS LINE IS THE MATH -- YOU SHOULDN'T NEED TO CHANGE THIS FOR THE BASICS

  The program works like this:
  Imagine a crossfade that moves the red LED from 0-10,
    the green from 0-5, and the blue from 10 to 7, in
    ten steps.
    We'd want to count the 10 steps and increase or
    decrease color values in evenly stepped increments.
    Imagine a + indicates raising a value by 1, and a -
    equals lowering it. Our 10 step fade would look like:

    1 2 3 4 5 6 7 8 9 10
  R + + + + + + + + + +
  G   +   +   +   +   +
  B     -     -     -

  The red rises from 0 to 10 in ten steps, the green from
  0-5 in 5 steps, and the blue falls from 10 to 7 in three steps.

  In the real program, the color percentages are converted to
  0-255 values, and there are 1020 steps (255*4).

  To figure out how big a step there should be between one up- or
  down-tick of one of the LED values, we call calculateStep(),
  which calculates the absolute gap between the start and end values,
  and then divides that gap by 1020 to determine the size of the step
  between adjustments in the value.
*/
int calculateStep(int prevValue, int endValue) {
  int step = endValue - prevValue; // What's the overall gap?
  if (step) {                      // If its non-zero,
    step = 1020 / step;          //   divide by 1020
  }

  return step;
}

/* The next function is calculateVal. When the loop value, i,
   reaches the step size appropriate for one of the
   colors, it increases or decreases the value of that color by 1.
   (R, G, and B are each calculated separately.)
*/
int calculateVal(int step, int val, int i) {
  if ((step) && i % step == 0) { // If step is non-zero and its time to change a value,
    if (step > 0) {              //   increment the value if step is positive...
      val += 1;
    }
    else if (step < 0) {         //   ...or decrement it if step is negative
      val -= 1;
    }
  }

  // Defensive driving: make sure val stays in the range 0-255
  if (val > 255) {
    val = 255;
  }
  else if (val < 0) {
    val = 0;
  }

  return val;
}

/****reset***/
// Restarts program from beginning but does not reset the peripherals and registers
void softwareReset() {
  Serial.print("resetting");
  ESP.reset(); 
}