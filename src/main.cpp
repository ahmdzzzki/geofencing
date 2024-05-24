#include <Arduino.h>
#include <ArduinoWebsockets.h>
#include <ESP8266WiFi.h>
#include <TinyGPS++.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

#define M1 0x50 // Alamat Perangkat I2C
#define EEPROM_SIZE 512
const char* ssid = "Hanya Untuk Masyarakat Miskin"; // SSID WiFi
const char* password = "abogoboga"; // Password WiFi
String nopol ="";
String vin = "";
String postUrlTripHistory;
String getUrlTripHistory;
String baseUrlTripHistory ="http://103.190.28.211:3000/api/v1/trip";
//String postUrl1 ="http://192.168.1.156:3000/api/v1/trip?vehicle_id=1HBGH1J787E";
//String ="http://192.168.1.156:3000/api/v1/trip/latest?vehicle_id=";
String baseUrl = "http://103.190.28.211:3000/api/v1/geofencing?vehicle_id="; // Alamat IP server API
String url ;
//String baseUrl = "http://0gw901vv-3000.asse.devtunnels.ms?vehicle_id=1HBGH1J787E"; // Alamat IP server API
String basewebsocketAddress = "ws://103.190.28.211:3300/geofencing?vehicle_id="; // Alamat IP server API  
String websocketAddress ;
//const char* websocketAddress = "ws://0gw901vv-3100.asse.devtunnels.ms?vehicle_id="+vin+"&device=GPS"; // Alamat IP server API
String lastReceivedMessage = "";

const int serverPort = 3100; // Port server WebSocket
static const int RXPin = D5, TXPin = D6 ;
const uint32_t GPSBaud = 38400;
const int buzzer = D7; // Pin untuk buzzer
const int led = D0;
const int led_2 = D8;
const int relay = D8; 

const unsigned int EEPROM_ADDRESS = 0; // Alamat awal di EEPROM untuk menyimpan data
const int MAX_FENCES = 10; 

enum REQUEST_TYPE {
    NONE = -1,
    GET_VIN = 0,
    GET_NOPOL = 1
  };

// The TinyGPS++ object
TinyGPSPlus gps;

// The serial connection to the GPS device
SoftwareSerial ss(RXPin, TXPin); // RX, TX
using namespace websockets;
WebsocketsClient client;

bool isConnected = false;
volatile bool isSendingStolenLocation = false;
volatile bool isSendLocation = false;
volatile bool isOutOfZone = false;
String target ;
bool enableGeofencingData = true;

double firstLat ;
double firstLng ;
double currentLat1mnt;
double currentLng1mnt;
double currentLat3mnt;
double currentLng3mnt;
double currentLat30mnt;
double currentLng30mnt;
unsigned long startTime = 0;
unsigned long secondStartTime = 0;
unsigned long thirdStartTime = 0;
unsigned long raspyTime = 0;
bool finishTime = false ; 
bool isNotChanged3mnt = false;
const long periode = 1000;
bool isFirstDataReceived = false ;

double changeRange  = 0.0004;
double changeStartRange = 0.0002;

int tripId ;

void webSocketConnect();
void wifiConnection();
void getToServer();
void getTrip();
void postToServer(int isEndTrip, int tripId,float lat, float lng);
void onMessageCallback(WebsocketsMessage message);
void getPolygon(const char* GetMessage);
void UpdatePolygon(String incomingMessage);
void processGeofencingData();
void sendStolenLocation();
void sendNotification();
void turnOffEngine();
void sendLocation(String target,float lat , float lng);
DynamicJsonDocument parsingDataJson(String vin, float lat , float lng);
DynamicJsonDocument parsingJsonWebSocket(String event,String GPS ,DynamicJsonDocument data);
void sound_connect();
void sound_outOfZone();
void saveGeofenceRadius(float latitude, float longitude, float radius);
void WriteToEEPROM(REQUEST_TYPE type, int addrOffset, int dataSize);
String readFromEEPROM(int addrOffset, int dataSize);
void saveExternalEEPROM(const char* data);
void readExternalEEPROM(char* buffer, size_t bufferSize);
int parseGeoLocations(const char* geoLocations, double fences[][2]);
bool isInsideGeoFence(double latitude, double longitude, double fences[][2], int numFences);

void setup() {
  Wire.begin();
  Wire.setClock(40000L);
  Serial.begin(9600);
  EEPROM.begin(512);
  ss.begin(GPSBaud);

  while (!Serial){}
    WriteToEEPROM(GET_VIN, 0, 11);  
    WriteToEEPROM(GET_NOPOL, 17, 10); 

    String vinFromEEPROM = readFromEEPROM(0,11);
    if (vinFromEEPROM.length() > 0) {
        Serial.println("vin dibaca dari EEPROM: " + vinFromEEPROM);
        //vin = vinFromEEPROM ;
        vin = "1HBGH1J787E";
    } else {
        Serial.println("Tidak ada vin yang ditemukan di EEPROM.");
        vin = "1HBGH1J787E";
    }
   url = baseUrl +vin+ "&device=GPS";
  websocketAddress = basewebsocketAddress +vin+ "&device=GPS";
  postUrlTripHistory = baseUrlTripHistory+"?vehicle_id="+ vin;
  getUrlTripHistory = baseUrlTripHistory+"/latest?vehicle_id="+vin;

   Serial.println(url);
   Serial.println(websocketAddress);
   Serial.println(postUrlTripHistory);
   Serial.println(getUrlTripHistory);
  
  // String polygonData = readStringFromEEPROM(20); // Start reading from address 20
  // Serial.println("Polygon Data: " + polygonData);
  
  pinMode(buzzer, OUTPUT); // Menetapkan pin buzzer sebagai output
  pinMode(relay, OUTPUT);
  pinMode(led,OUTPUT);
  digitalWrite(buzzer, LOW);// Menonaktifkan buzzer
  digitalWrite(relay , LOW);
  digitalWrite(led,LOW);
  wifiConnection();
  startTime = millis();
  client.onMessage(onMessageCallback);
 // timer.setInterval(2000,sendLocation)
  getTrip();
  getToServer();
   char readData[512]; 
   readExternalEEPROM(readData, sizeof(readData));
   Serial.print("Data EEPROM");
   Serial.println(readData);
}

void loop() {
  while (ss.available() > 0) {
    gps.encode(ss.read());
   if (!isFirstDataReceived && gps.location.isUpdated()){
      firstLat = gps.location.lat();
      firstLng = gps.location.lng();
      isFirstDataReceived = true;
      Serial.print("Latitude Pertama= ");
      Serial.print(firstLat);
      Serial.print(" Longitude Pertama= ");
      Serial.println(firstLng); 
    }
   if (millis() - startTime >= 60000 && isFirstDataReceived && finishTime == false) { // 1 menit
      currentLat1mnt = gps.location.lat();
      currentLng1mnt = gps.location.lng();

      Serial.print("Data terbaru setelah 1 menit: ");
      Serial.print("Latitude= ");
      Serial.print(currentLat1mnt, 6);
      Serial.print(" Longitude= ");
      Serial.println(currentLng1mnt, 6);

      double latDiff = abs(firstLat - currentLat1mnt);
      double lngDiff = abs(firstLng - currentLng1mnt);

      // Membandingkan data pertama dengan data terbaru
      if (latDiff > changeStartRange || lngDiff > changeStartRange) {
        Serial.println("Data telah berubah setelah 1 menit!");
        secondStartTime = millis(); // Mulai hitungan 3 menit
        finishTime = true;
        postToServer(0,tripId,firstLat,firstLng);
      } else {
        Serial.println("Data tidak berubah setelah 1 menit.");
      }
      startTime = millis();
    }
    // Membandingkan data setelah 3 menit

    if (millis() - secondStartTime >= 180000 && secondStartTime != 0 && finishTime==true) { // 3 menit
      currentLat3mnt = gps.location.lat();
      currentLng3mnt = gps.location.lng();

      Serial.print("Data terbaru setelah 3 menit: ");
      Serial.print("Latitude= ");
      Serial.print(currentLat3mnt, 6);
      Serial.print(" Longitude= ");
      Serial.println(currentLng3mnt, 6);

      double latDiff = abs(currentLat1mnt - currentLat3mnt);
      double lngDiff = abs(currentLat3mnt - currentLng3mnt);

      // Membandingkan data pertama dengan data terbaru setelah 3 menit
      if (latDiff < changeRange || lngDiff < changeRange) {
        isNotChanged3mnt = true;
        Serial.println("Data tidak berubah setelah 3 menit");
        postToServer(0,tripId,currentLat3mnt,currentLng3mnt);
      } else {
        Serial.println("Data berubah setelah 3 menit");
      }
      secondStartTime = 0; // Reset secondStartTime
    }
    if (thirdStartTime == 0 && isNotChanged3mnt) { // 30 menit setelah data tidak berubah
      thirdStartTime = millis(); // Mulai timer 30 menit
    }

    if (thirdStartTime != 0 && millis() - thirdStartTime >= 600000) { // 30 menit
      currentLat30mnt = gps.location.lat();
      currentLng30mnt = gps.location.lng();

      Serial.print("Data terbaru setelah 30 menit: ");
      Serial.print("Latitude= ");
      Serial.print(currentLat30mnt, 6);
      Serial.print(" Longitude= ");
      Serial.println(currentLng30mnt, 6);

      double latDiff = abs(currentLat3mnt - currentLat30mnt);
      double lngDiff = abs(currentLng3mnt - currentLng30mnt);

      // Membandingkan data pertama dengan data terbaru setelah 30 menit
      if (latDiff < changeRange || lngDiff < changeRange) {
        Serial.println("Data tidak berubah setelah 30 menit!");
        postToServer(1,tripId,currentLat30mnt,currentLng30mnt);
        tripId++;
        isFirstDataReceived = false ;
        finishTime = false;
        isNotChanged3mnt = false;
      
      } else {
        Serial.println("Data telah berubah setelah 30 menit.");
      }
      thirdStartTime = 0; // Reset thirdStartTime
    }
  
  // if(millis () - raspyTime >= 1000 && gps.location.isUpdated()){
  //     raspyTime = millis();
  //     DynamicJsonDocument doc(200);
  //     String raspyData;  
      
  //     doc["target"] = "raspy";
  //     doc["vehicle_id"] = vin;
  //     doc["latitude"] = String(gps.location.lat(),6); 
  //     doc["longitude"] = String(gps.location.lng(),6);
  //     doc.shrinkToFit();

  //     serializeJson(doc,raspyData);
  //     Serial.println(raspyData);
  // }
    if (!isConnected) {
      webSocketConnect();
    } else if(gps.location.isUpdated()) {
      //postToServer(gps.location.lat(), gps.location.lng());
        float lat = gps.location.lat();
        float lng = gps.location.lng();
        // Serial.print("Latitude= ");
        // Serial.print(gps.location.lat(), 6);
        // Serial.print(" Longitude= ");
        // Serial.println(gps.location.lng(), 6); 
        //processGeofencingData();
        client.poll();
      if (isSendLocation){
        sendLocation(target,lat,lng);
        }
      if (enableGeofencingData){
      processGeofencingData();
      }
      if (isOutOfZone){
        sound_outOfZone();
      // sendNotification();
        }
      else if(!isOutOfZone){
        digitalWrite(buzzer,LOW);
        //digitalWrite(led,LOW);
      }
    } 
  if (isSendingStolenLocation) {
      sendStolenLocation(); // Kirim lokasi "dicuri" secara terus-menerus
    }
    processGeofencingData();
 }
}

void webSocketConnect() {
  Serial.println("Connecting to WebSocket server...");
  bool connected = client.connect(websocketAddress);
  if (connected) {
    Serial.println("Connected to WebSocket server");
    isConnected = true; // Set flag isConnected menjadi true ketika berhasil terkoneksi
  } else {
    Serial.println("Failed to connect to WebSocket server");
    delay(2000);
  }
}

void wifiConnection() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("."); 
  }
  Serial.println("\nConnected to WiFi. IP Address: " + WiFi.localIP().toString());
  sound_connect();
}

void getToServer() {
  //https
  // WiFiClientSecure client;
  // client.setInsecure();

  WiFiClient client;
  HTTPClient http;
  //String token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VybmFtZSI6ImZhamFyMDIiLCJmdWxsbmFtZSI6IkZhamFyIFRyaSBDYWh5b25vIiwicGFzc3dvcmQiOiJVMkZzZEdWa1gxL2JWUGhuZ21YSVlHclA0eHVTU2NhcVhUV3pSSFFIN0hjPSIsImNyZWF0ZWRfZHQiOiIyMDI0LTAzLTA3VDA4OjMyOjQ2LjAwMFoiLCJjcmVhdGVkX2J5IjoiU1lTVEVNIiwiYWRkcmVzcyI6IkRpIGthcmF3YW5nIGFqYSIsImlhdCI6MTcxMTQyNjU5Nn0.Qf0DgqX7JG-Zqyc9NspMjg2Aizsv61UUY6OmhT6o0YM"; // Ganti dengan token yang valid
      http.begin(client, url);
      http.addHeader("x-device","GPS");
  //    http.setAuthorization(token);
      
  int httpCode = http.GET();
  
  // Memeriksa kode balasan HTTP
  if (httpCode == HTTP_CODE_OK) {
    // Jika berhasil, dapatkan payload respons
    String payload = http.getString();
    payload.trim();
    
    // Cetak payload respons ke Serial Monitor
    if (payload.length() > 0) {
      Serial.println(payload);
      const char* payloadPtr = payload.c_str();
      getPolygon(payloadPtr);
    }
  } else {
    // Jika terjadi kesalahan, cetak kode balasan HTTP
    Serial.print("Error on sending GET: ");
    Serial.println(httpCode);
  }
  http.end();
}

void getTrip() {
  //https
  // WiFiClientSecure client;
  // client.setInsecure();

  WiFiClient client;
  HTTPClient http;
  //String token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VybmFtZSI6ImZhamFyMDIiLCJmdWxsbmFtZSI6IkZhamFyIFRyaSBDYWh5b25vIiwicGFzc3dvcmQiOiJVMkZzZEdWa1gxL2JWUGhuZ21YSVlHclA0eHVTU2NhcVhUV3pSSFFIN0hjPSIsImNyZWF0ZWRfZHQiOiIyMDI0LTAzLTA3VDA4OjMyOjQ2LjAwMFoiLCJjcmVhdGVkX2J5IjoiU1lTVEVNIiwiYWRkcmVzcyI6IkRpIGthcmF3YW5nIGFqYSIsImlhdCI6MTcxMTQyNjU5Nn0.Qf0DgqX7JG-Zqyc9NspMjg2Aizsv61UUY6OmhT6o0YM"; // Ganti dengan token yang valid
    http.begin(client, getUrlTripHistory);
    http.addHeader("x-device","GPS");
    http.addHeader("Content-Type", "application/json");
      
  int httpCode = http.GET();
  
  // Memeriksa kode balasan HTTP
  if (httpCode == HTTP_CODE_OK) {
    // Jika berhasil, dapatkan payload respons
    String payload = http.getString();
    payload.trim();
    
    // Cetak payload respons ke Serial Monitor
    if (payload.length() > 0) {
          Serial.println(payload);
          const char* payloadPtr = payload.c_str();
          getPolygon(payloadPtr);
          StaticJsonDocument<1024> responseData;
          DeserializationError error = deserializeJson(responseData, payloadPtr);
          if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
          }
          int latestTripId = responseData["data"]["latest_trip_id"];

          // Tetapkan nilai latest_trip_id ke variabel trip_id
          tripId = latestTripId + 1;

          Serial.println(tripId); 
          
        }
  } else {
    // Jika terjadi kesalahan, cetak kode balasan HTTP
    Serial.print("Error on sending GET: ");
  }
  http.end();
}

void postToServer(int isEndTrip,int tripId,float lat, float lng) {
  WiFiClient client;
  HTTPClient http;
  
  digitalWrite(led,HIGH);
 // client.setInsecure();

  DynamicJsonDocument doc(200);
  String nodemcuData; 
  
  doc["vehicle_id"] = vin;
  doc["trip_id"] = String(tripId);
  doc["longitude"] = String(lng, 6);    
  doc["latitude"] = String(lat, 6);
  doc["is_end_trip"] = String(isEndTrip) ;
  
  
  http.begin(client, postUrlTripHistory); // Mulai koneksi HTTPS
  http.addHeader("x-device","GPS");
  http.addHeader("Content-Type", "application/json");
 // http.setAuthorization(token);

  serializeJson(doc, nodemcuData);
  Serial.print("POST data >> ");
  Serial.println(nodemcuData); 
    
  int httpCode = http.POST(nodemcuData); // Kirim request
  String payload;  
  if (httpCode > 0) { // Periksa kode balasan    
      payload = http.getString(); // Dapatkan payload respons
      payload.trim();
      
      if( payload.length() > 0 ){
         Serial.println(payload + "\n"); 
      }
  } else {
    Serial.print("Error on sending POST: ");
    Serial.println(httpCode);
  }
  digitalWrite(led,LOW);
  http.end(); // Tutup koneksi
}

void onMessageCallback(WebsocketsMessage message) {

  String incomingMessage = message.data();
  incomingMessage.trim();

  if (incomingMessage == lastReceivedMessage) {
    //Serial.println("Duplicate message received, skipping...");
    return;
  }

  lastReceivedMessage = incomingMessage;

  digitalWrite(led,HIGH);
  delay(1000);
  Serial.println("Received message from server: " + incomingMessage);
  
  DynamicJsonDocument event(1024);
  DeserializationError error = deserializeJson(event, incomingMessage);
  
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }
  
  String device = event["device"].as<String>();
  String eventType = event["event"].as<String>();

  if (eventType == "current_location") {
    isSendLocation = true ;
    target = device ;
  }else if(eventType == "stop_current_location"){
    isSendLocation = false ;
    //Serial.println("result_stop_current_location");
  } else if (eventType == "navigate_to_car") {
    isSendingStolenLocation = true;
  } else if (eventType == "stop_navigate_to_car") {
    isSendingStolenLocation = false;
  } else if (eventType == "turn_off_engine") {
    digitalWrite(relay, HIGH);
    turnOffEngine();
  } else if (eventType == "send_geofence_radius") {
    // geofencingRadius(event);
  } else if (eventType == "update_geofencing") {
    UpdatePolygon(incomingMessage);
    enableGeofencingData = true;
  } else if (eventType == "ignore_geofencing") {
    enableGeofencingData = false;
    isOutOfZone = false;
  }else {
    Serial.println("Unknown message received: " + eventType); // Debug untuk pesan tidak dikenal
  }
  digitalWrite(led,LOW);
}

void getPolygon2(const char* GetMessage) {
  StaticJsonDocument<1024> geofenceData;
  DeserializationError error = deserializeJson(geofenceData, GetMessage);

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Mengambil objek "data"
  JsonObject dataObject = geofenceData["data"].as<JsonObject>();

  // Mengambil nilai dari key "type", "geo_id", dan "geo_locations"
  String geo_type = dataObject["type"].as<String>();
  String geo_id = dataObject["geo_id"].as<String>();
  String geo_locations = dataObject["geo_locations"].as<String>();

  // Membuat JSON object untuk menyimpan data
  StaticJsonDocument<200> saveData;
  saveData["type"] = geo_type;
  saveData["geo_id"] = geo_id;
  saveData["geo_locations"] = geo_locations;

  // Mengkonversi JSON object menjadi string
  String jsonString;
  serializeJson(saveData, jsonString);

  // Konversi String ke char array untuk penyimpanan di EEPROM
  char dataToSave[jsonString.length() + 1];
  jsonString.toCharArray(dataToSave, sizeof(dataToSave));

  // Simpan data ke EEPROM
  saveExternalEEPROM(dataToSave);
}

void getPolygon(const char* GetMessage) {
  StaticJsonDocument<1024> geofenceData;
  DeserializationError error = deserializeJson(geofenceData, GetMessage);

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Mengambil array data
  JsonArray dataArray = geofenceData["data"].as<JsonArray>();

  // Memastikan array data tidak kosong
  if (dataArray.size() == 0) {
    Serial.println("No data found in the array");
    return;
  }

  // Mengambil objek pertama dari array
  JsonObject firstObject = dataArray[0];
  
  // Mengambil geo_id dan geo_locations dari objek pertama
  String geo_type = firstObject["type"].as<String>();
  String geo_id = firstObject["geo_id"].as<String>();
  String geo_locations = firstObject["geo_locations"].as<String>();

  // Membuat JSON object untuk menyimpan data
  StaticJsonDocument<200> saveData;
  saveData["type"] = geo_type;
  saveData["geo_id"] = geo_id;
  saveData["geo_locations"] = geo_locations;

  // Mengkonversi JSON object menjadi string
  String jsonString;
  serializeJson(saveData, jsonString);

  // Konversi String ke char array untuk penyimpanan di EEPROM
  char dataToSave[jsonString.length() + 1];
  jsonString.toCharArray(dataToSave, sizeof(dataToSave));

  // Simpan data ke EEPROM
  saveExternalEEPROM(dataToSave);
}

void UpdatePolygon(String incomingMessage){
  String payload = incomingMessage;
  payload.trim();
      if (payload.length() > 0) {
      Serial.println(payload);
      const char* payloadPtr = payload.c_str();
      getPolygon2(payloadPtr);
    }
}

void processGeofencingData() {
    char readData[512]; // Buffer untuk menyimpan data yang dibaca dari EEPROM
    readExternalEEPROM(readData, sizeof(readData)); // Membaca data geofencing dari EEPROM
    Serial.println(readData);
    // Parse data JSON yang dibaca dari EEPROM
    DynamicJsonDocument doc (512);
    DeserializationError deserializeError = deserializeJson(doc, readData);
    if (deserializeError) {
        Serial.print(F("deserializeJson() failed3: "));
        Serial.println(deserializeError.c_str());
        digitalWrite(led_2,HIGH);
        return;
    }

    char buffer[512];
    size_t n = serializeJson(doc, buffer);
   // Serial.println("Data JSON yang sudah di-parse:");
   // Serial.println(buffer);

    // Ambil nilai 'geo_locations' dari data JSON
    String geo_Types = doc ["type"];
    const char* geoLocations = doc["geo_locations"];
    // Serial.print("Type : ");
    // Serial.print(geo_Types);
    // Serial.print(" geo_locations: ");
    // Serial.println(geoLocations);

    // // Ambil lokasi GPS saat ini
    float lat = gps.location.lat();
    float lng = gps.location.lng();
    // Serial.print("Latitude= ");
    // Serial.print(gps.location.lat(), 6);
    // Serial.print(" Longitude= ");
    // Serial.println(gps.location.lng(), 6);
    
    if(geo_Types == "POLYGON"){
    //Parsing koordinat geofencing dan melakukan pengecekan apakah lokasi GPS berada di dalam geofence
      double fences[MAX_FENCES][2];
      int numFences = parseGeoLocations(geoLocations, fences);
      bool result = isInsideGeoFence(lat, lng, fences, numFences);
      // Serial.print("Is inside geofence?");
      // Serial.println(result ? "Yes" : "No");
    // Set flag isOutOfZone berdasarkan hasil pengecekan geofence
      if (result == false && isOutOfZone == false) {
        sendNotification();
        isOutOfZone = true;
      }
      else if (result == true){
        isOutOfZone = false; 
      }
    }

    else if (geo_Types == "CIRCLE"){
      double centerLat, centerLng;
      float radius;
      sscanf(geoLocations, "%lf,%lf;%f", &centerLat, &centerLng, &radius);
      // Serial.print("Center Latitude: ");
      // Serial.println(centerLat, 10);
      // Serial.print("Center Longitude: ");
      // Serial.println(centerLng, 10);
      // Serial.print("Radius: ");  
      // Serial.println(radius);
      double distance = TinyGPSPlus::distanceBetween(lat,lng, centerLat, centerLng);

      // Serial.print("Distance from center: ");
      // Serial.println(distance);

        // Periksa apakah lokasi GPS berada di dalam radius
      bool result = distance <= radius;
      // Serial.print("Is inside radius?");
      // Serial.println(result ? "Yes" : "No");
      if (result == false && isOutOfZone == false) {
        sendNotification();
        isOutOfZone = true;
      }
      else if (result == true){
        isOutOfZone = false; 
      }
    }
  digitalWrite(led,LOW);
}

void sendStolenLocation() {
  float lat = gps.location.lat();
  float lng = gps.location.lng();

  DynamicJsonDocument data = parsingDataJson(vin,lat, lng);
  DynamicJsonDocument doc = parsingJsonWebSocket("result_navigate_to_car","GPS", data);

  String jsonString;
  serializeJson(doc, jsonString);

  client.send(jsonString);
  Serial.println("Sent data to server: " + jsonString);
  delay(2000);
}

void sendNotification(){
  float lat = gps.location.lat();
  float lng = gps.location.lng();
  
  bool notificationIsSent = false; 

  // // Membuat objek JSON
  // DynamicJsonDocument data(128);
  // data["lat"] = lat;
  // data["lng"] = lng;

  // // Membuat objek JSON untuk pesan lengkap
  // DynamicJsonDocument doc(256);
  // doc["event"] = "mobile_notification";
  // doc["vehicle_id"] = vin;
  // doc["target"] = "MOBILE";
  // doc["type"] = "out_of_zone";
  // doc["data"] = data; 

  DynamicJsonDocument data (128);
  data ["case"] = "out_of_zone";

  DynamicJsonDocument doc(256);
  doc["type"] = "notification";
  doc["target"] = "MOBILE";
  doc["vehicle_id"] = vin;
  doc["data"] = data; 
  // Serialize JSON menjadi string
  String jsonString;
  
  serializeJson(doc, jsonString);

  // Mengirim string ke server
  client.send(jsonString);
  // Debugging
  Serial.println("Sent data to server: " + jsonString);
}

void turnOffEngine () {
  DynamicJsonDocument doc(200);
  doc["status"] = "success";

  String jsonString;
  serializeJson(doc, jsonString);
  // Mengirim data ke server
  client.send(jsonString);
  Serial.println("Sent data to server: " + jsonString);
  delay(2000);
}

void sendLocation(String target,float lat , float lng) {
  // if (gps.location.isUpdated()) {
    DynamicJsonDocument data = parsingDataJson(vin, lat, lng);
    DynamicJsonDocument doc = parsingJsonWebSocket("result_current_location", target, data);  // Menggunakan 'target' di sini

    String jsonString;
    serializeJson(doc, jsonString);

    client.send(jsonString);
    Serial.println("Sent data to server: " + jsonString);
  //  } else {
  //    Serial.println("GPS location is not valid.");
  //  }
  delay(2000);
}

DynamicJsonDocument parsingDataJson(String vin, float lat , float lng){
    DynamicJsonDocument doc_data(200);
    doc_data["vin"] = vin;
    doc_data["lat"] = String(lat, 6);
    doc_data["lng"] = String(lng, 6);  
    return doc_data;
}

DynamicJsonDocument parsingJsonWebSocket(String event ,String target,DynamicJsonDocument data) 
{
 
    DynamicJsonDocument doc(200);
    doc["event"] = event;
    doc["target"] = target;
    doc['device'] = 'GPS',
    doc["data"] = data;
    return doc;
}

void sound_connect(){
  for (int i = 0; i < 3; i++) {
    digitalWrite(buzzer, HIGH);
    delay(100);
    digitalWrite(buzzer, LOW);
    delay(100);
  }
}

void sound_outOfZone(){
  for (int i = 0; i < 3; i++) {
    digitalWrite(buzzer, HIGH);
    delay(300);
    digitalWrite(buzzer, LOW);
    delay(300);
  }
}

void WriteToEEPROM(REQUEST_TYPE type, int addrOffset, int dataSize) {
    Wire.beginTransmission(0x12);   // Start channel with slave 0x12
    Wire.write(type);               // send data to the slave
    Wire.endTransmission();         // End transmission
    delay(1000);                    // added to get better Serial print
    Wire.requestFrom(0x12, dataSize); // request data from slave device 0x12
    
    byte buffer[dataSize];          // Buat buffer untuk menampung data dari Wire
    int i = 0;

    // Baca data dari Wire dan simpan dalam buffer
    while (Wire.available()) {      // slave may send less than requested
        buffer[i++] = Wire.read();  // Tambahkan data ke buffer
    }

    // Tulis data dari buffer ke EEPROM
    for (int j = 0; j < i; j++) {
        EEPROM.write(j + addrOffset, buffer[j]); // Tulis setiap byte ke EEPROM
    }
    EEPROM.commit();                // Pastikan untuk menyimpan perubahan

    // Cetak hasil penulisan ke Serial Monitor
    Serial.print("Data ditulis ke EEPROMMM: ");
    for (int j = 0; j < i; j++) {
        Serial.print(char(buffer[j]));
    }
    Serial.println();
    delay(1000);                    // added to get better Serial print
}

String readFromEEPROM(int addrOffset, int dataSize) {
    char data[dataSize + 1];   // Create array to store data from EEPROM
    for (int i = 0; i < dataSize; i++) {
        data[i] = EEPROM.read(addrOffset + i); // Read each character from EEPROM
    }
    data[dataSize] = '\0';      // Terminate string
    return String(data);        // Return data as String
}

void saveExternalEEPROM(const char* data) {
    unsigned int address = EEPROM_ADDRESS; // Alamat awal untuk menyimpan data di EEPROM
    size_t len = strlen(data); // Mendapatkan panjang data JSON

    Serial.print("Data Yang ditulis di EEPROM");
    Serial.println(data);

    // Menghapus data sebelumnya dengan menulis NULL
    for (size_t i = 0; i < len; i++) {
        Wire.beginTransmission(M1); // Mulai transmisi I2C ke perangkat
        Wire.write(address >> 8); // MSB dari alamat EEPROM
        Wire.write(address & 0xFF); // LSB dari alamat EEPROM

            // Wire.write((int)((i >> 8) & 0xFF)); // MSB
            // Wire.write((int)(i & 0xFF)); // LSB


        Wire.write(data[i]); // Menulis karakter JSON ke EEPROM
        Wire.endTransmission(); // Selesai transmisi
        address++; // Pindah ke alamat berikutnya di EEPROM
        delay(20); // Delay untuk memberikan waktu EEPROM untuk menulis data
    }

    // Menulis NULL atau karakter kosong untuk menghapus sisa data lama
    for (size_t i = len; i < EEPROM_SIZE; i++) {
        Wire.beginTransmission(M1); // Mulai transmisi I2C ke perangkat
        Wire.write(address >> 8); // MSB dari alamat EEPROM
        Wire.write(address & 0xFF); // LSB dari alamat EEPROM
        Wire.write('\0'); // Menulis NULL ke EEPROM
        Wire.endTransmission(); // Selesai transmisi
        address++; // Pindah ke alamat berikutnya di EEPROM
        delay(5); // Delay untuk memberikan waktu EEPROM untuk menulis data
    }
}

void readExternalEEPROM(char* buffer, size_t bufferSize) {
  unsigned int address = EEPROM_ADDRESS; // Alamat awal untuk membaca data dari EEPROM
  size_t index = 0; // Indeks untuk buffer
  // Loop untuk membaca data dari EEPROM hingga buffer penuh atau tidak ada data lagi
  while (index < bufferSize - 1) {
    Wire.beginTransmission(M1); // Mulai transmisi I2C ke perangkat
    Wire.write(address >> 8); // MSB dari alamat EEPROM
    Wire.write(address & 0xFF); // LSB dari alamat EEPROM
    Wire.endTransmission(); // Selesai transmisi
    Wire.requestFrom(M1, 1); // Meminta 1 byte data dari EEPROM
    if (Wire.available()) {
      char c = Wire.read(); // Membaca karakter dari EEPROM
      buffer[index++] = c; // Menyimpan karakter ke buffer
      address++; // Pindah ke alamat berikutnya di EEPROM
    } else {
      break; // Keluar dari loop jika tidak ada lagi data yang tersedia di EEPROM
    }
  }
  buffer[index] = '\0'; // Menambahkan null-terminator di akhir buffer untuk menandai akhir string
}

int parseGeoLocations(const char* geoLocations, double fences[][2]) {
  char* temp;
  char* ptr = strdup(geoLocations); // Membuat salinan string geoLocations

  // Menggunakan strtok_r untuk membagi string menjadi token
  int numFences = 0;
  const char* delimiter = ";";
  char* token = strtok_r(ptr, delimiter, &temp);
  while (token != NULL && numFences < MAX_FENCES) {
    // Menggunakan strtok_r untuk membagi token menjadi latitude dan longitude
    char* subToken;
    const char* subDelimiter = ",";
    char* latitude = strtok_r(token, subDelimiter, &subToken);
    char* longitude = strtok_r(NULL, subDelimiter, &subToken);

    // Menyimpan nilai koordinat dalam array 2 dimensi
    fences[numFences][0] = atof(latitude);
    fences[numFences][1] = atof(longitude);

    // Mengambil token selanjutnya
    token = strtok_r(NULL, delimiter, &temp);
    numFences++;
  }

  free(ptr); // Membuang memori yang dialokasikan untuk salinan string
  return numFences; // Mengembalikan jumlah fences yang berhasil diparsing
}

bool isInsideGeoFence(double latitude, double longitude, double fences[][2], int numFences) {
    double vectors[numFences][2];
    for(int i = 0; i < numFences; i++){
        vectors[i][0] = fences[i][0] - latitude;
        vectors[i][1] = fences[i][1] - longitude;
    }
    double angle = 0;
    double num, den;
    for(int i = 0; i < numFences; i++){
        num = (vectors[i%numFences][0])*(vectors[(i+1)%numFences][0]) + (vectors[i%numFences][1])*(vectors[(i+1)%numFences][1]);
        den = (sqrt(pow(vectors[i%numFences][0],2) + pow(vectors[i%numFences][1],2)))*(sqrt(pow(vectors[(i+1)%numFences][0],2) + pow(vectors[(i+1)%numFences][1],2)));
        angle += (180 * acos(num / den) / M_PI);
    }
    return (angle > 355 && angle < 365);
}