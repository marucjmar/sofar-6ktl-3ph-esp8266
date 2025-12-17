// Doc's
// SOFAR-G3 Registers
// https://docs.google.com/spreadsheets/d/18OJS0J_MbOt52aF8X8rI1uK5xMWb2dtK
// Solarman v5 protocol
// https://pysolarmanv5.readthedocs.io/en/stable/solarmanv5_protocol.html

#include <ESP8266WiFi.h>

// PIN's
const int RELAY_PIN = 12; // D6 on board

// Network credentials
const char* WIFI_SSID = "---";
const char* WIFI_PASSWORD = "---";

// Inverter LSW3 Modbus config
const char* INVERTER_MODBUS_IP = "192.168.68.110";                    // Rezerwacja adresu IP na routerze dla LSW3
const uint16_t INVERTER_MODBUS_PORT = 8899;

// Heater algo consts
const int MEASUREMENT_INTERVAL = 20 * 1000;                           // Częstotliwość próbkowania
const int HEATER_POWER = 2000;                                        // Moc grzałki w Watach
const int POWER_DRIFT = 100;                                          // Zapas energii na wyjściu do sieci w Watach

void setup() {
  Serial.begin(115200);

  // PIN'S bindings
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  digitalWrite(RELAY_PIN, LOW);

  initWiFi();
}

// Loop Variables
int lastProbeTime = 0;

void loop() {
  if ((millis() - lastProbeTime) > MEASUREMENT_INTERVAL || lastProbeTime == 0) {
    enableBoardLight();

    if (WiFi.status() == WL_CONNECTED) {
      const int currentPCC = getCurrentPCCPower();
      
      Serial.print("Wartość PCC: ");
      Serial.print(currentPCC);
      Serial.println("W");

      if (
          // Gdy grzałka jest wyłączona oraz wpompowana energia do sieci przekracza moc grzałki (z ustawionym zapasem)
          (!heaterIsEnabled() && currentPCC - POWER_DRIFT >= HEATER_POWER) ||
          // Gdy grzałka jest włączona oraz wpompowana jest energia do sieci z zapasem (obsłuzenie sytuacji aby włączona grzałka się wyłączyła gdy zaczynamy pobierać energię z sieci)
          (heaterIsEnabled() && currentPCC >= POWER_DRIFT)
        ) {
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
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
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
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}
//----- End WiFi Section ------

// -----  Get Registers Values Section -----
// Gdy wpompujemy energię do sieci to wartość dodatnia, gdy pobieramy prąd z sieci wartość ujemna
// Wartość zwracana w Watach
int16_t getCurrentPCCPower() {
  // modbus.read_holding_registers(0x0488, 2)
  uint8_t frame[] = {
    0xA5, 0x17, 0x00, 0x10, 0x45, 0xC6, 0x00,
    0xB5, 0x43, 0xC3, 0x8E, 0x02, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03,
    0x04, 0x88, 0x00, 0x02, 0x45, 0x11, 0x65, 0x15
  };

  // Wartość rejestru zapisana w signed int - I16 
  return ((int16_t)getFrameRegisterValue(frame, sizeof(frame))) * 10;
}
// -----  End Registers Values Section -----

// ----- PIN's functions -----------
void enableHeater() {
  Serial.println("Zalacz grzalke");
  digitalWrite(RELAY_PIN, HIGH);
}

void disableHeater() {
  Serial.println("Wylacz grzalke");
  digitalWrite(RELAY_PIN, LOW);
}

bool heaterIsEnabled() {
  return digitalRead(RELAY_PIN) == HIGH;
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

uint16_t getFrameRegisterValue(uint8_t frame[], size_t lenFrame) {
  WiFiClient client;
  int startTime = millis();

  if (!client.connect(INVERTER_MODBUS_IP, INVERTER_MODBUS_PORT)) {
    Serial.println("Blad polaczenia TCP");
    return 0;
  }

  client.write(frame, lenFrame);
  client.flush();

  int timeout = 5000;
  int awaitTime = 0;
  int delayTime = 200;

  // Waiting to response
  while (!client.available() && awaitTime <= timeout) {
    delay(delayTime);
    awaitTime += delayTime;
  }

  uint16_t value = 0;

  while (client.available()) {
    uint8_t responseFrame[256];
    int responseFrameLength = client.read(responseFrame, sizeof(responseFrame));
    int endTime = millis();
    
    Serial.print("Ramka odpowiedzi: ");
    for (int i = 0; i < responseFrameLength; i++) {
        Serial.printf("%02X ", responseFrame[i]);
    }
    Serial.println();

    Serial.print("Czas zapytania o ramkę: ");
    Serial.print(endTime - startTime);
    Serial.println("ms");

    Serial.print("Otrzymano bajtów: ");
    Serial.println(responseFrameLength);
    value = parseModbusRegister(responseFrame, responseFrameLength, 0);
  }

  client.stop();

  return value;
}

// Funkcja dekodująca ramkę modbus i zwracająca wartość rejestru o podanym numerze
uint16_t parseModbusRegister(uint8_t* frame, size_t length, uint8_t registerNumber) {
  // Znajdź funkcję Modbus (0x03 lub 0x04)
  int modbusStartIndex = 25;
  int funcIndex = modbusStartIndex + 1;

  if (frame[modbusStartIndex] != 0x01 && (frame[modbusStartIndex + 1] != 0x03 || frame[modbusStartIndex + 1] != 0x04)) {
    Serial.println("Nie znaleziono funkcji Modbus w ramce");
    return 0; // błąd
  }

  uint8_t byteCount = frame[funcIndex + 1];
  int totalRegisters = byteCount / 2;

  if (registerNumber >= totalRegisters) {
    Serial.println("Nieprawidłowy numer rejestru");
    return 0; // błąd
  }

  int dataStart = funcIndex + 2;
  int index = dataStart + registerNumber * 2;
  uint16_t regValue = (frame[index] << 8) | frame[index + 1]; // MSB | LSB

  return regValue;
}
