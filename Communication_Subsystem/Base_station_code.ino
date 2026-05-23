#include <SPI.h>
#include <LoRa.h>

// ============================================
// LORA PINS
// ============================================
#define SS_PIN   5
#define RST_PIN  14
#define DIO0_PIN 2

// ============================================
// TELEMETRY DATA
// ============================================
float temp_inside = 0;
float temp_outside = 0;
float voltage = 0;
float state_of_charge = 0;

bool heater_status = false;

String power_mode = "NORMAL";

int rssi = 0;
int latestTelemetryID = 0;

// ============================================
// COMMAND STATE
// ============================================
String pendingCommand = "";

int pendingCommandID = 0;

bool waitingForPacket = false;

int commandRetryCount = 0;

// ============================================
// CONNECTION STATE
// ============================================
bool txConnected = false;

unsigned long lastTelemetryTime = 0;

const unsigned long TELEMETRY_TIMEOUT = 60000;

// ============================================
// INTERRUPT CALLBACK VARIABLES
// ============================================
volatile bool newPacketReceived = false;

volatile int receivedPacketSize = 0;

String receivedPacket = "";

// ============================================
// SETUP
// ============================================
void setup() {

  Serial.begin(115200);

  delay(1000);

  setupLoRa();

  printStartupMessage();
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {

  handleSerialCommands();

  processReceivedPacket();

  monitorTelemetryTimeout();

  delay(10);
}

// ============================================
// LORA SETUP
// ============================================
void setupLoRa() {

  LoRa.setPins(
    SS_PIN,
    RST_PIN,
    DIO0_PIN
  );

  if (!LoRa.begin(433E6)) {

    Serial.println(
      "{\"status\":\"error\",\"message\":\"LoRa init failed\"}"
    );

    while (1);
  }

  configureLoRa();

  LoRa.onReceive(onReceive);

  LoRa.receive();
}

void configureLoRa() {

  LoRa.setSpreadingFactor(12);

  LoRa.setSignalBandwidth(125E3);

  LoRa.setCodingRate4(8);

  LoRa.setTxPower(14);

  LoRa.setSyncWord(0x12);
}

void printStartupMessage() {

  Serial.println(
    "ESP32 RX Gateway Started"
  );

  Serial.println(
    "{\"status\":\"listening_for_tx\"}"
  );
}

// ============================================
// SERIAL COMMANDS
// ============================================
void handleSerialCommands() {

  if (!txConnected) {
    return;
  }

  if (!Serial.available()) {
    return;
  }

  String line =
    readSerialLine();

  if (line.length() == 0) {
    return;
  }

  processSerialCommand(line);
}

String readSerialLine() {

  String line =
    Serial.readStringUntil('\n');

  line.trim();

  return line;
}

void processSerialCommand(String line) {

  int firstPipe =
    line.indexOf('|');

  int secondPipe =
    line.indexOf('|', firstPipe + 1);

  if (
    firstPipe <= 0 ||
    secondPipe <= 0
  ) {

    Serial.println(
      "{\"status\":\"invalid_serial_format\"}"
    );

    return;
  }

  int commandID =
    line.substring(
      0,
      firstPipe
    ).toInt();

  String commandType =
    line.substring(
      firstPipe + 1,
      secondPipe
    );

  String commandValue =
    line.substring(
      secondPipe + 1
    );

  String command =
    commandType + ":" + commandValue;

  sendCommandReceivedJSON(
    commandID,
    command
  );

  processCommand(
    commandID,
    command
  );
}

void sendCommandReceivedJSON(
  int id,
  String command
) {

  Serial.println(
    "{\"status\":\"command_received\",\"id\":" +
    String(id) +
    ",\"command\":\"" +
    command +
    "\"}"
  );
}

// ============================================
// COMMAND PROCESSING
// ============================================
void processCommand(
  int commandID,
  String command
) {

  if (
    latestTelemetryID >= commandID
  ) {

    sendCommandSkippedJSON(commandID);

    return;
  }

  pendingCommandID = commandID;

  pendingCommand = command;

  waitingForPacket = true;

  commandRetryCount = 0;

  Serial.println(
    "{\"status\":\"command_waiting_for_next_tx\",\"id\":" +
    String(pendingCommandID) +
    "}"
  );
}

void sendCommand(
  int id,
  String command
) {

  String packet =
    String(id) +
    "," +
    command;

  LoRa.idle();

  LoRa.beginPacket();

  LoRa.print(packet);

  LoRa.endPacket();

  delay(10);

  LoRa.receive();

  sendCommandSentJSON(
    id,
    command
  );
}

void sendCommandSentJSON(
  int id,
  String command
) {

  Serial.println(
    "{\"status\":\"command_sent\",\"id\":" +
    String(id) +
    ",\"command\":\"" +
    command +
    "\"}"
  );
}

void sendCommandSkippedJSON(
  int id
) {

  Serial.println(
    "{\"status\":\"command_skipped\",\"id\":" +
    String(id) +
    ",\"reason\":\"already_acknowledged\"}"
  );
}

// ============================================
// INTERRUPT CALLBACK
// ============================================
void onReceive(
  int packetSize
) {

  if (!packetSize) {
    return;
  }

  receivedPacketSize =
    packetSize;

  newPacketReceived = true;
}

// ============================================
// PROCESS RECEIVED PACKETS
// ============================================
void processReceivedPacket() {

  if (!newPacketReceived) {
    return;
  }

  newPacketReceived = false;

  receivedPacket = "";

  while (LoRa.available()) {

    receivedPacket +=
      (char)LoRa.read();
  }

  receivedPacket.trim();

  if (
    receivedPacket.length() == 0 ||
    receivedPacket.length() > 80
  ) {

    LoRa.receive();

    return;
  }

  // ==========================================
  // ALIVE REQUEST
  // ==========================================
  if (
    receivedPacket == "ALIVE?"
  ) {

    replyAliveAck();

    txConnected = true;

    lastTelemetryTime = millis();

    LoRa.receive();

    return;
  }

  // ==========================================
  // TELEMETRY PACKET
  // ==========================================
  if (
    receivedPacket.indexOf(",") > 0
  ) {

    txConnected = true;

    parseTelemetry(receivedPacket);

    rssi = LoRa.packetRssi();

    lastTelemetryTime = millis();

    sendTelemetryJSON();

    // ========================================
    // PROCESS COMMAND AFTER TX SPEAKS
    // ========================================
    processPendingCommand();
  }

  LoRa.receive();
}

// ============================================
// ALIVE ACK
// ============================================
void replyAliveAck() {

  Serial.println(
    "{\"status\":\"alive_request_received\"}"
  );

  LoRa.idle();

  LoRa.beginPacket();

  LoRa.print("ALIVE_ACK");

  LoRa.endPacket();

  Serial.println(
    "{\"status\":\"alive_ack_sent\"}"
  );

  delay(10);

  LoRa.receive();
}

// ============================================
// TELEMETRY TIMEOUT
// ============================================
void monitorTelemetryTimeout() {

  if (!txConnected) {
    return;
  }

  if (
    millis() - lastTelemetryTime >=
    TELEMETRY_TIMEOUT
  ) {

    txConnected = false;

    Serial.println(
      "{\"status\":\"telemetry_timeout\",\"message\":\"Listening for TX again\"}"
    );
  }
}

// ============================================
// TELEMETRY PARSING
// ============================================
void parseTelemetry(
  String data
) {

  int commas[6];

  commas[0] = data.indexOf(',');

  for (int i = 1; i < 6; i++) {

    commas[i] =
      data.indexOf(
        ',',
        commas[i - 1] + 1
      );
  }

  if (!areCommasValid(commas)) {
    return;
  }

  extractTelemetryValues(
    data,
    commas
  );
}

bool areCommasValid(
  int commas[]
) {

  for (int i = 0; i < 6; i++) {

    if (commas[i] <= 0) {
      return false;
    }
  }

  return true;
}

void extractTelemetryValues(
  String data,
  int commas[]
) {

  latestTelemetryID =
    data.substring(
      0,
      commas[0]
    ).toInt();

  power_mode =
    data.substring(
      commas[0] + 1,
      commas[1]
    );

  String heaterState =
    data.substring(
      commas[1] + 1,
      commas[2]
    );

  heater_status =
    (heaterState == "1");

  temp_inside =
    data.substring(
      commas[2] + 1,
      commas[3]
    ).toFloat();

  temp_outside =
    data.substring(
      commas[3] + 1,
      commas[4]
    ).toFloat();

  voltage =
    data.substring(
      commas[4] + 1,
      commas[5]
    ).toFloat();

  state_of_charge =
    data.substring(
      commas[5] + 1
    ).toFloat();
}

void sendTelemetryJSON() {

  String json = "{";

  json += "\"type\":\"telemetry\",";
  json += "\"counter\":" + String(latestTelemetryID) + ",";
  json += "\"temp_inside\":" + String(temp_inside, 1) + ",";
  json += "\"temp_outside\":" + String(temp_outside, 1) + ",";
  json += "\"voltage\":" + String(voltage, 2) + ",";
  json += "\"state_of_charge\":" + String(state_of_charge, 1) + ",";
  json += "\"heater_status\":" + String(heater_status ? "true" : "false") + ",";
  json += "\"power_mode\":\"" + power_mode + "\",";
  json += "\"rssi\":" + String(rssi);

  json += "}";

  Serial.println(json);
}

// ============================================
// RETRY LOGIC
// ============================================
void processPendingCommand() {

  if (!waitingForPacket) {
    return;
  }

  // ==========================================
  // COMMAND ACKNOWLEDGED
  // ==========================================
  if (
    latestTelemetryID >= pendingCommandID
  ) {

    Serial.println(
      "{\"status\":\"command_acknowledged\",\"id\":" +
      String(pendingCommandID) +
      "}"
    );

    clearPendingCommand();

    return;
  }

  // ==========================================
  // TX SPOKE BUT DID NOT ACK
  // RESEND COMMAND NOW
  // ==========================================
  commandRetryCount++;

  Serial.println(
    "{\"status\":\"command_retry_after_tx\",\"id\":" +
    String(pendingCommandID) +
    ",\"attempt\":" +
    String(commandRetryCount) +
    "}"
  );

  sendCommand(
    pendingCommandID,
    pendingCommand
  );
}

void clearPendingCommand() {

  pendingCommand = "";

  pendingCommandID = 0;

  waitingForPacket = false;

  commandRetryCount = 0;
}