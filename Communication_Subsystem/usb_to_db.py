import serial
import requests
import json
import time
import sys

# ============================================
# CONFIGURATION
# ============================================

COM_PORT = "COM5"

API_URL = "http://localhost:8000/telemetry"

COMMAND_URL = "http://localhost:8000/command/next"

UPDATE_SENT_URL = "http://localhost:8000/command/sent"

# ============================================
# RTT TRACKING
# ============================================

command_send_times = {}

# ============================================
# SERIAL CONNECTION
# ============================================

def connect_serial(port, baudrate=115200):

    try:

        ser = serial.Serial(
            port,
            baudrate,
            timeout=1
        )

        time.sleep(2)

        print(f"✅ Connected to {port}")

        return ser

    except serial.SerialException as e:

        print(f"❌ Serial Error: {e}")

        sys.exit(1)

# ============================================
# DATABASE
# ============================================

def send_telemetry_to_db(data):

    try:

        response = requests.post(
            API_URL,
            json=data,
            timeout=2
        )

        return response.status_code == 200

    except requests.exceptions.RequestException:

        return False


def get_next_command():

    try:

        response = requests.get(
            COMMAND_URL,
            timeout=1
        )

        if response.status_code != 200:
            return None

        data = response.json()

        if "command_type" not in data:
            return None

        return data

    except requests.exceptions.RequestException:

        return None


def update_command_sent(command_id):

    try:

        requests.post(
            UPDATE_SENT_URL,
            json={"id": command_id},
            timeout=1
        )

    except requests.exceptions.RequestException:

        pass

# ============================================
# COMMAND MAPPING
# ============================================

def map_command(command_type, command_value):

    command_type = command_type.upper()

    command_value = command_value.upper()

    # ============================================
    # POWER MODE MAPPING
    # ============================================

    if command_type == "POWER_MODE":

        command_type = "MODE"

        if command_value == "LOW POWER":
            command_value = "LOW"

        elif command_value == "NORMAL":
            command_value = "NORMAL"

    return command_type, command_value

# ============================================
# COMMAND SENDING
# ============================================

def send_command_to_rx(ser, command):

    command_id = command["id"]

    command_type, command_value = map_command(
        command["command_type"],
        command["command_value"]
    )

    command_str = (
        f"{command_id}|"
        f"{command_type}|"
        f"{command_value}"
    )

    # ============================================
    # STORE SEND TIME
    # ============================================

    command_send_times[command_id] = time.time()

    ser.write(f"{command_str}\n".encode())

    print(
        f"📨 Sent -> RX: {command_str}"
    )

    print(
        f"⏱️ Timer started for Command ID {command_id}"
    )

# ============================================
# SERIAL PROCESSING
# ============================================

def process_serial_line(line):

    line = line.strip()

    if not line:
        return

    try:

        data = json.loads(line)

    except json.JSONDecodeError:

        print(f"DEBUG: {line}")

        return

    process_json_message(data)

# ============================================
# JSON MESSAGE HANDLING
# ============================================

def process_json_message(data):

    msg_type = data.get("type")

    status = data.get("status")

    # ============================================
    # TELEMETRY
    # ============================================

    if msg_type == "telemetry":

        print_telemetry(data)

        send_telemetry_to_db(data)

        return

    # ============================================
    # COMMAND SENT
    # ============================================

    if status == "command_sent":

        command_id = data.get("id")

        print(
            f"✅ Command sent | ID: {command_id}"
        )

        update_command_sent(command_id)

        return

    # ============================================
    # COMMAND RECEIVED
    # ============================================

    if status == "command_received":

        print(
            f"📨 RX received command | "
            f"ID: {data.get('id')}"
        )

        return

    # ============================================
    # COMMAND SKIPPED
    # ============================================

    if status == "command_skipped":

        print(
            f"⚠️ Command skipped | "
            f"ID: {data.get('id')} | "
            f"{data.get('reason')}"
        )

        return

    # ============================================
    # COMMAND DELAYED
    # ============================================

    if status == "command_delayed":

        print(
            f"⏳ Command delayed | "
            f"ID: {data.get('id')}"
        )

        return

    # ============================================
    # TIMEOUT
    # ============================================

    if status == "command_timeout":

        print(
            f"⌛ Command timeout | "
            f"ID: {data.get('id')}"
        )

        return

    # ============================================
    # READY
    # ============================================

    if status == "ready":

        print("✅ RX Ready")

        return

# ============================================
# TELEMETRY DISPLAY
# ============================================

def print_telemetry(data):

    print("\n==============================")

    telemetry_id = data.get("counter")

    print(
        f"Telemetry ACK ID: "
        f"{telemetry_id}"
    )

    print(
        f"Inside Temp: "
        f"{data.get('temp_inside')}°C"
    )

    print(
        f"Outside Temp: "
        f"{data.get('temp_outside')}°C"
    )

    print(
        f"Voltage: "
        f"{data.get('voltage')}V"
    )

    print(
        f"SOC: "
        f"{data.get('state_of_charge')}%"
    )

    print(
        f"Heater: "
        f"{'ON' if data.get('heater_status') else 'OFF'}"
    )

    print(
        f"Mode: "
        f"{data.get('power_mode')}"
    )

    print(
        f"RSSI: "
        f"{data.get('rssi')} dBm"
    )

    # ============================================
    # RTT MEASUREMENT
    # ============================================

    if telemetry_id in command_send_times:

        send_time = command_send_times[telemetry_id]

        ack_time = time.time()

        rtt = ack_time - send_time

        send_time_str = time.strftime(
            "%H:%M:%S",
            time.localtime(send_time)
        )

        ack_time_str = time.strftime(
            "%H:%M:%S",
            time.localtime(ack_time)
        )

        print("\n================================")

        print(
            f"✅ ACK RECEIVED | "
            f"ID: {telemetry_id}"
        )

        print(
            f"📤 Command Sent Time: "
            f"{send_time_str}"
        )

        print(
            f"📥 ACK Received Time: "
            f"{ack_time_str}"
        )

        print(
            f"📡 RTT: "
            f"{rtt * 1000:.1f} ms"
        )

        print("================================")

        # Remove completed timing
        del command_send_times[telemetry_id]

# ============================================
# MAIN
# ============================================

def main():

    print("=================================")
    print("LoRa RX Bridge")
    print("=================================")

    ser = connect_serial(COM_PORT)

    ser.reset_input_buffer()

    last_command_check = time.time()

    command_check_interval = 2

    try:

        while True:

            # ============================================
            # READ RX SERIAL
            # ============================================

            if ser.in_waiting:

                line = ser.readline().decode(
                    "utf-8",
                    errors="ignore"
                )

                process_serial_line(line)

            # ============================================
            # CHECK DATABASE FOR COMMANDS
            # ============================================

            current_time = time.time()

            if (
                current_time - last_command_check
                >= command_check_interval
            ):

                command = get_next_command()

                if command:

                    send_command_to_rx(
                        ser,
                        command
                    )

                last_command_check = current_time

            time.sleep(0.01)

    except KeyboardInterrupt:

        print("\n👋 Exiting...")

    finally:

        ser.close()

# ============================================
# START
# ============================================

if __name__ == "__main__":

    main()