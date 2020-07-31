#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>

/*  You need to go into this file and change this line from:
      #define MQTT_MAX_PACKET_SIZE 128
    to:
      #define MQTT_MAX_PACKET_SIZE 2048
*/
#include <PubSubClient.h>

#include "./sha256.h"
#include "./base64.h"
#include "./parson.h"
#include "./utils.h"
#include "./config.h"   //use different config file for github purposes-delete line when download
//#include "./configure.h"  //uncomment when download

bool wifiConnected = false;
bool mqttConnected = false;

String iothubHost;
String deviceId;
String sharedAccessKey;

WiFiSSLClient wifiClient;
PubSubClient *mqtt_client = NULL;

#define TELEMETRY_SEND_INTERVAL 5000  // telemetry data sent every 5 seconds
#define SENSOR_READ_INTERVAL  2500    // read sensors every 2.5 seconds

long lastTelemetryMillis = 0;
long lastSensorReadMillis = 0;

//telemetry data
float temperature, 
    humidity,
    soilMoisture,
    pressure;

//Sensors
DHT dht(11, DHT11); //temperature and humidity sensor
const int moistSensorPin = A0;


// grab the current time from internet time service
unsigned long getNow()
{
    IPAddress address(129, 6, 15, 28); // time.nist.gov NTP server
    const int NTP_PACKET_SIZE = 48;
    byte packetBuffer[NTP_PACKET_SIZE];
    WiFiUDP Udp;
    
    Udp.begin(2390);

    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    packetBuffer[0] = 0b11100011;     // LI, Version, Mode
    packetBuffer[1] = 0;              // Stratum, or type of clock
    packetBuffer[2] = 6;              // Polling Interval
    packetBuffer[3] = 0xEC;           // Peer Clock Precision
    packetBuffer[12]  = 49;
    packetBuffer[13]  = 0x4E;
    packetBuffer[14]  = 49;
    packetBuffer[15]  = 52;
    
    Udp.beginPacket(address, 123);
    Udp.write(packetBuffer, NTP_PACKET_SIZE);
    Udp.endPacket();

    // wait to see if a reply is available
    int waitCount = 0;
    while (waitCount < 20)
    {
        delay(500);
        waitCount++;
        if (Udp.parsePacket() )
        {
            Udp.read(packetBuffer, NTP_PACKET_SIZE);
            
            unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
            unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
            unsigned long secsSince1900 = highWord << 16 | lowWord;

            Udp.stop();
            
            Serial.println("Got current time!");
            
            return (secsSince1900 - 2208988800UL);
        }
    }
    Serial.println("Failed to get current time. :(");
    return 0;
}

// IoT Hub MQTT publish topics
static const char IOT_EVENT_TOPIC[] = "devices/{device_id}/messages/events/";
static const char IOT_DIRECT_METHOD_RESPONSE_TOPIC[] = "$iothub/methods/res/{status}/?$rid={request_id}";

// IoT Hub MQTT subscribe topics
static const char IOT_DIRECT_MESSAGE_TOPIC[] = "$iothub/methods/POST/#";

// split the connection string into it's composite pieces
void splitConnectionString()
{
    String connStr = (String)iotConnStr;
    int hostIndex = connStr.indexOf("HostName=");
    int deviceIdIndex = connStr.indexOf(";DeviceId=");
    int sharedAccessKeyIndex = connStr.indexOf(";SharedAccessKey=");
    
    iothubHost = connStr.substring(hostIndex + 9, deviceIdIndex);
    deviceId = connStr.substring(deviceIdIndex + 10, sharedAccessKeyIndex);
    sharedAccessKey = connStr.substring(sharedAccessKeyIndex + 17);
}


// process direct method requests
void handleDirectMethod(String topicStr, String payloadStr)
{
    String msgId = topicStr.substring(topicStr.indexOf("$RID=") + 5);
    String methodName = topicStr.substring(topicStr.indexOf("$IOTHUB/METHODS/POST/") + 21, topicStr.indexOf("/?$"));
    
    Serial_printf("Direct method call:\n\tMethod Name: %s\n\tParameters: %s\n", methodName.c_str(), payloadStr.c_str());
    
    if (strcmp(methodName.c_str(), "ECHO") == 0)
    {
        // acknowledge receipt of the command
        String response_topic = (String)IOT_DIRECT_METHOD_RESPONSE_TOPIC;
        
        response_topic.replace("{request_id}", msgId);
        response_topic.replace("{status}", "200");  //OK
        mqtt_client->publish(response_topic.c_str(), "");

        // output the message as morse code
        JSON_Value *root_value = json_parse_string(payloadStr.c_str());
        JSON_Object *root_obj = json_value_get_object(root_value);
        const char* msg = json_object_get_string(root_obj, "displayedValue");
        
        json_value_free(root_value);
    }
}


// callback for MQTT subscriptions
void callback(char* topic, byte* payload, unsigned int length)
{
    String topicStr = (String)topic;
    topicStr.toUpperCase();
    payload[length] = '\0';
    String payloadStr = (String)((char*)payload);

    if (topicStr.startsWith("$IOTHUB/METHODS/POST/")) // direct method callback
        handleDirectMethod(topicStr, payloadStr);
        
    else // unknown message
        Serial_printf("Unknown message arrived [%s]\nPayload contains: %s", topic, payloadStr.c_str());
}

// connect to Azure IoT Hub via MQTT
void connectMQTT(String deviceId, String username, String password)
{
    mqtt_client->disconnect();

    Serial.println("\nStarting IoT Hub connection");
    
    int retry = 0;
    
    while(retry < 10 && !mqtt_client->connected())
    {     
        if (mqtt_client->connect(deviceId.c_str(), username.c_str(), password.c_str()))
        {
                Serial.println("===> mqtt connected");
                mqttConnected = true;
        }
        else
        {
            Serial.print("---> mqtt failed, rc=");
            Serial.println(mqtt_client->state());
            delay(2000);
            retry++;
        }
    }
}

// create an IoT Hub SAS token for authentication
String createIotHubSASToken(char *key, String url, long expire){
    url.toLowerCase();
    String stringToSign = url + "\n" + String(expire);
    int keyLength = strlen(key);

    int decodedKeyLength = base64_dec_len(key, keyLength);
    char decodedKey[decodedKeyLength];

    base64_decode(decodedKey, key, keyLength);

    Sha256 *sha256 = new Sha256();
    sha256->initHmac((const uint8_t*)decodedKey, (size_t)decodedKeyLength);
    sha256->print(stringToSign);
    char* sign = (char*) sha256->resultHmac();
    int encodedSignLen = base64_enc_len(HASH_LENGTH);
    char encodedSign[encodedSignLen];
    base64_encode(encodedSign, sign, HASH_LENGTH);
    delete(sha256);

    Serial.println("SAS Token created!");

    return "SharedAccessSignature sr=" + url + "&sig=" + urlEncode((const char*)encodedSign) + "&se=" + String(expire);
    
    //SharedAccessSignature sig={signature-string}&se={expiry}&sr={URL-encoded-resourceURI}  (From https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-mqtt-support#using-the-mqtt-protocol-directly-as-a-device)
    
    //return "SharedAccessSignature sig=" + url + "&se=" + String(expire) + "&sr=" + urlEncode((const char*)encodedSign);
}

// reads the value from the onboard LSM6DS3 sensor
void readSensors()
{
    soilMoisture = analogRead(moistSensorPin);

    soilMoisture = 100 - soilMoisture * 100 / 1023;   //sensor value: 0-1023; we want percentage

    temperature = dht.readTemperature();

    humidity = dht.readHumidity();

    pressure = random(1, 100);
}

// arduino setup function called once at device startup
void setup()
{
    Serial.begin(115200);

    dht.begin();
    
    // attempt to connect to Wifi network:
    Serial.print("WiFi Firmware version is ");
    Serial.println(WiFi.firmwareVersion());

    int status = WL_IDLE_STATUS;

    Serial_printf("Attempting to connect to Wi-Fi SSID: %s ", wifi_ssid);

    status = WiFi.begin(wifi_ssid, wifi_password);
    
    while ( status != WL_CONNECTED)
    {
        Serial.print(".");
        delay(1000);
    }
    Serial.println("\nConnected!");

    delay(1000); //time for wifi and dht to setup

    splitConnectionString();

    // create SAS token and user name for connecting to MQTT broker
    String url = iothubHost + urlEncode(String("/devices/" + deviceId).c_str());
    
    char *devKey = (char *)sharedAccessKey.c_str();
    
    long expire = getNow() + 864000; //expire in 10 days
    
    String sasToken = createIotHubSASToken(devKey, url, expire);
    
    String username = iothubHost + "/" + deviceId + "/api-version=2016-11-14";
    //String username = iothubHost + "/" + deviceId + "/?api-version=2018-06-30";

    // connect to the IoT Hub MQTT broker
    wifiClient.connect(iothubHost.c_str(), 8883);
    
    mqtt_client = new PubSubClient(iothubHost.c_str(), 8883, wifiClient);
    
    connectMQTT(deviceId, username, sasToken);
    
    mqtt_client->setCallback(callback);

    // // add subscriptions
    mqtt_client->subscribe(IOT_DIRECT_MESSAGE_TOPIC); // direct messages

    // initialize timers
    lastTelemetryMillis = millis();
}

// arduino message loop - do not do anything in here that will block the loop
void loop()
{
    if (mqtt_client->connected())
    {
        // give the MQTT handler time to do it's thing
        mqtt_client->loop();

        // read the sensor values
        if (millis() - lastSensorReadMillis > SENSOR_READ_INTERVAL)
        {
            readSensors();
            lastSensorReadMillis = millis();
        }
        
        // send telemetry values every 5 seconds
        if (millis() - lastTelemetryMillis > TELEMETRY_SEND_INTERVAL)
        {
            Serial.println("Sending telemetry ...");

            String topic = (String)IOT_EVENT_TOPIC;
            topic.replace("{device_id}", deviceId);

            String payload = "{\"temperature\": {temp}, \"humidity\": {hum}, \"soil_moisture\": {moist}, \"pressure\": {p}}";

            payload.replace("{temp}", String(temperature));
            payload.replace("{hum}", String(humidity));
            payload.replace("{moist}", String(soilMoisture));
            payload.replace("{p}", String(pressure));
            
            Serial_printf("\t%s\n", payload.c_str());

            mqtt_client->publish(topic.c_str(), payload.c_str());

            lastTelemetryMillis = millis();
        }
    }
}