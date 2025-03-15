#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "config.h" // Include the configuration file - see config.example.h for an example

//#define DEBUG_NO_SEND

#define HOST_NAME "kanal"
#define SERVICE_NAME "Pumpen Alarm"

// JSON payloads for different statuses
const char* HOST_ONLINE = "{\"cmd\":\"PROCESS_HOST_CHECK_RESULT\",\"host\":\"" HOST_NAME "\",\"status_code\":\"0\",\"plugin_output\":\"ONLINE\"}";
const char* PUMPE_OK = "{\"cmd\":\"PROCESS_SERVICE_CHECK_RESULT\",\"host\":\"" HOST_NAME "\",\"service\":\"" SERVICE_NAME "\",\"plugin_output\":\"OK\",\"plugin_state\":0 }";
const char* PUMPE_FEHLER = "{\"cmd\":\"PROCESS_SERVICE_CHECK_RESULT\",\"host\":\"" HOST_NAME "\",\"service\":\"" SERVICE_NAME "\",\"plugin_output\":\"FEHLER\",\"plugin_state\":2 }";

#define SWITCH_PIN 23  // kontakt zwischen einem digitalen Pin GPIO 23 und GND anschließen.

#define WIFI_RETRY_LIMIT 30 // Number of WiFi connection attempts before restarting
#define WIFI_RETRY_DELAY 500 // Delay in milliseconds between WiFi connection attempts
#define NTP_SYNC_DELAY 500 // Delay in milliseconds between NTP synchronization attempts
#define NTP_SYNC_TIMEOUT 30 // Timeout in seconds for NTP synchronization

const long INTERVAL = 300*1000L; // Interval for periodic status check
const long CLOCK_UPDATE_INTERVAL = 3600*1000L*4L; // 4 hour

unsigned long previousMillis = 0; // Initial state
unsigned long clockUpdateMillis = 0; // Initial state
int lastState = 0x2; // Initial state of the switch
long rssi = 0; // Initial state of the RSSI
time_t startTime = 0; // Variable to store the start time, initialized to 0
time_t uptime = 0; // Variable to store the start time, initialized to 0


// Function to set the system clock using NTP with timeout
void setClock() {
  configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", "de.pool.ntp.org", "pool.ntp.org", "time.nist.gov"); // Set timezone to Europe/Berlin with DST

  Serial.print(F("Waiting for NTP time sync: "));
  time_t nowSecs = time(nullptr);
  unsigned long startMillis = millis();
  while (nowSecs < 8 * 3600 * 2 && millis() - startMillis < NTP_SYNC_TIMEOUT * 1000) {
    delay(NTP_SYNC_DELAY);
    Serial.print(F("."));
    yield();
    nowSecs = time(nullptr);
  }

  if (nowSecs >= 8 * 3600 * 2) {
    Serial.println(F("NTP time sync successful"));
    if (startTime == 0) { // Check if startTime is not already set
      startTime = nowSecs; // Store the start time
    }
  } else {
    Serial.println(F("NTP time sync failed"));
    ESP.restart();
  }

  struct tm timeinfo;
  localtime_r(&nowSecs, &timeinfo);
  Serial.print(F("Current time: "));
  Serial.print(asctime(&timeinfo));
}

// Function to wait for WiFi connection
bool waitWifi() {
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(WIFI_RETRY_DELAY);
    Serial.print(".");
    if (wifiAttempts > WIFI_RETRY_LIMIT) {
      wifiAttempts = 0;
      ESP.restart();
    }
    wifiAttempts++;
  }
  rssi = WiFi.RSSI();
  return WiFi.status() == WL_CONNECTED;
}

// Function to print WiFi connection information
void printWifiInfo() {
  Serial.println("WiFi connected");
  Serial.print("RSSI: ");
  Serial.println(WiFi.RSSI());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("DNS address: ");
  Serial.println(WiFi.dnsIP());
  Serial.print("Gateway address: ");
  Serial.println(WiFi.gatewayIP());
}


// Function to set up WiFi connection
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(SSID);
  WiFi.begin(SSID, PSK);

  if (!waitWifi()) {
    ESP.restart(); // should never happen
  }
  
  printWifiInfo();
  setClock(); // Set the clock after WiFi is connected
  clockUpdateMillis = millis(); // Initialize the clock update timer
}

// Arduino setup function
void setup() {
  pinMode(SWITCH_PIN, INPUT_PULLDOWN);
  Serial.begin(115200); // Starts the serial communication
  setup_wifi();
}

// Function to send status to the server
void sendStatus(String payload) {
  #ifdef DEBUG_NO_SEND
    Serial.println("DEBUG_NO_SEND!");
    Serial.println(payload);
    return;
  #endif
  WiFiClientSecure *sclient = new WiFiClientSecure;
  if (sclient) {
    sclient->setInsecure();
    {
      HTTPClient http;
      if (waitWifi()) {
        http.begin(*sclient, THRUK_URL);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-Thruk-Auth-Key", API_KEY);
        int httpCode = http.PUT(payload);
        if (httpCode > 0) {
          Serial.print("HTTP-Status: ");
          Serial.println(httpCode);
          String response = http.getString();
          Serial.print(response);
        } else {
          Serial.print("HTTP-Fehler: ");
          Serial.println(http.errorToString(httpCode));
        }
        http.end();
      }
    }
    delete sclient;
  }
}

String getFormattedUptime() {
  time_t nowSecs = time(nullptr);
  time_t uptime_local = nowSecs - startTime;
  
  int days = uptime_local / 86400;
  uptime_local %= 86400;
  int hours = uptime_local / 3600;
  uptime_local %= 3600;
  int minutes = uptime_local / 60;

  String formattedUptime = "Uptime: " + String(days) + " day(s) " + String(hours) + " hour(s) " + String(minutes) + " minute(s)";
  return formattedUptime;
}

// Function to send status based on switch state
void sendSwitchStatus(int switchState) {
  if (switchState == LOW) {  // Pumpe - OK // kontakt offen
    Serial.println("Pumpe - OK");
    sendStatus(PUMPE_OK);
  } else if (switchState == HIGH) {  // Pumpen - Fehler // kontakt geschlossen
    Serial.println("Pumpe - Fehler");
    sendStatus(PUMPE_FEHLER);
  }
  
  String HOST_ONLINE_WITH_PERFORMANCE_DATA = HOST_ONLINE;
  HOST_ONLINE_WITH_PERFORMANCE_DATA.replace("ONLINE", "OK - " + getFormattedUptime() + " | rssi=" + String(rssi) + ", uptime=" + String(uptime) );
  sendStatus(HOST_ONLINE_WITH_PERFORMANCE_DATA);
}


// Function to check if the current time is within the allowed range - WLAN is only active between 6:30 and 0:30
bool isWithinTimeRange() {
  time_t nowSecs = time(nullptr);
  struct tm timeinfo;
  localtime_r(&nowSecs, &timeinfo);

  int currentHour = timeinfo.tm_hour;
  int currentMinute = timeinfo.tm_min;

  // Check if the current time is between 6:05 and 0:25
  if ((currentHour > 6 || (currentHour == 6 && currentMinute >= 5)) &&
      (currentHour < 24 || (currentHour == 0 && currentMinute <= 25))) {
    return true;
  }
  return false;
}

// Arduino loop function
void loop() {

  // Calculate uptime
  time_t nowSecs = time(nullptr);
  uptime = nowSecs - startTime;


  // Update the clock every hour if connected to WiFi
  if (WiFi.status() == WL_CONNECTED && millis() - clockUpdateMillis >= CLOCK_UPDATE_INTERVAL) {
    setClock();
    clockUpdateMillis = millis();
  }

  // Skip the loop if the current time is outside the allowed range
  if (!isWithinTimeRange()) {
    Serial.println("Outside of allowed time range, skipping remaining loop");
    delay(60 * 1000L); // Wait for a minute before checking again
    return;
  }

  int switchState = digitalRead(SWITCH_PIN);  // Read the switch state

  if (switchState != lastState ) {  // send status if the switch state changes
    sendSwitchStatus(switchState);
    previousMillis = millis();
  }   

  // Send status periodically if there is no change in switch state
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= INTERVAL) {
    // Save the last time
    previousMillis = currentMillis;
   
    if (switchState == lastState) {
      sendSwitchStatus(switchState);
    }
  }

  lastState = switchState;
  delay(50);
}

