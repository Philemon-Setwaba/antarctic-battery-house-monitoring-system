/*
  ============================================================
  ESP32-S3 LoRa Remote Unit + Real Telemetry System
  ============================================================

  COMBINED FEATURES:
  ------------------
  - LoRa telemetry transmission
  - LoRa command reception
  - ALIVE / ALIVE_ACK handling
  - DS18B20 temperature monitoring
  - Heater control
  - INA219 voltage/current monitoring
  - Bias-corrected coulomb counting SOC
  - EH/Battery automatic switching
  - External interrupt handling
  - Real telemetry integration

  TELEMETRY FORMAT:
  -----------------
  ID,powerMode,heaterStatus,temp_inside,temp_outside,voltage,SOC

  EXAMPLE:
  --------
  5,NORMAL,1,26.5,18.2,12.4,87.2

  ============================================================
*/

#include <SPI.h>
#include <LoRa.h>

#include <Wire.h>
#include <Adafruit_INA219.h>

#include <OneWire.h>
#include <DallasTemperature.h>

// ============================================================
// LORA PINS
// ============================================================

#define LORA_SCK   35
#define LORA_MISO  36
#define LORA_MOSI  37
#define SS_PIN     38
#define RST_PIN    39
#define DIO0_PIN   40

// ============================================================
// TELEMETRY HARDWARE
// ============================================================

#define ONE_WIRE_BUS 4
#define MOSFET_PIN   5

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

Adafruit_INA219 ina219;

// ============================================================
// EH / BATTERY SWITCHING
// ============================================================

const int EH_READY = 16;
const int EH_PWR   = 14;
const int BAT_PWR  = 13;

// ============================================================
// TIMING SETTINGS
// ============================================================

const unsigned long COMMAND_LISTEN_TIME = 9000;
const unsigned long LOOP_DELAY_TIME     = 4000;
const unsigned long ALIVE_ACK_TIMEOUT   = 5000;
const unsigned long RX_OFFLINE_WAIT     = 60000;
const unsigned long LORA_TX_DELAY       = 100;
const unsigned long PACKET_POLL_DELAY   = 10;
const unsigned long PACKET_READ_TIMEOUT = 100;

const int ALIVE_CHECK_PACKET_COUNT = 10;

// EH timer
const unsigned long timeoutPeriod = 5000UL;

// ============================================================
// BATTERY PARAMETERS
// ============================================================

const float BATTERY_CAPACITY_mAh = 1500.0;

float SOC = 100.0;
float currentBias_mA = 0.0;

unsigned long previousMillis = 0;

// ============================================================
// SYSTEM STATE
// ============================================================

int telemetryID = 0;

bool heaterOn = false;
bool ignoreHeaterToggle = true; // Override pearl code (Heating)
String powerMode = "NORMAL";

bool systemShutdown = false;

int telemetryCounter = 0;

bool aliveCheckPending = false;

// ============================================================
// TELEMETRY VARIABLES
// ============================================================

float temp_inside = 0.0;
float temp_outside = 0.0;
float voltage = 0.0;
float state_of_charge = 100.0;

// ============================================================
// COMMAND VARIABLES
// ============================================================

int receivedCommandID = 0;

String receivedCommand = "";

// ============================================================
// INTERRUPT VARIABLES
// ============================================================

volatile bool timerStarted = false;

volatile unsigned long timerStartMillis = 0;

// ============================================================
// INTERRUPT SERVICE ROUTINE
// ============================================================

void IRAM_ATTR handleInterrupt()
{
  if (digitalRead(EH_READY) == LOW)
  {
    timerStarted = true;
    timerStartMillis = millis();
  }
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);

  delay(1000);

  // ==========================================================
  // I2C
  // ==========================================================

  Wire.begin(8, 9);

  // ==========================================================
  // GPIO
  // ==========================================================

  pinMode(MOSFET_PIN, OUTPUT);

  digitalWrite(MOSFET_PIN, LOW);

  pinMode(EH_READY, INPUT_PULLUP);

  pinMode(EH_PWR, OUTPUT);

  pinMode(BAT_PWR, OUTPUT);

  digitalWrite(EH_PWR, LOW);

  digitalWrite(BAT_PWR, LOW);

  // ==========================================================
  // TEMPERATURE SENSORS
  // ==========================================================

  sensors.begin();

  Serial.print("Temperature sensors found: ");

  Serial.println(sensors.getDeviceCount());

  // ==========================================================
  // INA219
  // ==========================================================

  if (!ina219.begin())
  {
    Serial.println("Failed to find INA219");

    while (1);
  }

  // ==========================================================
  // INTERRUPT
  // ==========================================================

  attachInterrupt(
    digitalPinToInterrupt(EH_READY),
    handleInterrupt,
    FALLING
  );

  previousMillis = millis();

  // ==========================================================
  // LORA
  // ==========================================================

  initializeLoRa();

  printStartupInfo();

  waitForAliveAck();
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{
  if (aliveCheckPending)
  {
    waitForAliveAck();

    telemetryCounter = 0;

    aliveCheckPending = false;
  }

  else if (!systemShutdown)
  {
    sendTelemetry();

    telemetryCounter++;
  }

  listenForCommands(COMMAND_LISTEN_TIME);

  if (telemetryCounter >= ALIVE_CHECK_PACKET_COUNT)
  {
    aliveCheckPending = true;
  }

  delay(LOOP_DELAY_TIME);
}

// ============================================================
// UPDATE REAL TELEMETRY
// ============================================================

void updateTelemetryData()
{
  // ==========================================================
  // TEMPERATURES
  // ==========================================================

  sensors.requestTemperatures();

  float batteryTemp =
    sensors.getTempCByIndex(1);

  float ambientTemp =
    sensors.getTempCByIndex(0);

  // Map to telemetry packet format
  temp_inside = batteryTemp;

  temp_outside = ambientTemp;

  // ==========================================================
  // HEATER CONTROL
  // ==========================================================
if (ignoreHeaterToggle){
  if (batteryTemp < 26)
  {
    digitalWrite(MOSFET_PIN, HIGH);

    heaterOn = true;
  }
  else if (batteryTemp > 27)
  {
    digitalWrite(MOSFET_PIN, LOW);

    heaterOn = false;
  }
}else{
   heaterOn = false;
   digitalWrite(MOSFET_PIN, LOW);
}
  // ==========================================================
  // INA219 MEASUREMENTS
  // ==========================================================

  float rawCurrent_mA =
    ina219.getCurrent_mA();

  voltage =
    ina219.getBusVoltage_V();

  // ==========================================================
  // BIAS CORRECTION
  // ==========================================================

  if (abs(rawCurrent_mA) < 1.0)
  {
    currentBias_mA =
      0.99 * currentBias_mA +
      0.01 * rawCurrent_mA;
  }

  float current_mA =
    rawCurrent_mA - currentBias_mA;

  // ==========================================================
  // COULOMB COUNTING
  // ==========================================================

  unsigned long currentMillis =
    millis();

  float dt_hours =
    (currentMillis - previousMillis)
    / 3600000.0;

  previousMillis = currentMillis;

  float delta_mAh =
    current_mA * dt_hours;

  SOC -=
    (delta_mAh / BATTERY_CAPACITY_mAh)
    * 100.0;

  if (SOC > 100.0) SOC = 100.0;

  if (SOC < 0.0) SOC = 0.0;

  state_of_charge = SOC;

  if (SOC<20){
    powerMode = "LOW";
  }
  // ==========================================================
  // EH / BATTERY SWITCHING
  // ==========================================================
  bool EH_READYState =
    digitalRead(EH_READY);

  if (EH_READYState == HIGH)
  {
    timerStarted = false;
    digitalWrite(EH_PWR, HIGH);
    digitalWrite(BAT_PWR, LOW);
  }

  if (timerStarted)
  {
    unsigned long elapsed =
      millis() - timerStartMillis;

    if (elapsed >= timeoutPeriod)
    {
      if (digitalRead(EH_READY) == LOW && powerMode != "LOW")
      {
        digitalWrite(EH_PWR, LOW);
        digitalWrite(BAT_PWR, HIGH);
        Serial.println("NORMAL MODE");
      }
      else{
         Serial.println("LOW MODE");
      }
      timerStarted = false;
    }
  }

  // ==========================================================
  // LOCAL TELEMETRY DEBUG
  // ==========================================================

  Serial.println("========== REAL TELEMETRY ==========");

  Serial.print("Battery Temp: ");
  Serial.println(temp_inside);

  Serial.print("Ambient Temp: ");
  Serial.println(temp_outside);

  Serial.print("Voltage: ");
  Serial.println(voltage);

  Serial.print("SOC: ");
  Serial.println(state_of_charge);

  Serial.print("Current Bias: ");
  Serial.println(currentBias_mA);

  Serial.print("EH_PWR: ");
  Serial.println(digitalRead(EH_PWR));

  Serial.print("BAT_PWR: ");
  Serial.println(digitalRead(BAT_PWR));
  Serial.println("====================================");
  Serial.print("Received : ");
  Serial.println(receivedCommand);
}

// ============================================================
// LORA INITIALIZATION
// ============================================================

void initializeLoRa()
{
  SPI.begin(
    LORA_SCK,
    LORA_MISO,
    LORA_MOSI,
    SS_PIN
  );

  LoRa.setPins(
    SS_PIN,
    RST_PIN,
    DIO0_PIN
  );

  if (!LoRa.begin(433E6))
  {
    Serial.println("LoRa initialization failed!");

    while (1);
  }

  configureLoRa();
}

void configureLoRa()
{
  LoRa.setSpreadingFactor(12);

  LoRa.setSignalBandwidth(125E3);

  LoRa.setCodingRate4(8);

  LoRa.setTxPower(14);

  LoRa.setSyncWord(0x12);
}

// ============================================================
// STARTUP INFO
// ============================================================

void printStartupInfo()
{
  Serial.println("=================================");
  Serial.println("ESP32-S3 TX - Real Telemetry");
  Serial.println("=================================");

  Serial.println("Telemetry Format:");
  Serial.println("ID,powerMode,heaterStatus,temp_inside,temp_outside,voltage,SOC");

  Serial.println("Command Format:");
  Serial.println("ID,COMMAND");

  Serial.println("LoRa Ready");
}

// ============================================================
// ALIVE CHECK
// ============================================================

void waitForAliveAck()
{
  bool ackReceived = false;

  while (!ackReceived)
  {
    Serial.println("[SEND] ALIVE?");

    LoRa.beginPacket();

    LoRa.print("ALIVE?");

    LoRa.endPacket();

    delay(LORA_TX_DELAY);

    unsigned long start = millis();

    while (millis() - start < ALIVE_ACK_TIMEOUT)
    {
      int packetSize = LoRa.parsePacket();

      if (packetSize)
      {
        String packet = readLoRaPacket();

        if (packet == "ALIVE_ACK")
        {
          Serial.println("[RECV] ALIVE_ACK");

          ackReceived = true;

          break;
        }

        if (isCommandPacket(packet))
        {
          bool validCommand =
            processCommandPacket(packet);

          if (validCommand)
          {
            ackReceived = true;

            break;
          }
        }
      }

      delay(PACKET_POLL_DELAY);
    }

    if (!ackReceived)
    {
      Serial.println("[WAIT] RX OFFLINE");

      delay(RX_OFFLINE_WAIT);
    }
  }
}

// ============================================================
// TELEMETRY
// ============================================================

String buildTelemetryPacket()
{
  String packet =
    String(telemetryID) + "," +
    powerMode + "," +
    (heaterOn ? "1" : "0") + "," +
    String(temp_inside, 2) + "," +
    String(temp_outside, 2) + "," +
    String(voltage, 2) + "," +
    String(state_of_charge, 2);

  return packet;
}

void sendTelemetry()
{
  // UPDATE REAL TELEMETRY FIRST
  updateTelemetryData();

  String packet =
    buildTelemetryPacket();

  // PRINT SENT TELEMETRY
  Serial.print("[SEND] ");

  Serial.print(millis() / 1000);

  Serial.print("s - ");

  Serial.println(packet);

  // SEND OVER LORA
  LoRa.beginPacket();

  LoRa.print(packet);

  LoRa.endPacket();

  delay(LORA_TX_DELAY);
}

// ============================================================
// COMMAND LISTENING
// ============================================================

void listenForCommands(
  unsigned long timeout
)
{
  unsigned long start = millis();

  while (
    millis() - start < timeout
  )
  {
    int packetSize =
      LoRa.parsePacket();

    if (packetSize)
    {
      String packet =
        readLoRaPacket();

      if (packet == "ALIVE_ACK")
      {
        Serial.println(
          "[IGNORE] Stale ALIVE_ACK"
        );

        continue;
      }

      if (isCommandPacket(packet))
      {
        bool validCommand =
          processCommandPacket(packet);

        if (validCommand)
        {
          break;
        }
      }

      delay(PACKET_POLL_DELAY);
    }

    delay(PACKET_POLL_DELAY);
  }
}

// ============================================================
// READ PACKET
// ============================================================

String readLoRaPacket()
{
  String packet = "";

  unsigned long readStart =
    millis();

  while (
    LoRa.available() &&
    (millis() - readStart)
    < PACKET_READ_TIMEOUT
  )
  {
    packet +=
      (char)LoRa.read();
  }

  return packet;
}

// ============================================================
// COMMAND VALIDATION
// ============================================================

bool isCommandPacket(String packet)
{
  int commaPos =
    packet.indexOf(',');

  if (commaPos <= 0)
  {
    return false;
  }

  String idStr =
    packet.substring(0, commaPos);

  String command =
    packet.substring(commaPos + 1);

  for (
    int i = 0;
    i < idStr.length();
    i++
  )
  {
    if (!isDigit(idStr[i]))
    {
      return false;
    }
  }

  return isValidCommand(command);
}

bool isValidCommand(String command)
{
  if (command == "HEATER:ON") return true;

  if (command == "HEATER:OFF") return true;

  if (command == "MODE:LOW") return true;

  if (command == "MODE:NORMAL") return true;

  if (command == "SYSTEM:SHUTDOWN") return true;

  return false;
}

// ============================================================
// COMMAND PROCESSING
// ============================================================

bool processCommandPacket(String packet)
{
  Serial.print("[RECV] ");

  Serial.print(millis() / 1000);

  Serial.print("s - ");

  Serial.println(packet);

  int commaPos =
    packet.indexOf(',');

  receivedCommandID =
    packet.substring(0, commaPos).toInt();

  receivedCommand =
    packet.substring(commaPos + 1);

  Serial.print("[ID] ");

  Serial.println(receivedCommandID);

  Serial.print("[COMMAND] ");

  Serial.println(receivedCommand);

  bool success =
    executeCommand(receivedCommand);

  if (success)
  {
    telemetryID =
      receivedCommandID;

    Serial.print(
      "[ACK] Telemetry ID updated to: "
    );

    Serial.println(telemetryID);

    telemetryCounter = 0;

    aliveCheckPending = false;

    if (systemShutdown)
    {
      Serial.println(
        "[SHUTDOWN] Sending final ACK telemetry"
      );

      sendTelemetry();
    }

    return true;
  }

  return false;
}

// ============================================================
// COMMAND EXECUTION
// ============================================================

bool executeCommand(String command)
{
  if (command == "HEATER:ON")
  {
    ignoreHeaterToggle = true;
    
    //digitalWrite(MOSFET_PIN, HIGH);

    Serial.println("[ACTION] Heater ON");

    return true;
  }

  if (command == "HEATER:OFF")
  {
    ignoreHeaterToggle = false;

    //digitalWrite(MOSFET_PIN, LOW);
    
    Serial.println("[ACTION] Heater OFF");

    return true;
  }

  if (command == "MODE:LOW")
  {
    powerMode = "LOW";

    //heaterOn = false;
   // digitalWrite(MOSFET_PIN, LOW);
    Serial.println("[ACTION] Mode LOW");
    return true;
  }

  if (command == "MODE:NORMAL")
  {
    powerMode = "NORMAL";

    systemShutdown = false;

    Serial.println("[ACTION] Mode NORMAL");

    return true;
  }

  if (command == "SYSTEM:SHUTDOWN")
  {
    heaterOn = false;

    //digitalWrite(MOSFET_PIN, LOW);

    powerMode = "LOW";

    systemShutdown = true;
    digitalWrite(BAT_PWR, LOW);
    digitalWrite(EH_PWR, LOW);
    Serial.println("[ACTION] SYSTEM SHUTDOWN");

    return true;
  }

  return false;
}