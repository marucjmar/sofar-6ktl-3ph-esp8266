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
const int POWER_HYSTERESIS = 100;                                     // Zapas energii na wyjściu do sieci w Watach

// -----  Types Section ----
struct PCCReadResult {
  bool success;
  int32_t powerWatts;
};

struct ModbusReadResult {
  bool success;
  uint16_t value;
};

class Probe {
public:
  explicit Probe(unsigned long intervalMs) : interval(intervalMs), lastProbeTime(0) {}

  bool shouldDo() const {
    return lastProbeTime == 0 || (millis() - lastProbeTime) > interval;
  }

  void checked() {
    lastProbeTime = millis();
  }

private:
  const unsigned long interval;
  unsigned long lastProbeTime;
};
// -----  End Types Section ----

void setup() {
  Serial.begin(115200);

  // PIN'S bindings
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  digitalWrite(RELAY_PIN, LOW);

  initWiFi();
}

// Loop Variables
Probe heaterProbe(MEASUREMENT_INTERVAL);

void loop() {
  if (!heaterProbe.shouldDo()) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    disableHeater();
    Serial.println("WiFi Disconnected");

    return;
  }

  enableBoardLight();

  PCCReadResult pccResult = getCurrentPCCPower();

  if (!pccResult.success) {
    Serial.println("Blad odczytu z falownika - wylaczam grzaleczke");
    disableHeater();
    disableBoardLight();

    heaterProbe.checked();
    return;
  }

  const int32_t currentPCC = pccResult.powerWatts;
  
  Serial.print("Wartość PCC: ");
  Serial.print(currentPCC);
  Serial.println("W");

  if (shouldHeaterBeEnabled(currentPCC)) {
    enableHeater();
  } else {
    disableHeater();
  }

  disableBoardLight();
  heaterProbe.checked();
}

bool shouldHeaterBeEnabled(int32_t currentPCC) {
  const bool heaterOn = heaterIsEnabled();

  // Grzałka wyłączona: włączamy, gdy wprowadzana energia do sieci
  // przekracza moc grzałki (z zapasem)
  if (!heaterOn) {
    return currentPCC - POWER_HYSTERESIS >= HEATER_POWER;
  }

  // Grzałka włączona: wyłączamy, gdy zaczynamy pobierać energię z sieci
  // (zostawiamy włączoną tylko, gdy wciąż oddajemy energię z zapasem)
  return currentPCC >= POWER_HYSTERESIS;
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

// -----  Get Registers Values Section ----
// Gdy wpompujemy energię do sieci to wartość dodatnia, gdy pobieramy prąd z sieci wartość ujemna
// Wartość zwracana w Watach
PCCReadResult getCurrentPCCPower() {
  // modbus.read_holding_registers(0x0488, 2)
  const uint8_t frame[] = {
    0xA5, 0x17, 0x00, 0x10, 0x45, 0xC6, 0x00,
    0xB5, 0x43, 0xC3, 0x8E, 0x02, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03,
    0x04, 0x88, 0x00, 0x02, 0x45, 0x11, 0x65, 0x15
  };

  ModbusReadResult result = getFrameRegisterValue(frame, sizeof(frame));

  if (!result.success) {
    return { false, 0 };
  }

  return { true, ((int16_t)result.value) * 10 };
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

// ----- Modbus functions -----------
ModbusReadResult getFrameRegisterValue(const uint8_t frame[], const size_t lenFrame) {
  WiFiClient client;
  const unsigned long startTime = millis();

  if (!client.connect(INVERTER_MODBUS_IP, INVERTER_MODBUS_PORT)) {
    Serial.println("Blad polaczenia TCP");
    return { false, 0 };
  }

  client.write(frame, lenFrame);
  client.flush();

  const int timeout = 5000;
  const int delayTime = 200;
  int awaitTime = 0;

  // Waiting to response
  while (!client.available() && awaitTime <= timeout) {
    delay(delayTime);
    awaitTime += delayTime;
  }

  ModbusReadResult result = { false, 0 };

  while (client.available()) {
    uint8_t responseFrame[256];
    const int responseFrameLength = client.read(responseFrame, sizeof(responseFrame));

    if (responseFrameLength <= 0) {
      Serial.println("Blad odczytu odpowiedzi");
      continue;
    }

    const unsigned long endTime = millis();
    
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
    
    ModbusReadResult parsed = parseModbusRegister(responseFrame, responseFrameLength, 0);

    if (parsed.success) {
      result = parsed;
    } else {
      Serial.println("Blad parsowania ramki Modbus");
    }
  }

  client.stop();

  return result;
}

// Funkcja dekodująca ramkę modbus i zwracająca wartość rejestru o podanym numerze
ModbusReadResult parseModbusRegister(uint8_t* frame, size_t length, uint8_t registerNumber) {
  // Znajdź funkcję Modbus (0x03 lub 0x04)
  const int modbusStartIndex = 25;
  const int funcIndex = modbusStartIndex + 1;

  if (length < (size_t)(funcIndex + 2)) {
    Serial.println("Ramka za krótka");
    return { false, 0 };
  }

  const int operationId = frame[modbusStartIndex + 1];

  if (frame[modbusStartIndex] != 0x01 && (operationId != 0x03 && operationId != 0x04)) {
    Serial.println("Nie znaleziono funkcji Modbus w ramce");
    return { false, 0 };
  }

  const uint8_t byteCount = frame[funcIndex + 1];
  const int totalRegisters = byteCount / 2;

  if (registerNumber >= totalRegisters) {
    Serial.println("Nieprawidłowy numer rejestru");
    return { false, 0 };
  }

  const int dataStart = funcIndex + 2;
  const int index = dataStart + registerNumber * 2;

  if (length < (size_t)(index + 2)) {
    Serial.println("Ramka za krótka dla żądanego rejestru");
    return { false, 0 };
  }

  const uint16_t regValue = (frame[index] << 8) | frame[index + 1]; // MSB | LSB
  return { true, regValue };
}
// ----- End Modbus functions -----------
