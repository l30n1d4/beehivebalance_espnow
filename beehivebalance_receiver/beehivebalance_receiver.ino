#include <esp_now.h>
#include <WiFi.h>
#include <Arduino_JSON.h>
#include <HTTPClient.h>

const char* ssid = "SSID"; // Replace with your network credentials (STATION)
const char* password = "PASS";

const char* serverName = "http://YOURSITE.org/insert.php"; // Your Domain name with URL path or IP address with path

typedef struct struct_message { // Structure to receive data. Must match the send structure
  int station;
  float temp;
  float weight;
  float batt;
  int reading;
} struct_message;

struct_message incomingReadings;
bool sendHTTPReq = false;
unsigned long previousMillis = 0;
const long interval = 21600000; //6 ore
String jsonString;
JSONVar board;

// callback function that will be executed when data is received
void OnDataRecv(const uint8_t * mac_addr, const uint8_t *incomingData, int len) { 
  char macStr[18]; // Copies the sender mac address to a string
  Serial.print("Packet received from: ");
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.println(macStr);
  memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));
  
  board["station"] = incomingReadings.station;
  board["temperature"] = floatRound(incomingReadings.temp);
  board["weight"] = floatRound(incomingReadings.weight);
  board["battery"] = floatRound(incomingReadings.batt);
  board["reading"] = String(incomingReadings.reading);
  jsonString = JSON.stringify(board);
  sendHTTPReq = true;
}

void setup() {
  previousMillis = millis();
  Serial.begin(115200);
  initWifi();
  initEspNow();
}

void initWifi() {
  WiFi.mode(WIFI_AP_STA); // Set the device as a Station and Soft Access Point simultaneously (for esp_now)
  WiFi.begin(ssid, password);
  WiFi.softAP("ESP32_receiver", "1234567890", NULL, 1); // Set the Soft Access Point to hide SSID (i don't need to connect to the access point)

  Serial.print("Connecting to SSID: ");
  Serial.print(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("connected");
  Serial.print("ESP Board MAC Address: ");
  Serial.print(WiFi.macAddress());
  Serial.print(" | Station IP Address: ");
  Serial.print(WiFi.localIP());
  Serial.print(" | Wi-Fi Channel: ");
  Serial.print(WiFi.channel());
  Serial.print(" | RSSI: ");
  Serial.println(WiFi.RSSI());
}

void initEspNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
}

void makePostRequest(String bodyReq) {
  HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST(bodyReq);
    if (httpResponseCode == HTTP_CODE_OK) {
      Serial.print("REQUEST: ");
      Serial.println(bodyReq);
      String payload = http.getString();
      Serial.print("RESPONSE: ");
      Serial.println(payload);
    } else {
      Serial.print("HTTP Response code: ");
      Serial.print(httpResponseCode);
      Serial.print(" => ");
      Serial.println(http.errorToString(httpResponseCode));
    }
    http.end();
}

String floatRound(float in) { // Return with 2 decimal
  float rounded = round(in * 100) / 100.0;
  return (String)rounded;
}
 
void loop() {
  if (sendHTTPReq) {
    sendHTTPReq = false;
    Serial.print(" | RSSI: "); Serial.println(WiFi.RSSI());
    makePostRequest(jsonString);
  }

  if (millis() - previousMillis >= interval) {
    Serial.println("ESP restart...");
    ESP.restart();
  }
}