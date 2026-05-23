# Communication Subsystem

This folder contains files related to the communication subsystem of the Smart Battery Thermal Regulation System.

## Purpose

This subsystem is responsible for wireless communication between the remote unit and the base station using the SX1278 RA-02 LoRa 433 MHz module. The remote unit sends telemetry data through the LoRa link, while the base station receives the telemetry and forwards it to the user interface. The subsystem also allows control commands from the user interface to be sent back to the remote unit through the same LoRa link.

## Contents

This folder may include:

- Remote unit code for the Arduino Uno R3 and SX1278 RA-02 LoRa 433 MHz transmitter module
- Base station receiver code for the ESP32 Dev Board and SX1278 RA-02 LoRa 433 MHz receiver module
- Python serial bridge script
- FastAPI backend files
- PostgreSQL database scripts
- Web-based dashboard files
- Communication test evidence
- Supporting documentation

## Subsystem Role

The communication subsystem provides SX1278 RA-02 LoRa-based telemetry transmission, bidirectional command handling, database storage, dashboard updates, communication warnings, and CSV data export. It links the remote battery system to the base station user interface so that system data can be monitored and control commands can be sent remotely.
