# Implementing AgroHack on Arduino UNO WiFi Rev. 2

## Whats this about

I was inspired by the AgroHack project (https://github.com/jimbobbennett/AgroHack) and wanted to see if it can be implemented on an even smaller device than the Raspberry Pi. The Arduino Uno WiFi Rev. 2 seemed like the perfect fit to get started with since it has an onboard WiFi and BLE module and more memory than the Uno (48 KB Flash, 6.144 KB SRAM, 256 Bytes EEPROM).

The code was based on firedog1024's repository for connecting the Uno WiFi Rev. 2 to Azure IoT Central (https://github.com/firedog1024/arduino-uno-wifi-iotc).

## Features

* Uses the onboard u-blox NINA-W102 radio module for communicating with Azure IoT Central using WiFi
* Uses simple MQTT library to communicate with Azure IoT Central
* IoT Central features supported
    * Telemetry data - Temperature, humidity and pressure
    * Commands - Send a message to the device to turn on LED to indicate it's time to water the plant
* Weather prediction using Azure Maps

## Installation

Run:

```
git clone https://github.com/Gostas/AgroHackUnoWiFi.git   
```

## Prerequisite

Install the Arduino IDE and the necessary drivers for the Arduino Uno WiFi Rev2 board and ensure that a simple LED blink sketch compiles and runs on the board. Follow the getting started guide here https://www.arduino.cc/en/Guide/ArduinoUnoWiFiRev2.

This code requires a couple of libraries to be installed for it to compile. To install an Arduino library open the Arduino IDE and click the "Sketch" menu and then "Include Library" -> "Manage Libraries". In the dialog filter by the library name below and install the latest version. For more information on installing libraries with Arduino see https://www.arduino.cc/en/guide/libraries.

* Install library "WiFiNINA"
* Install library "PubSubClient"
* Install library "DHT sensor library" by Adafruit

Note - We need to increase the payload size limit in PubSubClient to allow for the larger size of MQTT messages from the Azure IoT Hub. Open the file at %HomePath%\Documents\Arduino\libraries\PubSubClient\src\PubSubClient.h in your favorite code editor. Change the line (line 26 in current version):

```
#define MQTT_MAX_PACKET_SIZE 128
```

to:

```
#define MQTT_MAX_PACKET_SIZE 2048
```

Save the file and you have made the necessary fix.

Also, we need to create the application in Azure IoT Central. For that, I followed the guide provided in AgroHack (https://github.com/jimbobbennett/AgroHack/blob/master/Steps/CreateTheAppInIoTCentral.md).

## Wiring


## Configuration

We need to copy some values from our new IoT Central device into the configure.h file so it can connect to IoT Central. 

There is a tool called DPS KeyGen that given device and application information can generate a connection string to the IoT Hub:

```
git clone https://github.com/Azure/dps-keygen.git
```

in the cloned directory, navigate to the bin folder and choose the correct folder for your operating system (for Windows you will need to unzip the .zip file in the folder).

We now need to grab some values from our application in IoT Central. Go to your application, click on "Devices", then "Environment Sensor" and then onto your device:
Now click on connect:
You will need to use the values from "Scope id", "Device id" and "primary key".

Using the command line UX type:

cd dps-keygen\bin\windows\dps_cstr
dps_cstr <scope_id> <device_id> <primary_key>  //subsitute your values in


```
HostName=iotc-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxx.azure-devices.net;DeviceId=<your device id>;SharedAccessKey=zyGZtz6r5mqta6p7QXOhlxR1ltgHS0quZPgIYiKb9aE=
```

We need to copy this value from the command line to the configure.h file and paste it into the iotConnStr[] line, the resulting line will look something like this.

```
static char iotConnStr[] = "HostName=<host name>.azure-devices.net;DeviceId=<device id>;SharedAccessKey=<shared access key>";
```

You will also need to provide the Wi-Fi SSID (Wi-Fi name) and password in the configure.h

```
// Wi-Fi information
static char wifi_ssid[] = "<replace with Wi-Fi SSID>";
static char wifi_password[] = "<replace with Wi-Fi password>";
```


### Telemetry:

If the device is working correctly you should see output like this in the serial monitor that indicates data is successfully being transmitted to Azure IoT Central:

```

```


### Commands:
