
/***************************************************
MQTT Garage Door Opener

By Jordan Carlson
 ****************************************************/

#include <FS.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <PubSubClient.h>
#include <ArduinoJson.h>


/**************************** SENSOR DEFINITIONS *******************************************/

char mqttServer[40];
char mqttPortString[6];
int mqttPort;
char mqttUser[30];
char mqttPassword[30];

char stateTopic[6] = "state";
char commandTopic[8] = "command";

char* sensorName = "garageDoor";

//flag for saving data
bool shouldSaveConfig = false;

/**************************** SENSOR DEFINITIONS *******************************************/

int RELAY1 = D10;

int REED = D7;
int ReedState = 0;

WiFiClient espClient;
PubSubClient client(espClient);

/********************************** START SETUP *****************************************/
void setup() {
  Serial.begin(115200);
  delay(10);
  pinMode(RELAY1, OUTPUT);
  delay(10);
  pinMode(REED, INPUT);
  delay(10);

  readConfig();

  setupWifi();

  setConfigValues();

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    saveConfig()
  }

  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);

  mqttConnect();
}

/********************************** START LOOP *****************************************/
void loop() {
  if (!client.connected()) {
    mqttConnect();
  }
  client.loop();


  // Get the current state of the Reed switch (HIGH = open)
  ReedState = digitalRead(REED);

  if (ReedState == HIGH) {
    client.publish(stateTopic, "STATE_OPEN", true);
  } else {
    client.publish(stateTopic, "STATE_CLOSED", true);
  }

  // The state will be published every loop, use this delay to publish less frequently
  delay(1000);
}



void saveConfig() {
  Serial.println("saving config");
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["mqtt_server"] = mqttServer;
  json["mqtt_port"] = mqttPortString;
  json["mqtt_user"] = mqttUser;
  json["mqtt_password"] = mqttPassword;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("failed to open config file for writing");
  }

  json.printTo(Serial);
  json.printTo(configFile);
  configFile.close();
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

void setConfigValues() {
      //read updated parameters
  strcpy(mqttServer, custom_mqtt_server.getValue());
  strcpy(mqttPortString, custom_mqtt_port.getValue());
  strcpy(mqttUser, custom_mqtt_user.getValue());
  strcpy(mqttPassword, custom_mqtt_password.getValue());

    // Converts the char* to int
  sscanf(mqttPortString, "%d", &mqttPort);
}

/********************************** WIFI MANAGER SAVE CALLBACK *****************************************/
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

/********************************** START SETUP WIFI*****************************************/
void setupWifi() {

  Serial.println(); Serial.println();
  Serial.print("Connecting to ");

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
  
  if (!wifiManager.autoConnect("SmartGarageAP", "smartgarageap")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}


/********************************** MQTT CALLBACK *****************************************/
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

  if (strcmp(topic, commandTopic) == 0) {
    if (strcmp(message, "SERVICE_OPEN") == 0 ||strcmp(message, "SERVICE_CLOSE") == 0) {
      // Send signal to the garage door
      Relay_activate();
    } else {
      Serial.println("Invalid Command");
    }
  }
 
}

void mqttConnect() {
  int retryCount = 0;
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(sensorName, mqttUser, mqttPassword)) {
      Serial.println("connected");
      client.subscribe(commandTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");

      if (retryCount >= 3) {
        break;
      }

      retryCount = retryCount + 1;

      // Wait 5 seconds before retrying
      delay(5000);
    }
  }

  if (retryCount >= 3) {
    WiFi.disconnect();
    SPIFFS.format();
    softwareReset();
  }

}

void Relay_activate() {
  // Turn the pin "on"
  digitalWrite(RELAY1, 1);
  Serial.println("Relay on");
  // Leave it on for 2 seconds
  delay(2000);
  // Turn the pin "off"
  digitalWrite(RELAY1, 0);
  Serial.println("Relay off");
}

void softwareReset() {
  Serial.print("resetting");
  ESP.reset(); 
}
