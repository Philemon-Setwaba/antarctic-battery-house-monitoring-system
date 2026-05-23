# =====================================================
# FASTAPI BACKEND FOR TELEMETRY + COMMAND SYSTEM
# =====================================================

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import StreamingResponse, Response
from pydantic import BaseModel
import psycopg2
from datetime import datetime
import io
import csv
import zipfile
import asyncio

# -----------------------------------------------------
# CREATE FASTAPI APP
# -----------------------------------------------------
app = FastAPI()

# -----------------------------------------------------
# ENABLE CORS (ALLOW FRONTEND TO CONNECT)
# -----------------------------------------------------
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],        # Allow all origins (OK for development)
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# -----------------------------------------------------
# DATABASE CONNECTION FUNCTION
# -----------------------------------------------------
def get_db_connection():
    """
    Creates a new connection to PostgreSQL database
    """
    return psycopg2.connect(
        dbname="Raw data",
        user="postgres",
        password="Lister@2004",
        host="localhost",
        port="5432"
    )

# -----------------------------------------------------
# LOGGING FUNCTION (FOR DEBUGGING)
# -----------------------------------------------------
def log(message):
    """
    Prints timestamped log messages to terminal
    """
    time = datetime.now().strftime('%d/%b/%Y %H:%M:%S')
    print(f"[{time}] {message}")

# =====================================================
# DATA MODELS
# =====================================================

class Telemetry(BaseModel):
    temp_inside: float
    temp_outside: float
    voltage: float
    state_of_charge: float
    heater_status: bool
    power_mode: str
    rssi: int

class Command(BaseModel):
    command_type: str
    command_value: str


# =====================================================
# SHARED DATABASE FUNCTION
# =====================================================

def fetch_latest_telemetry():
    """
    Gets the latest telemetry row from the database.
    Used by both /data and WebSocket.
    """
    conn = get_db_connection()
    cur = conn.cursor()

    cur.execute("""
        SELECT *
        FROM telemetry
        ORDER BY timestamp DESC
        LIMIT 1;
    """)

    row = cur.fetchone()

    cur.close()
    conn.close()

    if row is None:
        return None

    return {
        "id": row[0],
        "timestamp": str(row[1]),
        "temp_inside": row[2],
        "temp_outside": row[3],
        "voltage": row[4],
        "state_of_charge": row[5],
        "heater_status": row[6],
        "power_mode": row[7],
        "rssi": row[8]
    }


# =====================================================
# TELEMETRY ENDPOINTS
# =====================================================

@app.post("/telemetry")
def store_telemetry(data: Telemetry):
    """
    Receives telemetry data from ESP32
    and stores it in the database
    """
    log("POST /telemetry")

    conn = get_db_connection()
    cur = conn.cursor()

    cur.execute("""
        INSERT INTO telemetry (
            temp_inside,
            temp_outside,
            voltage,
            state_of_charge,
            heater_status,
            power_mode,
            rssi
        )
        VALUES (%s, %s, %s, %s, %s, %s, %s)
    """, (
        data.temp_inside,
        data.temp_outside,
        data.voltage,
        data.state_of_charge,
        data.heater_status,
        data.power_mode,
        data.rssi
    ))

    conn.commit()
    cur.close()
    conn.close()

    return {"status": "stored"}


@app.get("/data")
def get_latest_telemetry():
    """
    Returns the most recent telemetry data
    for dashboard display
    """
    log("GET /data")

    data = fetch_latest_telemetry()

    if data is None:
        return {"message": "No telemetry data found"}

    return data


# =====================================================
# REAL-TIME TELEMETRY WEBSOCKET
# =====================================================

@app.websocket("/ws/telemetry")
async def websocket_telemetry(websocket: WebSocket):
    """
    Website connects here.
    Backend checks the latest DB telemetry ID.
    If a new row appears, it sends that row to the website.
    """
    await websocket.accept()

    log("WebSocket /ws/telemetry connected")

    last_sent_id = None

    try:
        while True:
            data = fetch_latest_telemetry()

            if data is not None:
                current_id = data["id"]

                # Only send when DB has a new telemetry row
                if current_id != last_sent_id:
                    last_sent_id = current_id
                    await websocket.send_json(data)

            # Backend checks DB often, but website only updates when new data arrives
            await asyncio.sleep(0.2)

    except WebSocketDisconnect:
        log("WebSocket /ws/telemetry disconnected")


# =====================================================
# COMMAND ENDPOINTS
# =====================================================

@app.post("/command")
def send_command(cmd: Command):
    """
    Receives command from website
    and stores it in database
    """
    log(f"POST /command → {cmd.command_type} {cmd.command_value}")

    conn = get_db_connection()
    cur = conn.cursor()

    cur.execute("""
        INSERT INTO commands (command_type, command_value)
        VALUES (%s, %s)
    """, (
        cmd.command_type,
        cmd.command_value
    ))

    conn.commit()
    cur.close()
    conn.close()

    return {"status": "command stored"}


@app.get("/command/next")
def get_next_command():
    """
    ESP32 calls this endpoint to:
    - get the next pending command
    - mark it as sent
    """
    log("GET /command/next")

    conn = get_db_connection()
    cur = conn.cursor()

    cur.execute("""
        SELECT id, command_type, command_value
        FROM commands
        WHERE sent = FALSE
        ORDER BY timestamp ASC
        LIMIT 1;
    """)

    row = cur.fetchone()

    if row is None:
        cur.close()
        conn.close()
        return {"message": "No new command"}

    command_id = row[0]

    cur.execute("""
        UPDATE commands
        SET sent = TRUE
        WHERE id = %s;
    """, (command_id,))

    conn.commit()

    cur.close()
    conn.close()

    return {
        "id": row[0],
        "command_type": row[1],
        "command_value": row[2]
    }


# =====================================================
# EXPORT ALL DATA AS ZIP WITH TWO CSV FILES
# =====================================================

@app.get("/export/all")
def export_all_data():
    """
    Exports telemetry and commands as two separate CSV files in a ZIP archive
    """
    log("GET /export/all - Exporting both tables as separate CSV files")

    conn = get_db_connection()
    cur = conn.cursor()

    # ----- TELEMETRY DATA -----
    cur.execute("SELECT * FROM telemetry ORDER BY timestamp;")
    telemetry_rows = cur.fetchall()
    telemetry_cols = [desc[0] for desc in cur.description]

    # ----- COMMANDS DATA -----
    cur.execute("SELECT * FROM commands ORDER BY timestamp;")
    commands_rows = cur.fetchall()
    commands_cols = [desc[0] for desc in cur.description]

    cur.close()
    conn.close()

    zip_buffer = io.BytesIO()

    with zipfile.ZipFile(zip_buffer, 'w', zipfile.ZIP_DEFLATED) as zip_file:

        telemetry_csv = io.StringIO()
        telemetry_writer = csv.writer(telemetry_csv)
        telemetry_writer.writerow(telemetry_cols)

        for row in telemetry_rows:
            telemetry_writer.writerow(row)

        zip_file.writestr(
            'telemetry_data.csv',
            telemetry_csv.getvalue()
        )

        commands_csv = io.StringIO()
        commands_writer = csv.writer(commands_csv)
        commands_writer.writerow(commands_cols)

        for row in commands_rows:
            commands_writer.writerow(row)

        zip_file.writestr(
            'commands_data.csv',
            commands_csv.getvalue()
        )

        readme_content = """Database Export - Two Tables Included:
1. telemetry_data.csv - Contains all telemetry sensor readings
2. commands_data.csv - Contains all command history

Export Date: {0}
        """.format(datetime.now().strftime('%Y-%m-%d %H:%M:%S'))

        zip_file.writestr(
            'README.txt',
            readme_content
        )

    zip_buffer.seek(0)

    return Response(
        content=zip_buffer.getvalue(),
        media_type="application/zip",
        headers={
            "Content-Disposition":
            "attachment; filename=database_export_{}.zip".format(
                datetime.now().strftime('%Y%m%d_%H%M%S')
            )
        }
    )


# =====================================================
# OPTIONAL: SEPARATE CSV EXPORTS
# =====================================================

@app.get("/export/telemetry")
def export_telemetry_only():
    """
    Export ONLY telemetry data as CSV
    """
    log("GET /export/telemetry")

    conn = get_db_connection()
    cur = conn.cursor()

    cur.execute("SELECT * FROM telemetry ORDER BY timestamp;")
    rows = cur.fetchall()
    col_names = [desc[0] for desc in cur.description]

    cur.close()
    conn.close()

    output = io.StringIO()
    writer = csv.writer(output)
    writer.writerow(col_names)
    writer.writerows(rows)

    output.seek(0)

    return StreamingResponse(
        output,
        media_type="text/csv",
        headers={
            "Content-Disposition":
            "attachment; filename=telemetry_data.csv"
        }
    )


@app.get("/export/commands")
def export_commands_only():
    """
    Export ONLY commands data as CSV
    """
    log("GET /export/commands")

    conn = get_db_connection()
    cur = conn.cursor()

    cur.execute("SELECT * FROM commands ORDER BY timestamp;")
    rows = cur.fetchall()
    col_names = [desc[0] for desc in cur.description]

    cur.close()
    conn.close()

    output = io.StringIO()
    writer = csv.writer(output)
    writer.writerow(col_names)
    writer.writerows(rows)

    output.seek(0)

    return StreamingResponse(
        output,
        media_type="text/csv",
        headers={
            "Content-Disposition":
            "attachment; filename=commands_data.csv"
        }
    )