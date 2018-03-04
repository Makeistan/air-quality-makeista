// -------------------------// Internal/ External Import
#include <ESP8266WiFi.h>    // Wifi Liberary (Node MCU Core)
#include <WiFiClient.h>     // TCP/Client    (Node MCU Core)
#include <WiFiServer.h>     // TCP/Server    (Node MCU Core)
#include <string.h>         // String        (Arduino)
#include <EEPROM.h>         // Accessing EEPROM (Arduino)
#include <StorageIO.h>      // To save Wifi SSID/Password
#include <Indicator.h>      // To Show some output
#include <FirebaseArduino.h>// For using Firebase(Google)
//--------------------------//-----------------------------


//------------------------------------// Globalveriables
StorageIO rom;                         // It will be used to Read/Write wifidata on ROM
Indicator light(D4);                  // To show some output about operations going on. Change pin if you want to use external LED or anything else
//====================================// Wifi Connection Attempt settings
#define WIFI_TIME_OUT 12              // Try to connect with wifi for (Seconds)
#define WAIT_TIME     1               // Check for connection status after every (Second)
int     connecTime = 0;               // Time Passed Since attempt made to connect to Wifi.
//====================================// Device HotSpoT Settings
#define HOTSPOT_SSID      "Air Quality Makeistan"  // Name of Hot Spot
#define HOTSPOT_PASSWORD  "saveairsaveplanet"      // Password of Hot spot
#define PORT_APP_SERVER    2525       // Port at which TCP/Server will start
WiFiServer server(PORT_APP_SERVER);   // TCP/Server to recieve password.
//====================================// FireBase Settings
#define FIREBASE_HOST "air-quality-makeistan.firebaseio.com"
#define FIREBASE_AUTH "LaU9VISYvXLe1Oe4SNgSb114jWm3gAe2p56UHpXs"
#define DEVICE_ID     "test"
#define LOCATION_PATH "test/location"
//=====================================// TCP Data recieving protocole(Data will be recieved in token seperated string)
#define TOKEN ','


#define INTERRUPT_PIN D2
volatile bool setSettings = false;


void wifiConnection();
void setLocationOnFireBase();
void resetPassword();
void parseAndWriteDataOnROM(String);
void showConnecWait();
void settingsResetRequest() {setSettings = true;}
void locationReset();

//-----------------------// Dust Sensor Imports and veriables
#define DUST_SENSOR_PIN D8
byte buff[2];
int pin = D8; //DSM501A input D8
unsigned long previousMillis = millis();
unsigned long duration;
unsigned long starttime;
unsigned long endtime;
unsigned long sampletime_ms = 30000;
float lowpulseoccupancy = 0;
float lowpulseinsec = 0;
float ratio = 0;
float concentration = 0;

void setup() {
  Serial.begin(9600); // - Initalized Serial Communication - //
  wifiConnection();
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  setLocationOnFireBase();
  pinMode(INTERRUPT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), settingsResetRequest, HIGH);
  
  pinMode(DUST_SENSOR_PIN, INPUT);
  starttime = millis();
}


void loop()
{
  
  // Code incorporated from something.ino
  duration = pulseIn(pin, LOW);
  lowpulseoccupancy += duration;
  endtime = millis();
  
  if ( (endtime - starttime) > sampletime_ms) {
    
    // Compute values
    lowpulseinsec = (lowpulseoccupancy - endtime + starttime + sampletime_ms) / 1000;
    
    // Integer percentage 0=>100
    ratio = lowpulseinsec / 3000;
    concentration = 1.1 * pow(ratio,3)
                    - 3.8 * pow(ratio,2) 
                    + 520 * ratio + 0.62; // using spec sheet curve
    
    concentration = (concentration - 1443) * 0.213;
    
    // Show on serial monitor
    Serial.print("\nlowpulseoccupancy:" + (String) lowpulseoccupancy);
    Serial.print("\tratio:" + (String) ratio + "\tDSM501A:");
    Serial.println(concentration);

    sendSensorDataOnFireBase(concentration);
    
    starttime = millis();
    lowpulseinsec = 0;
    ratio = 0;
    lowpulseoccupancy= 0;
  }

  
  if ( setSettings )
    resetSettings();
}




void wifiConnection(){

  char * ssid = rom.readNextString();
  char * password = rom.readNextString();

  // Try to Make connection with Wifi
  WiFi.begin(ssid, password);
  delay(1000);
  Serial.println("Connection Request:" + (String) ssid);
  Serial.println("Password:" + (String) password);
  
  while (WiFi.status() != WL_CONNECTED) {  
    light.blink();
    Serial.print(".");
    
    if (connecTime > WIFI_TIME_OUT){
      Serial.println("\nConnection time out!!");
      resetSettings();
    }
    
    connecTime += WAIT_TIME;
  }
  
  Serial.print(" connected on ip adress:");
  Serial.println(WiFi.localIP());
}





void setLocationOnFireBase(){
  Serial.println();
  Serial.println("Setting up location on firebase");
  Serial.print("Path:");
  Serial.println(LOCATION_PATH);
  
  char * lon = rom.readNextString();
  char * lat = rom.readNextString();
  
  Serial.print("lon:");
  Serial.println(lon);
  Serial.print("lat:");
  Serial.println(lat);
  
  DynamicJsonBuffer jsonBuffer;
  JsonObject& location = jsonBuffer.createObject();
  location["lon"] = lon;
  location["lat"] = lat;
  
  Firebase.set(LOCATION_PATH, location);
  if (Firebase.failed()) {
    Serial.print("pushing /temperature failed:");
    Serial.println(Firebase.error());
    return;
  }
  
  light.blink(400,3,HIGH); // Indicate that location is sent three consective blinks
}




void sendSensorDataOnFireBase(float concentration){
  DynamicJsonBuffer jsonBuffer;
  JsonObject& airQualityObject = jsonBuffer.createObject();
  JsonObject& timeStamp = airQualityObject.createNestedObject("timestamp");
  airQualityObject["dust_25"] = concentration;
  timeStamp[".sv"] = "timestamp";
  Firebase.push(DEVICE_ID, airQualityObject);
  if (Firebase.failed()) {
    Serial.print("pushing /temperature failed:");
    Serial.println(Firebase.error());
    return;
  }
  light.blink(200,1,HIGH,1);
}


void startHotSpot(){
  // Start HoTSpoT so that Moblie can connect to this device
  WiFi.softAP(HOTSPOT_SSID, HOTSPOT_PASSWORD);
  Serial.println("Starting Hotspot . . .");
  delay(2000);
  
  // Show IP/Adress of this device.
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  
}



String recieveDataFromClient(WiFiClient client){
  Serial.println("\nClient avalable");

  while( ! client.available() ){
    showConnecWait();
  }
  
  String s = client.readStringUntil('\0');
  Serial.println("\nRaw Data:" + s);
  return s;
}


WiFiClient TCPGetClient(){
  server.begin();
  
  Serial.println("\nWaiting for TCPClient");
  while(true){
    showConnecWait();
    WiFiClient client = server.available();
    
    if (client != NULL)
      return client;
    
  }
}


void resetSettings(){
  startHotSpot();                         // Turn on HoTSpoT
  WiFiClient client = TCPGetClient();
  String rawData = recieveDataFromClient(client);
  Serial.println(rawData);
  parseAndWriteDataOnROM(rawData);
  delay(100);
  ESP.restart();
}


void parseAndWriteDataOnROM(String data){
  rom.reposition();
  // Find first Token and fetch SSID
  int firstTokenIndex = data.indexOf(TOKEN);
  int secondTokenIndex = data.indexOf(TOKEN, firstTokenIndex + 1);
  int thirdTokenIndex = data.indexOf(TOKEN, secondTokenIndex + 1);
    
  String ssid = data.substring(0, firstTokenIndex);
  rom.writeNextString(ssid);
  Serial.println(ssid);

  // Fetch Passwords
  String password = data.substring(firstTokenIndex + 1, secondTokenIndex);
  rom.writeNextString(password);
  Serial.println(password);
  
  String lon = data.substring(secondTokenIndex + 1, thirdTokenIndex);
  rom.writeNextString(lon);
  Serial.println(lon);

  String lat = data.substring(thirdTokenIndex + 1);
  rom.writeNextString(lat);
  Serial.println(lat);
}


void showConnecWait(){
  Serial.print('.');
  light.blink(10, 50, HIGH, 0.9);
}

