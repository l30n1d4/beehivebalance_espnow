#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <Arduino_JSON.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <HX711.h>
#include "soc/rtc.h"

#define BOARD_ID 1 // Set your Board ID (ESP32 Sender #1 = BOARD_ID 1, ESP32 Sender #2 = BOARD_ID 2, etc)

#define ONE_WIRE_BUS 33 // GPIO where the DS18B20 is connected to
OneWire oneWire(ONE_WIRE_BUS); // Setup a oneWire instance to communicate with any OneWire devices
DallasTemperature sensors(&oneWire); // Pass our oneWire reference to Dallas Temperature sensor 

#define HX711_DOUT_PIN 12 // HX711 circuit wiring DOUT
#define HX711_SCK_PIN 13 // HX711 circuit wiring SCK
HX711 scale;

Preferences preferences;

#define BUTTON_PIN 26 // GPIO26 pin connected to button
#define VOLTMETER_PIN 34 // GPIO34 pin connected to battery

#define uS_TO_S_FACTOR 1000000 // Conversion factor for micro seconds to seconds
#define TIME_TO_SLEEP  600 //Time ESP32 will go to sleep (in seconds)
RTC_DATA_ATTR int bootCount = 0;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // MAC Address of the receiver
const char* ssid = "SSID"; // Insert your SSID

typedef struct struct_message { // Structure to send data. Must match the receiver structure
  int station;
  float temp;
  float weight;
  float batt;
  int reading;
} struct_message;

struct_message myData; // Create a struct_message called myData

int32_t getWiFiChannel(const char *ssid) {
  if (int32_t n = WiFi.scanNetworks()) {
    for (uint8_t i = 0; i < n; i++) {
      if (!strcmp(ssid, WiFi.SSID(i).c_str())) {
        return WiFi.channel(i);
      }
    }
  }
  return 0;
}

float readTemperature() {
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  if(tempC == DEVICE_DISCONNECTED_C) { // Check if reading was successful
    Serial.println("Error: Could not read temperature data");
  }
  return tempC;
}

float readWeight() {
  float weight;
  preferences.begin("hiveMon", true); // Namespace is opened in read-only (RO) mode
  float calFactor = preferences.getFloat("calFactor", 0.0);
  float scaleOffset = preferences.getFloat("scaleOffset", 0.0);
  preferences.end();
  scale.power_up();
  delay(200);
  if (scale.is_ready()) { // https://forum.arduino.cc/t/drifting-load-cell-in-a-stable-environment/1048252/123
    weight = (scale.get_units(10) - scaleOffset) / calFactor;
  } else {
    Serial.println("HX711 not found.");
  }
  scale.power_down(); // Put the ADC in sleep mode
  return weight;
}

float readBattery() {
  // https://randomnerdtutorials.com/power-esp32-esp8266-solar-panels-battery-level-monitoring/
  // https://docs.espressif.com/projects/esp-idf/en/v4.3.3/esp32/api-reference/peripherals/adc.html
  // Vout = (Vin * R2) / (R1 + R2)
  // Vout = (4.2 * 100k) / (27k + 100k) = 3.3V
  // (Vin)-----[R1_27k]---+---[R2_100k]-----(GND)
  //                      |
  //                   (Vout)
  uint16_t analogLevel = analogRead(VOLTMETER_PIN);
  uint16_t minLevel = 3100.0f; // 3.3v lithium battery discharged
  if (analogLevel < minLevel) { 
    analogLevel = minLevel;
  }
  float batteryLevel = map(analogLevel, minLevel, 4095.0f, 0, 100);
  return batteryLevel;
}

void CalibratingScale() { //https://randomnerdtutorials.com/esp32-load-cell-hx711/
  int knownWeight = 650; //grams
  scale.power_up();
  delay(200);
  if (scale.is_ready()) {
    Serial.print("Tare: remove any weights from the scale... ");
    delay(4000);
    float scaleOffset = scale.read_average(20);
    scale.tare();
    Serial.println("done");
    Serial.print("Place a known weight of " + (String)knownWeight + " grams on the scale... ");
    delay(4000);
    float reading = scale.get_units(10); //scale.get_value(10);
    Serial.println("done");
    float calFactor = reading / knownWeight;
    scale.power_down(); // Put the ADC in sleep mode
    Serial.print("Calibration Factor: ");
    Serial.print(calFactor);
    Serial.print(" | ScaleOffset: ");
    Serial.println(scaleOffset);
    preferences.begin("hiveMon", false); // Namespace is opened in read-write (RW) mode
    preferences.putFloat("calFactor", calFactor);
    preferences.putFloat("scaleOffset", scaleOffset);
    preferences.end();
  } else {
    Serial.println("HX711 not found.");
  }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) { // Callback when data is sent
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.println(" => ESP32 deep_sleep " + String(TIME_TO_SLEEP) + " seconds");
  esp_deep_sleep_start();
}
 
void setup() {
  delay(500); // For the deep_sleep bug
  Serial.begin(115200);

  rtc_cpu_freq_config_t config; // Reduce CPU freq for problem with HX711
  rtc_clk_cpu_freq_get_config(&config);
  rtc_clk_cpu_freq_to_config(RTC_CPU_FREQ_80M, &config);
  rtc_clk_cpu_freq_set_config_fast(&config);

  scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN); // Initialize HX711
  sensors.begin(); // Start the DS18B20 sensor
  pinMode(VOLTMETER_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if(digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("Starting calibrate scale...");
    CalibratingScale();
  }

  if (++bootCount > 1000) {
    ESP.restart();
  }

  myData.station = BOARD_ID;
  myData.batt = readBattery();
  myData.temp = readTemperature();
  myData.weight = readWeight();
  myData.reading = bootCount;

  JSONVar board;
  board["station"] = myData.station;
  board["temperature"] = myData.temp;
  board["weight"] = myData.weight;
  board["battery"] = myData.batt;
  board["reading"] = myData.reading;
  Serial.print(JSON.stringify(board));
 
  WiFi.mode(WIFI_STA); // Set device as a Wi-Fi Station and set channel
  int channel = getWiFiChannel(ssid);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) { //Init ESP-NOW
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR); // Set Long Range Mode
  esp_now_register_send_cb(OnDataSent); // Once ESPNow is successfully Init, we will register for Send CB to get the status of Trasnmitted packet
  
  esp_now_peer_info_t peerInfo; // Register peer
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) { // Add peer
    Serial.println("Failed to add peer");
    return;
  }
     
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData)); //Send message via ESP-NOW
  if (result == ESP_OK) {
    Serial.print(" => Sent Success");
  } else {
    Serial.print(" => Sent Fail");
  }
}
 
void loop() {
  // Nothing to do here
}