#include <SPI.h>
#include <LoRa.h>

// ============================================
// LORA PINS
// ============================================

#define SS_PIN   10
#define RST_PIN  9
#define DIO0_PIN 2

// ============================================
// TIMING SETTINGS
// Change these values here only
// ============================================

// Target telemetry interval:
// COMMAND_LISTEN_TIME + LOOP_DELAY_TIME ≈ telemetry sending interval
const unsigned long COMMAND_LISTEN_TIME = 26000;   // 26 seconds
const unsigned long LOOP_DELAY_TIME = 4000;        // 4 seconds

// Startup and alive-check timing
const unsigned long ALIVE_ACK_TIMEOUT = 5000;      // 5 seconds
const unsigned long RX_OFFLINE_WAIT = 60000;       // 60 seconds

// LoRa transmit settling delay
const unsigned long LORA_TX_DELAY = 100;           // 0.1 seconds

// Small polling delay while listening
const unsigned long PACKET_POLL_DELAY = 10;        // 0.01 seconds

// Packet read timeout
const unsigned long PACKET_READ_TIMEOUT = 100;     // 0.1 seconds

// Sensor update interval
const unsigned long SENSOR_UPDATE_INTERVAL = 10000; // 10 seconds

// After this number of telemetry packets,
// the next normal send time is used for an ALIVE check
const int ALIVE_CHECK_PACKET_COUNT = 10;

// ============================================
// SYSTEM STATE
// ============================================

int telemetryID = 0;

bool heaterOn = false;

String powerMode = "NORMAL";

bool systemShutdown = false;

// Counts consecutive telemetry packets
int telemetryCounter = 0;

// After ALIVE_CHECK_PACKET_COUNT telemetry packets,
// next send must be ALIVE
bool aliveCheckPending = false;

// Sensor values
float temp_inside = 22.5;
float temp_outside = 18.3;
float voltage = 12.8;
float state_of_charge = 85.0;

// ============================================
// SETUP
// ============================================

void setup() {

  Serial.begin(9600);

  while (!Serial);

  initializeLoRa();

  printStartupInfo();

  // ==========================================
  // STARTUP ALIVE CHECK
  // ==========================================

  waitForAliveAck();
}

// ============================================
// MAIN LOOP
// ============================================

void loop() {

  // ============================================
  // IF ALIVE CHECK IS DUE, SEND ALIVE INSTEAD
  // OF TELEMETRY AT THE NORMAL SEND TIME
  // ============================================

  if (aliveCheckPending) {

    waitForAliveAck();

    telemetryCounter = 0;

    aliveCheckPending = false;
  }

  // ============================================
  // NORMAL OPERATION
  // ============================================

  else if (!systemShutdown) {

    updateDummyData();

    sendTelemetry();

    telemetryCounter++;
  }

  // ============================================
  // ALWAYS LISTEN FOR COMMANDS
  // This is 26 s by default, so with the 4 s delay
  // telemetry is sent approximately every 30 s.
  // ============================================

  listenForCommands(COMMAND_LISTEN_TIME);

  // ============================================
  // AFTER ALIVE_CHECK_PACKET_COUNT TELEMETRY PACKETS,
  // DO NOT SEND ALIVE IMMEDIATELY.
  // MARK IT FOR NEXT NORMAL SEND TIME.
  // ============================================

  if (telemetryCounter >= ALIVE_CHECK_PACKET_COUNT) {

    aliveCheckPending = true;
  }

  delay(LOOP_DELAY_TIME);
}

// ============================================
// INITIALIZATION
// ============================================

void initializeLoRa() {

  LoRa.setPins(
    SS_PIN,
    RST_PIN,
    DIO0_PIN
  );

  if (!LoRa.begin(433E6)) {

    Serial.println(
      "LoRa initialization failed!"
    );

    while (1);
  }

  configureLoRa();
}

void configureLoRa() {

  LoRa.setSpreadingFactor(12);

  LoRa.setSignalBandwidth(125E3);

  LoRa.setCodingRate4(8);

  LoRa.setTxPower(14);

  LoRa.setSyncWord(0x12);
}

void printStartupInfo() {

  Serial.println(
    "================================="
  );

  Serial.println(
    "Arduino TX - ACK Based Telemetry"
  );

  Serial.println(
    "================================="
  );

  Serial.println(
    "Telemetry Format:"
  );

  Serial.println(
    "ID,powerMode,heaterStatus,temp_inside,temp_outside,voltage,SOC"
  );

  Serial.println("LoRa Ready");

  Serial.print("Telemetry interval target: ");

  Serial.print(
    (COMMAND_LISTEN_TIME + LOOP_DELAY_TIME) / 1000
  );

  Serial.println(" seconds");
}

// ============================================
// ALIVE CHECK
// ============================================

void waitForAliveAck() {

  bool ackReceived = false;

  while (!ackReceived) {

    Serial.println("[SEND] ALIVE?");

    LoRa.beginPacket();

    LoRa.print("ALIVE?");

    LoRa.endPacket();

    delay(LORA_TX_DELAY);

    unsigned long start = millis();

    while (millis() - start < ALIVE_ACK_TIMEOUT) {

      int packetSize = LoRa.parsePacket();

      if (packetSize) {

        String packet = readLoRaPacket();

        if (
          packet.length() == 0 ||
          packet.length() > 40
        ) {

          printGarbage(packet);

          delay(PACKET_POLL_DELAY);

          continue;
        }

        if (packet == "ALIVE_ACK") {

          Serial.println("[RECV] ALIVE_ACK");

          ackReceived = true;

          break;
        }

        // ============================================
        // FIXED BUG:
        // Do NOT accept any packet with a comma.
        //
        // Telemetry also has commas:
        // ID,LOW,0,temp,temp,voltage,SOC
        //
        // Only accept packets that match real command
        // format:
        // ID,HEATER:ON
        // ID,HEATER:OFF
        // ID,MODE:LOW
        // ID,MODE:NORMAL
        // ID,SYSTEM:SHUTDOWN
        // ============================================

        if (isCommandPacket(packet)) {

          bool validCommand =
            processCommandPacket(packet);

          if (validCommand) {

            ackReceived = true;

            break;
          }

        } else {

          printGarbage(packet);
        }
      }

      delay(PACKET_POLL_DELAY);
    }

    if (!ackReceived) {

      Serial.println("[WAIT] RX OFFLINE");

      delay(RX_OFFLINE_WAIT);
    }
  }
}

// ============================================
// SENSOR DATA
// ============================================

void updateDummyData() {

  static unsigned long lastUpdate = 0;

  if (
    millis() - lastUpdate < SENSOR_UPDATE_INTERVAL
  ) {

    return;
  }

  lastUpdate = millis();

  temp_inside =
    22.5 + random(-15, 15) / 10.0;

  temp_outside =
    18.3 + random(-20, 20) / 10.0;

  updateVoltage();

  updateSOC();
}

void updateVoltage() {

  if (heaterOn) {

    voltage =
      12.2 + random(-10, 10) / 100.0;

  } else {

    voltage =
      12.8 + random(-10, 10) / 100.0;
  }
}

void updateSOC() {

  if (voltage >= 12.6) {

    state_of_charge =
      95 + random(-5, 5);

  } else if (voltage >= 12.3) {

    state_of_charge =
      75 + random(-10, 10);

  } else if (voltage >= 12.0) {

    state_of_charge =
      50 + random(-10, 10);

  } else {

    state_of_charge =
      20 + random(-15, 15);
  }

  constrainSOC();
}

void constrainSOC() {

  if (state_of_charge < 0) {

    state_of_charge = 0;
  }

  if (state_of_charge > 100) {

    state_of_charge = 100;
  }
}

// ============================================
// TELEMETRY
// ============================================

String buildTelemetryPacket() {

  String packet =
    String(telemetryID) + "," +
    powerMode + "," +
    (heaterOn ? "1" : "0") + "," +
    String(temp_inside) + "," +
    String(temp_outside) + "," +
    String(voltage) + "," +
    String(state_of_charge);

  return packet;
}

void sendTelemetry() {

  String packet =
    buildTelemetryPacket();

  printTelemetry(packet);

  LoRa.beginPacket();

  LoRa.print(packet);

  LoRa.endPacket();

  delay(LORA_TX_DELAY);
}

void printTelemetry(String packet) {

  Serial.print("[SEND] ");

  Serial.print(millis() / 1000);

  Serial.print("s - ");

  Serial.println(packet);
}

// ============================================
// COMMAND LISTENING
// ============================================

void listenForCommands(
  unsigned long timeout
) {

  unsigned long start = millis();

  while (
    millis() - start < timeout
  ) {

    int packetSize =
      LoRa.parsePacket();

    if (packetSize) {

      String packet =
        readLoRaPacket();

      // ============================================
      // Ignore empty packets
      // ============================================

      if (packet.length() == 0) {

        printGarbage(packet);

        delay(PACKET_POLL_DELAY);

        continue;
      }

      // ============================================
      // Ignore stale ALIVE_ACK packets during normal
      // command listening. ALIVE_ACK is not a command,
      // so it must not break the listen window.
      // ============================================

      if (packet == "ALIVE_ACK") {

        Serial.println(
          "[IGNORE] Stale ALIVE_ACK during command listen"
        );

        delay(PACKET_POLL_DELAY);

        continue;
      }

      // ============================================
      // FIXED:
      // Only real commands are processed.
      // Garbage and telemetry fragments are ignored.
      // ============================================

      if (isCommandPacket(packet)) {

        bool validCommand =
          processCommandPacket(packet);

        if (validCommand) {

          break;
        }

      } else {

        printGarbage(packet);
      }

      delay(PACKET_POLL_DELAY);

      continue;
    }

    delay(PACKET_POLL_DELAY);
  }
}

String readLoRaPacket() {

  String packet = "";

  unsigned long readStart = millis();

  while (
    LoRa.available() &&
    (millis() - readStart) < PACKET_READ_TIMEOUT
  ) {

    packet +=
      (char)LoRa.read();
  }

  return packet;
}

// ============================================
// COMMAND PACKET VALIDATION
// ============================================

bool isCommandPacket(String packet) {

  int commaPos =
    packet.indexOf(',');

  if (commaPos <= 0) {

    return false;
  }

  // ============================================
  // A command packet must have only ONE comma.
  //
  // Valid:
  // 329,HEATER:ON
  //
  // Invalid telemetry:
  // 329,LOW,0,21.70,17.20,12.70,92.00
  // ============================================

  if (
    packet.indexOf(',', commaPos + 1) != -1
  ) {

    return false;
  }

  String idStr =
    packet.substring(0, commaPos);

  String command =
    packet.substring(commaPos + 1);

  // ID must contain only digits
  for (
    int i = 0;
    i < idStr.length();
    i++
  ) {

    if (!isDigit(idStr[i])) {

      return false;
    }
  }

  if (!isValidCommand(command)) {

    return false;
  }

  return true;
}

bool isValidCommand(String command) {

  if (command == "HEATER:ON") {

    return true;
  }

  if (command == "HEATER:OFF") {

    return true;
  }

  if (command == "MODE:LOW") {

    return true;
  }

  if (command == "MODE:NORMAL") {

    return true;
  }

  if (command == "SYSTEM:SHUTDOWN") {

    return true;
  }

  return false;
}

// ============================================
// COMMAND PROCESSING
// ============================================

bool processCommandPacket(String packet) {

  if (
    packet.length() == 0 ||
    packet.length() >= 40
  ) {

    printGarbage(packet);

    return false;
  }

  if (!isCommandPacket(packet)) {

    printGarbage(packet);

    return false;
  }

  Serial.print("[RECV] ");

  Serial.print(millis() / 1000);

  Serial.print("s - ");

  Serial.println(packet);

  int commaPos =
    packet.indexOf(',');

  int receivedID =
    extractCommandID(
      packet,
      commaPos
    );

  String command =
    extractCommand(
      packet,
      commaPos
    );

  printCommandInfo(
    receivedID,
    command
  );

  bool success =
    executeCommand(command);

  if (success) {

    telemetryID = receivedID;

    Serial.print(
      "[ACK] Telemetry ID updated to: "
    );

    Serial.println(telemetryID);

    // Receiver confirmed alive, restart telemetry count
    telemetryCounter = 0;

    aliveCheckPending = false;

    // FINAL SHUTDOWN ACK TELEMETRY
    if (systemShutdown) {

      Serial.println(
        "[SHUTDOWN] Sending final ACK telemetry"
      );

      sendTelemetry();
    }

    return true;
  }

  return false;
}

// ============================================
// COMMAND HELPERS
// ============================================

int extractCommandID(
  String packet,
  int commaPos
) {

  String idStr =
    packet.substring(0, commaPos);

  return idStr.toInt();
}

String extractCommand(
  String packet,
  int commaPos
) {

  return packet.substring(
    commaPos + 1
  );
}

void printCommandInfo(
  int id,
  String command
) {

  Serial.print("[ID] ");

  Serial.println(id);

  Serial.print("[COMMAND] ");

  Serial.println(command);
}

// ============================================
// COMMAND EXECUTION
// ============================================

bool executeCommand(String command) {

  // ============================================
  // HEATER ON
  // ============================================

  if (command == "HEATER:ON") {

    heaterOn = true;

    Serial.println(
      "[ACTION] Heater ON"
    );

    return true;
  }

  // ============================================
  // HEATER OFF
  // ============================================

  if (command == "HEATER:OFF") {

    heaterOn = false;

    Serial.println(
      "[ACTION] Heater OFF"
    );

    return true;
  }

  // ============================================
  // LOW POWER MODE
  // ============================================

  if (command == "MODE:LOW") {

    powerMode = "LOW";

    heaterOn = false;

    Serial.println(
      "[ACTION] Mode LOW"
    );

    return true;
  }

  // ============================================
  // NORMAL MODE
  // ============================================

  if (command == "MODE:NORMAL") {

    powerMode = "NORMAL";

    systemShutdown = false;

    Serial.println(
      "[ACTION] Mode NORMAL"
    );

    return true;
  }

  // ============================================
  // SYSTEM SHUTDOWN
  // ============================================

  if (command == "SYSTEM:SHUTDOWN") {

    heaterOn = false;

    powerMode = "LOW";

    systemShutdown = true;

    Serial.println(
      "[ACTION] SYSTEM SHUTDOWN"
    );

    return true;
  }

  // ============================================
  // UNKNOWN COMMAND
  // ============================================

  Serial.println(
    "[ACTION] Unknown command"
  );

  return false;
}

// ============================================
// UTILITIES
// ============================================

void printGarbage(String packet) {

  Serial.print("[GARBAGE] ");

  for (
    int i = 0;
    i < packet.length() && i < 20;
    i++
  ) {

    Serial.print("0x");

    Serial.print(
      (unsigned char)packet[i],
      HEX
    );

    Serial.print(" ");
  }

  Serial.println();
}