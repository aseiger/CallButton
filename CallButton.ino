#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <EEPROM.h>

#define TRIGGER_PIN 5
#define CALLBUTTON_PIN 4
#define CONFIG_LED 16
#define CALL_LED 0

WiFiManager wifiManager;

WiFiClient espClient;
PubSubClient client(espClient);

const char* callButtonTopic = "callbuttons/all";
long lastReconnectAttempt = 0;

bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

/********************** Begin EEPROM Section *****************/
#define EEPROM_SALT 12664
typedef struct
{
  int   salt = EEPROM_SALT;
  char mqtt_server[256]  = "";
  char mqtt_port[6] = "";
} MQTTSettings;
MQTTSettings mqtt_settings;

WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_settings.mqtt_server, 256);
WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_settings.mqtt_port, 6);

void eeprom_read()
{
  EEPROM.begin(512);
  EEPROM.get(0, mqtt_settings);
  EEPROM.end();
}


void eeprom_saveconfig()
{
  EEPROM.begin(512);
  EEPROM.put(0, mqtt_settings);
  EEPROM.commit();
  EEPROM.end();
}

/*********************************************************************************/


bool doInitialSetup = false;

bool isCalled = false;

bool ignoreCallButtonPress = false;

void setup() {
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP 
  Serial.begin(115200);
  Serial.println("\n Starting");
  //pin setups
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(CALLBUTTON_PIN, INPUT_PULLUP);
  pinMode(CONFIG_LED, OUTPUT);
  pinMode(CALL_LED, OUTPUT);
  digitalWrite(CALL_LED, HIGH);

  eeprom_read();
  if (mqtt_settings.salt != EEPROM_SALT)
  {
    Serial.println("Invalid settings in EEPROM, trying with defaults");
    MQTTSettings defaults;
    mqtt_settings = defaults;
  }

  //set up WiFi
  char setup_ssid[32];
  sprintf(setup_ssid, "CALLBUTTON-%d", ESP.getChipId());
  Serial.println(setup_ssid);
    
  WiFi.hostname(setup_ssid);
  digitalWrite(CONFIG_LED, LOW);
  int retryCount = 0;

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);

  client.setCallback(mqtt_callback);

  //reset settings - for testing
  //wifiManager.resetSettings();
  
  while(WiFi.status() != WL_CONNECTED)
  {
    delay(250); 
    retryCount++;

    if(retryCount > 120) //30 seconds
    {
      doInitialSetup = true;
      break;
    }
  }

  //if setup is OK, initialize the MQTT stuff
  if(doInitialSetup == false)
  {
    digitalWrite(CONFIG_LED, HIGH);
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  if( (digitalRead(TRIGGER_PIN) == LOW ) || (doInitialSetup == true))
  {
    digitalWrite(CONFIG_LED, LOW);

    //reset settings - for testing
    //wifiManager.resetSettings();

    wifiManager.setTimeout(120);
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    //set the SSID name to something noteworthy
    char setup_ssid[32];
    sprintf(setup_ssid, "CALLBUTTON-%d", ESP.getChipId());

    if (!wifiManager.startConfigPortal(setup_ssid, "callbutton")) 
    {
      Serial.println("failed to connect and hit timeout");
    }
    else    
    {
      Serial.println("Connected!!!");
      doInitialSetup = false;

      strcpy(mqtt_settings.mqtt_server, custom_mqtt_server.getValue());
      strcpy(mqtt_settings.mqtt_port, custom_mqtt_port.getValue());

      eeprom_saveconfig();
      shouldSaveConfig = false;
      mqtt_reconnect();
    }
    digitalWrite(CONFIG_LED, HIGH);
  }

  if (client.loop()) 
  {
    
  }
  else
  {
    if (millis() >= lastReconnectAttempt) 
    {
      lastReconnectAttempt = millis() + 5000;
      //Serial.println("Disconnected!");
      // Attempt to reconnect
      if (mqtt_reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  }
  //check the button states
  if((digitalRead(CALLBUTTON_PIN) == LOW ) && (ignoreCallButtonPress == false))
  {
    if(isCalled)
    {
      acknowledgeCall();
    }
    else
    {
      transmitCall();
    }
    ignoreCallButtonPress = true;
  }
  else if(digitalRead(CALLBUTTON_PIN) == HIGH )
  {
    ignoreCallButtonPress = false;
  }

  digitalWrite(CALL_LED, !isCalled);
  
  //end of main loop
  delay(10);
}

void transmitCall(void)
{
  client.publish(callButtonTopic, "CALL");
}

void acknowledgeCall(void)
{
  client.publish(callButtonTopic, "ACKNOWLEDGE_CALL");
}

void requestActiveCalls(void)
{
  client.publish(callButtonTopic, "GET_NEW_CALLS");
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  payload[length] = '\0';

  if(strcmp(topic, callButtonTopic) == 0)
  {
    if(strcmp((char *)payload, "CALL") == 0)
    {
      isCalled = true;
    }
    else if(strcmp((char *)payload, "ACKNOWLEDGE_CALL") == 0)
    {
      isCalled = false;
    }
    else if(strcmp((char *)payload, "GET_ACTIVE_CALLS") == 0)
    {
      if(isCalled)
      {
        transmitCall();
      }
    }
  }
}

bool mqtt_reconnect() {
  // Attempt to connect
  client.disconnect();
  
  unsigned int portNum = atoi(mqtt_settings.mqtt_port);
  client.setServer(mqtt_settings.mqtt_server, portNum);
  char setup_name[32];
  sprintf(setup_name, "CALLBUTTON-%d", ESP.getChipId());
  if (client.connect(setup_name)) {
    client.subscribe(callButtonTopic);
    requestActiveCalls();
  }
  return client.connected();
}
