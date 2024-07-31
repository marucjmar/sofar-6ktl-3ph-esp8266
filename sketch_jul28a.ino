#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

#define RELAY_PIN 4

// network credentials
const char* ssid = "---";
const char* password = "---";

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;

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
  Serial.println("Disconnected from Wi-Fi, trying to connect...");
  WiFi.disconnect();
  WiFi.begin(ssid, password);
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  // Register event handlers
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  initWiFi();
  Serial.print("RRSI: ");
  Serial.println(WiFi.RSSI());
}

const int measurementInterval = 30 * 1000;                           // Czestotliwosc probkowania
const int minPower = 4000;                                           // Minimalna moc w watach po przekroczeniu której ma się uruchamiać grzalka
const String serverStatusPath = "http://192.168.2.100/status.html"; // Adres url do falownika
int lastProbeTime = 0;

void loop() {
  if ((millis() - lastProbeTime) > measurementInterval) {
    digitalWrite(LED_BUILTIN, HIGH);

    // Check WiFi connection status
    if (WiFi.status() == WL_CONNECTED) {

      int currentPower = getCurrentPower();

      if (currentPower >= minPower) {
        Serial.println("Zalacz grzalke");
        digitalWrite(RELAY_PIN, HIGH);
      } else {
        Serial.println("Wylacz grzalke");
        digitalWrite(RELAY_PIN, LOW);
      }
    } else {
      digitalWrite(RELAY_PIN, LOW);
      Serial.println("WiFi Disconnected");
    }

    digitalWrite(LED_BUILTIN, LOW);
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

  if (httpResponseCode > 0) {
    const String payload = http.getString();
    const int value = extractCurrentPower(payload);

    http.end();

    return value;
  }

  http.end();

  return -1;
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
