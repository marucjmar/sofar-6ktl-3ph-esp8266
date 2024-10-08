#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

const int RELAY_PIN = 12; // D6 on board

// network credentials
const char* ssid = "---";
const char* password = "---";

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;

const long utcOffsetInSeconds = 3600 * 2;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

void onWifiConnect(const WiFiEventStationModeGotIP &event) {
  Serial.println("Connected to Wi-Fi sucessfully.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected &event) {
  disableHeater();
  Serial.println("Disconnected from Wi-Fi, trying to connect...");
  WiFi.disconnect();
  WiFi.begin(ssid, password);
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  pinMode(LED_BUILTIN, OUTPUT);


  // Register event handlers
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  initWiFi();
  Serial.print("RRSI: ");
  Serial.println(WiFi.RSSI());
}

const int measurementInterval = 20 * 1000;                           // Czestotliwosc probkowania
const int minPower = 4000;                                           // Minimalna moc w watach po przekroczeniu której ma się uruchamiać grzalka
const int maxPower = 6300;                                           // Minimalna moc w watach po przekroczeniu której ma się uruchamiać grzalka
const String serverStatusPath = "http://192.168.68.110/status.html"; // Adres url do falownika
int lastProbeTime = 0;

void loop() {
  timeClient.update();
  
  if ((millis() - lastProbeTime) > measurementInterval || lastProbeTime == 0) {
    digitalWrite(LED_BUILTIN, LOW);

    // Check WiFi connection status
    if (WiFi.status() == WL_CONNECTED) {
      int currentPower = getCurrentPower();

      if (currentPower >= minPower && currentPower <= maxPower && timeClient.getHours() >= 11 && timeClient.getHours() <= 13) {
        enableHeater();
      } else {
        disableHeater();
      }
    } else {
      disableHeater();
      Serial.println("WiFi Disconnected");
    }

    digitalWrite(LED_BUILTIN, HIGH);
    lastProbeTime = millis();
  }
}

int getCurrentPower() {
  const int retries = 3;
  int currentPower = -1;

  for (int i = 0; i <= retries; i++) {
    currentPower = doGetCurrentPower();

    if (currentPower >= 0) {
      break;
    }
  }

  return currentPower;
}

int doGetCurrentPower() {
  WiFiClient client;
  HTTPClient http;

  http.begin(client, serverStatusPath.c_str());
  http.addHeader("Authorization", "Basic YWRtaW46YWRtaW4="); // basic auth: admin/admin

  const int httpResponseCode = http.GET();
  int value = -1;

  if (httpResponseCode > 0) {
    const String payload = http.getString();
    value = extractCurrentPower(payload);
  }

  http.end();

  return value;
}

int extractCurrentPower(String str) {
  const String searchString = "var webdata_now_p = ";
  const int startIndex = str.indexOf(searchString) + searchString.length();
  const int endIndex = str.indexOf(';', startIndex);
  const String val = str.substring(startIndex + 1, endIndex - 1);

  if (isDigit(val.charAt(0))) { // wartość moze byc zwrocona jako ---
    return val.toInt() * 10; // fix na Moc w Watach, strona podaje wartość ze źle przesuniętym przecinkiem
  }

  return -1;
}

void enableHeater() {
  Serial.println("Zalacz grzalke");
  digitalWrite(RELAY_PIN, HIGH);
}

void disableHeater() {
  Serial.println("Wylacz grzalke");
  digitalWrite(RELAY_PIN, LOW);
}
