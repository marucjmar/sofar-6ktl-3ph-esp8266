#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <time.h>
#include <WiFiUdp.h>

// PIN's
const int RELAY_PIN = 12; // D6 on board

// network credentials
const char* ssid = "----";
const char* password = "----";

// Program Consts
const int measurementInterval = 20 * 1000;                           // Czestotliwosc probkowania
const int minPower = 4000;                                           // Minimalna moc w watach po przekroczeniu której ma się uruchamiać grzalka
const int maxPower = 6300;                                           // Maksymalna moc w watach po przekroczeniu której ma się uruchamiać grzalka
const int startHour = 11;                                            // Godzina od ktorej ma byc wlaczona grzalka
const int endHour = 13;                                              // Godzina do ktorej ma byc wlaczona grzalka
const String serverStatusPath = "http://192.168.68.110/status.html"; // Adres url do falownika

void setup() {
  Serial.begin(115200);

  // Time config
  configTime("CET-1CEST,M3.5.0/2,M10.5.0/3", "pool.ntp.org", "time.nist.gov");

  // PIN'S bindings
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  digitalWrite(RELAY_PIN, LOW);

  initWiFi();
}

// Loop Variables
int lastProbeTime = 0;

void loop() {
  if ((millis() - lastProbeTime) > measurementInterval || lastProbeTime == 0) {
    enableBoardLight();

    if (WiFi.status() == WL_CONNECTED) {
      const int currentPower = getCurrentPower();
      const time_t now = time(nullptr);
      const struct tm* timeinfo = localtime(&now);
      const int currentHour = timeinfo->tm_hour;

      if (currentPower >= minPower && currentPower <= maxPower && currentHour >= startHour && currentHour <= endHour) {
        enableHeater();
      } else {
        disableHeater();
      }
    } else {
      disableHeater();
      Serial.println("WiFi Disconnected");
    }

    disableBoardLight();
    lastProbeTime = millis();
  }
}

// -----  WiFi Section -----
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;

void initWiFi() {
  // Register event handlers
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.print("RRSI: ");
  Serial.println(WiFi.RSSI());
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
//----- End WiFi Section ------

// -----  Get Power Section -----
int getCurrentPower() {
  const int retries = 3;
  int currentPower = -1;

  for (int i = 0; i < retries; i++) {
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
  http.addHeader("Authorization", "Basic YWRtaW46YWRtaW4="); // Basic auth: admin/admin

  const int httpResponseCode = http.GET();
  int value = -1;

  if (httpResponseCode > 0) {
    const String payload = http.getString();
    value = extractCurrentPower(payload);
  }

  http.end();

  return value;
}

int extractCurrentPower(String payload) {
  const String searchString = "var webdata_now_p = ";
  const int startIndex = payload.indexOf(searchString) + searchString.length();
  const int endIndex = payload.indexOf(';', startIndex);
  const String extractedVal = payload.substring(startIndex + 1, endIndex - 1);

  if (isDigit(extractedVal.charAt(0))) { // wartość moze byc zwrocona jako ---
    return extractedVal.toInt() * 10; // fix na Moc w Watach, strona podaje wartość ze źle przesuniętym przecinkiem
  }

  return -1;
}
// -----  End Get Power Section -----

// ----- PIN's functions -----------
void enableHeater() {
  Serial.println("Zalacz grzalke");
  digitalWrite(RELAY_PIN, HIGH);
}

void disableHeater() {
  Serial.println("Wylacz grzalke");
  digitalWrite(RELAY_PIN, LOW);
}

void enableBoardLight() {
  Serial.println("Zalacz diode");
  digitalWrite(LED_BUILTIN, LOW);
}

void disableBoardLight() {
  Serial.println("Wylacz diode");
  digitalWrite(LED_BUILTIN, HIGH);
}
// ----- END PIN's functions ---------

