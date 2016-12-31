#include <FS.h>                   //this needs to be first, or it all crashes and burns...

// Needed for parsing incoming strings
#include <stdio.h>

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>

// WifiManager Platformio lib number 1265
// https://github.com/tzapu/WiFiManager
#include <WiFiManager.h>

// ArduinoJson Platformio lib number 64
// https://github.com/bblanchon/ArduinoJson
#include <ArduinoJson.h>

// PubSubClient MQTT Platformio lib number 89
// https://github.com/knolleary/pubsubclient
#include <PubSubClient.h>

// Adafruit Neopixel Platformio lib number 28
// https://github.com/adafruit/Adafruit_NeoPixel
#include <Adafruit_NeoPixel.h>

/**************************************************************************
Global variables
**************************************************************************/

bool wifi_up = false;
bool mqtt_up = false;
uint32_t mqtt_rx = 0;   // Time last mqtt message received
int mqtt_status_period = 1000;
uint32_t mqtt_colour = 0;

/**************************************************************************
Neopixel setup
**************************************************************************/

// Define the parameters for the neopixels
#define NEO_PIN 13
#define NEO_NUMPIXELS 5

// Create the Neopixel object
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NEO_NUMPIXELS, NEO_PIN, NEO_GRB + NEO_KHZ800);

uint16_t STATUS_PIXEL = 0;
uint32 WIFI_STATUS = strip.Color(255,0,0);
uint32 MQTT_STATUS = strip.Color(0,0,255);
uint32 MQTT_RX = strip.Color(0,255,0);

uint32_t mixColors(uint32_t c1, uint32_t c2) {
    uint8_t
    r1 = (uint8_t)(c1 >> 16),
    g1 = (uint8_t)(c1 >>  8),
    b1 = (uint8_t)c1,
    r2 = (uint8_t)(c2 >> 16),
    g2 = (uint8_t)(c2 >>  8),
    b2 = (uint8_t)c2;

    uint16_t
    r = r1 + r2,
    g = g1 + g2,
    b = b1 + b2;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    /*
    uint8_t
    r = (uint8_t)((r1 + r2) >> 1),
    g = (uint8_t)((g1 + g2) >> 1),
    b = (uint8_t)((b1 + b2) >> 1);
    */

    return strip.Color((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

/**************************************************************************
Settings and callback for WifiManager
**************************************************************************/
//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40] = "geo-fun.org";
char mqtt_port[6] = "1883";
char mqtt_user[20];

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
    Serial.println("Should save config");
    shouldSaveConfig = true;
}

/**************************************************************************
Settings and callback for MQTT
**************************************************************************/
WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
char colourTopic[20];   // Defined in setup() as VBClk-XXXXXX/colour
char statusTopic[20];   // Defined in setup() as VBClk-XXXXXX/status
int value = 0;

void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    char strPayload[length +1];
    for (int i = 0; i < length; i++) {
        strPayload[i] = payload[i];
        Serial.print((char)payload[i]);
    }
    strPayload[length] = '\0'; // Null terminate
    Serial.println();
    mqtt_colour = parse_colour(strPayload);
    for (int i = 0; i < NEO_NUMPIXELS; i++) {
        strip.setPixelColor(i, mqtt_colour);
    }
}

uint32_t parse_colour(char* webcolour) {
    uint32_t colour = strip.Color(128,128,128);
    if (webcolour[0] == '#') {
        Serial.println("Converting the colour");
        Serial.println(webcolour);
        colour = hex2num(webcolour[1]) << 20 |
                 hex2num(webcolour[2]) << 16 |
                 hex2num(webcolour[3]) << 12 |
                 hex2num(webcolour[4]) << 8 |
                 hex2num(webcolour[5]) << 4 |
                 hex2num(webcolour[6]);
         //colour = (uint32_t)strtol((const char*)webcolour[1], NULL, 16);
        /*
        int r, g, b;
        sscanf((const char*)webcolour[1], "%02x%02x%02x", &r, &g, &b);
        colour = strip.Color(r,g,b);
        */
    }
    Serial.println(colour);
    return colour;
}

int hex2num(char c) {
    if (c >= 48 && c <= 57) {
        return (int)c - 48;
    } else if (c >= 65 && c <=70) {
        return (int)c - 55;
    } else if (c >= 97 && c <= 102) {
        return (int)c - 87;
    } else {
        return 0;
    }
}

void reconnect() {
    // Check the wifistatus first
    switch (WiFi.status()) {
        case WL_CONNECTED :
            wifi_up = true;
            mqtt_up = true;  // Will change this if we are not connected
            // Attempt reconnect (non blocking - doesn't guarantee reconnect success)
            if (!client.connected()) {
                Serial.print("Attempting MQTT connection...");
                // Attempt to connect
                if (client.connect(mqtt_user)) {
                    Serial.println("connected");
                    // Once connected, publish an announcement...
                    client.publish("mqtt-connect", mqtt_user);
                    // ... and resubscribe
                    client.subscribe(colourTopic);
                } else {
                    Serial.print("failed, rc=");
                    Serial.print(client.state());
                    mqtt_up = false;
                }
            }
            break;
        default :
        wifi_up = false;
    }
}

/**************************************************************************
Working functions
**************************************************************************/

void update_status() {
    uint32_t status = 0;
    if (wifi_up) {
        status |= WIFI_STATUS;
    }
    if (mqtt_up) {
        status |= MQTT_STATUS;
    }
    if (millis() - mqtt_rx < mqtt_status_period) {
        status |= MQTT_RX;
    }
    strip.setPixelColor(STATUS_PIXEL, status);
}

/**************************************************************************
Setup function
**************************************************************************/

void setup() {
    // put your setup code here, to run once:

    //////////////////////////////////////////////////////////////////////
    // Serial setup
    //////////////////////////////////////////////////////////////////////

    Serial.begin(115200);
    Serial.println();

    //////////////////////////////////////////////////////////////////////
    // Neopixel setup
    //////////////////////////////////////////////////////////////////////

    strip.begin();
    update_status();
    strip.show();

    //////////////////////////////////////////////////////////////////////
    // WifiManager Setup code
    //////////////////////////////////////////////////////////////////////

    // Get a unique ID for this IOT Device
    String id = String(ESP.getChipId(), HEX);
    char pwd[7];
    id.toCharArray(pwd, 7);
    const char* passwd = pwd;
    String name = "VBClk-" + id;
    name.toCharArray(mqtt_user, 20);
    Serial.println(mqtt_user);

    //clean FS, for testing
    //SPIFFS.format();

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

                    strcpy(mqtt_server, json["mqtt_server"]);
                    strcpy(mqtt_port, json["mqtt_port"]);
                    strcpy(mqtt_user, json["mqtt_user"]);

                } else {
                    Serial.println("failed to load json config");
                }
            }
        }
    } else {
        Serial.println("failed to mount FS");
    }
    //end read

    // The extra parameters to be configured (can be either global or just in the setup)
    // After connecting, parameter.getValue() will get you the configured value
    // id/name placeholder/prompt default length
    WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
    WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
    WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 20);

    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    //set config save notify callback
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    //set static ip
    //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

    //add all your parameters here
    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.addParameter(&custom_mqtt_user);

    //reset settings - for testing
    //wifiManager.resetSettings();

    //set minimu quality of signal so it ignores AP's under that quality
    //defaults to 8%
    //wifiManager.setMinimumSignalQuality();

    //sets timeout until configuration portal gets turned off
    //useful to make it all retry or go to sleep
    //in seconds
    //wifiManager.setTimeout(120);

    //fetches ssid and pass and tries to connect
    //if it does not connect it starts an access point with the specified name
    //here  "AutoConnectAP"
    //and goes into a blocking loop awaiting configuration
    if (!wifiManager.autoConnect(mqtt_user, passwd)) {
        Serial.println("failed to connect and hit timeout");
        delay(3000);
        //reset and try again, or maybe put it to deep sleep
        ESP.reset();
        delay(5000);
    }

    //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");

    //read updated parameters
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    strcpy(mqtt_user, custom_mqtt_user.getValue());

    //save the custom parameters to FS
    if (shouldSaveConfig) {
        Serial.println("saving config");
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.createObject();
        json["mqtt_server"] = mqtt_server;
        json["mqtt_port"] = mqtt_port;
        json["mqtt_user"] = mqtt_user;

        File configFile = SPIFFS.open("/config.json", "w");
        if (!configFile) {
            Serial.println("failed to open config file for writing");
        }

        json.printTo(Serial);
        json.printTo(configFile);
        configFile.close();
        //end save
    }

    Serial.println("local ip");
    Serial.println(WiFi.localIP());

    //////////////////////////////////////////////////////////////////////
    // MQTT Setup
    //////////////////////////////////////////////////////////////////////
    client.setServer(mqtt_server, atoi(mqtt_port));
    client.setCallback(callback);
    // Generate the MQTT Topics
    strcpy(colourTopic, mqtt_user);
    strcat(colourTopic, "/colour");
    strcpy(statusTopic, mqtt_user);
    strcat(statusTopic, "/status");
    reconnect(); // Connect to the mqtt setServer
    if (client.connected()) {
        client.publish(statusTopic, "Setup complete");
    }
}

/**************************************************************************
Loop function
**************************************************************************/

void loop() {
    // put your main code here, to run repeatedly:
    if (!client.connected()) {
        reconnect();
    }
    client.loop();

    // Update the neopixel display
    update_status();
    strip.show();

    long now = millis();
    if (now - lastMsg > 60000) {
        lastMsg = now;
        value++;
        snprintf (msg, 75, "hello world #%ld", value);
        Serial.print("Publish message: ");
        Serial.println(msg);
        client.publish("outTopic", msg);
    }

}
