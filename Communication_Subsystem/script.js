// =============================
// API CONFIG
// =============================
const API_BASE_URL = "http://127.0.0.1:8000";

// =============================
// DASHBOARD ELEMENTS
// =============================
const insideTempSpan = document.getElementById("insideTemp");
const outsideTempSpan = document.getElementById("outsideTemp");
const batteryVoltageSpan = document.getElementById("batteryVoltage");
const socSpan = document.getElementById("soc");
const heaterStatusSpan = document.getElementById("heaterStatusText");
const powerModeSpan = document.getElementById("powerMode");
const commStatusSpan = document.getElementById("commStatus");
const rssiValueSpan = document.getElementById("rssiValue");
const lastUpdateSpan = document.getElementById("lastUpdate");
const heaterNote = document.getElementById("heaterNote");

// =============================
// HEATER SWITCH ELEMENTS (CONTROL PANEL - USER ONLY)
// =============================
const heaterSwitch = document.getElementById("heaterSwitch");
const heaterOnOption = document.getElementById("heaterOnOption");
const heaterOffOption = document.getElementById("heaterOffOption");

// =============================
// MODE & BUTTON CONTROLS (CONTROL PANEL - USER ONLY)
// =============================
const powerModeSelect = document.getElementById("powerModeSelect");
const btnApplyMode = document.getElementById("btnApplyMode");
const btnShutdown = document.getElementById("btnShutdown");
const btnDownloadCsv = document.getElementById("btnDownloadCsv");

// =============================
// LOW POWER MODE VARIABLES
// =============================
const lowPowerWarning = document.getElementById("lowPowerWarning");
let isModeChangeLocked = false;

// =============================
// INITIAL STATE
// =============================
let currentPowerMode = "Normal";
let pendingCommand = null;
let controlPanelHeaterState = false;

// TELEMETRY UPDATE INTERVAL
let telemetryInterval = null;

// =============================
// CHART VARIABLES
// =============================
let tempChart, socChart, heaterChart;
let telemetryHistory = [];

// IMPORTANT FIX:
// This prevents the same DB row from being added to the graph more than once.
let lastChartTelemetryKey = null;

const MAX_HISTORY_POINTS = 50;

// =============================
// TOAST NOTIFICATION
// =============================
function showToast(message) {
    const existingToast = document.getElementById("system-toast");

    if (existingToast) {
        existingToast.remove();
    }

    const toast = document.createElement("div");
    toast.id = "system-toast";
    toast.className = "system-toast";
    toast.textContent = message;

    document.body.appendChild(toast);

    requestAnimationFrame(() => {
        toast.classList.add("show");
    });

    setTimeout(() => {
        toast.classList.remove("show");
        toast.classList.add("hide");

        setTimeout(() => {
            if (toast.parentNode) {
                toast.remove();
            }
        }, 300);
    }, 2500);
}

// =============================
// LAST UPDATE TIME
// =============================
function updateLastUpdateTime() {
    lastUpdateSpan.textContent = new Date().toLocaleTimeString();
}

// =============================
// DISABLE/ENABLE ALL CONTROL PANEL CONTROLS BASED ON DB POWER MODE
// =============================
function setControlPanelDisabled(isLowPower) {
    if (isLowPower) {
        powerModeSelect.disabled = true;
        btnApplyMode.disabled = true;

        powerModeSelect.classList.add("disabled-control");
        btnApplyMode.classList.add("disabled-btn");

        heaterOnOption.disabled = true;
        heaterOffOption.disabled = true;
        heaterSwitch.classList.add("disabled-switch");

        if (heaterNote) {
            heaterNote.style.display = "block";
        }

        if (lowPowerWarning) {
            lowPowerWarning.style.display = "block";
        }

    } else {
        powerModeSelect.disabled = false;
        btnApplyMode.disabled = false;

        powerModeSelect.classList.remove("disabled-control");
        btnApplyMode.classList.remove("disabled-btn");

        powerModeSelect.title = "";
        btnApplyMode.title = "";

        heaterOnOption.disabled = false;
        heaterOffOption.disabled = false;
        heaterSwitch.classList.remove("disabled-switch");

        heaterOnOption.title = "";
        heaterOffOption.title = "";
        heaterSwitch.title = "";

        if (heaterNote) {
            heaterNote.style.display = "none";
        }

        if (lowPowerWarning) {
            lowPowerWarning.style.display = "none";
        }
    }
}

// =============================
// SEND COMMAND TO BACKEND
// =============================
async function sendCommand(commandType, commandValue) {
    try {
        const response = await fetch(`${API_BASE_URL}/command`, {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
            },
            body: JSON.stringify({
                command_type: commandType,
                command_value: commandValue
            })
        });

        if (!response.ok) {
            throw new Error("Failed to store command");
        }

        const result = await response.json();
        console.log("Command stored:", result);

        return true;

    } catch (error) {
        console.error("Failed to send command:", error.message);
        showToast("Failed to send command to backend.");

        return false;
    }
}

// =============================
// UPDATE CONTROL PANEL HEATER BUTTONS (VISUAL ONLY)
// =============================
function updateControlPanelHeaterButtons(isOn) {
    controlPanelHeaterState = isOn;

    heaterOnOption.classList.toggle("active-option", isOn);
    heaterOffOption.classList.toggle("active-option", !isOn);

    heaterSwitch.classList.toggle("heater-switch-on", isOn);
    heaterSwitch.classList.toggle("heater-switch-off", !isOn);
}

// =============================
// GENERAL CONFIRMATION MODAL
// =============================
function showConfirmationModal(options) {
    const existingModal = document.getElementById("general-confirmation-modal");

    if (existingModal) {
        existingModal.remove();
    }

    const modal = document.createElement("div");
    modal.id = "general-confirmation-modal";
    modal.className = "confirm-modal";

    modal.innerHTML = `
        <div class="confirm-dialog">
            <div class="confirm-icon">${options.icon}</div>
            <div class="confirm-title">${options.title}</div>
            <div class="confirm-message">${options.message}</div>
            <div class="confirm-actions">
                <button id="confirm-accept" class="confirm-accept-btn">${options.acceptText}</button>
                <button id="confirm-decline" class="confirm-decline-btn">${options.declineText}</button>
            </div>
        </div>
    `;

    document.body.appendChild(modal);

    const acceptBtn = document.getElementById("confirm-accept");
    const declineBtn = document.getElementById("confirm-decline");

    acceptBtn.addEventListener("click", () => {
        modal.remove();

        if (options.onAccept) {
            options.onAccept();
        }
    });

    declineBtn.addEventListener("click", () => {
        modal.remove();

        if (options.onDecline) {
            options.onDecline();
        }
    });

    modal.addEventListener("click", (event) => {
        if (event.target === modal) {
            modal.remove();

            if (options.onDecline) {
                options.onDecline();
            }
        }
    });
}

// =============================
// HEATER CONFIRMATION
// =============================
function showHeaterConfirmation(targetState) {
    const actionText = targetState ? "turn ON" : "turn OFF";
    const targetValue = targetState ? "ON" : "OFF";

    showConfirmationModal({
        icon: targetState ? "🔥" : "❄️",
        title: "Confirm Heater Change",
        message: `Are you sure you want to ${actionText} the heater?<br><strong>Command: ${targetValue}</strong>`,
        acceptText: "Accept",
        declineText: "Decline",

        onAccept: () => {
            setHeaterState(targetState);
        },

        onDecline: () => {
            showToast("Heater change cancelled.");
        }
    });
}

// =============================
// CHECK IF TELEMETRY MATCHES PENDING COMMAND
// =============================
function matchesPendingCommand(data) {
    if (!pendingCommand) {
        return true;
    }

    if (pendingCommand.type === "heater") {
        const expected = pendingCommand.value === "ON";
        return data.heater_status === expected;
    }

    if (pendingCommand.type === "power_mode") {
        return data.power_mode === pendingCommand.value;
    }

    if (pendingCommand.type === "system") {
        return false;
    }

    return true;
}

// =============================
// APPLY HEATER STATE - TOGGLE CONTROL PANEL VISUALLY AND SEND COMMAND
// =============================
async function setHeaterState(isOn, showMessage = true) {
    const targetValue = isOn ? "ON" : "OFF";

    pendingCommand = {
        type: "heater",
        value: targetValue
    };

    updateControlPanelHeaterButtons(isOn);
    updateLastUpdateTime();

    if (showMessage) {
        showToast(`Heater command sent: ${targetValue}`);
    }

    console.log(`Heater command sent: ${targetValue}`);

    const ok = await sendCommand("heater", targetValue);

    if (!ok) {
        pendingCommand = null;
        loadTelemetryData();
    }
}

// =============================
// APPLY POWER MODE - USER ACTION
// =============================
async function applyPowerMode(mode) {
    pendingCommand = {
        type: "power_mode",
        value: mode
    };

    updateLastUpdateTime();

    showToast(`Power mode command sent: ${mode}`);

    console.log(`Power mode command sent: ${mode}`);

    const ok = await sendCommand("power_mode", mode);

    if (!ok) {
        pendingCommand = null;
        loadTelemetryData();
    }
}

// =============================
// DOWNLOAD ALL DATA AS ZIP
// =============================
function downloadDatabaseExport() {
    try {
        showToast("Preparing database export...");

        window.location.href = `${API_BASE_URL}/export/all`;

        showToast("Download started! Check your downloads folder.");

    } catch (error) {
        console.error("Download failed:", error.message);
        showToast("Failed to download database export.");
    }
}

// =============================
// UPDATE CHARTS WITH REAL DATA
// FIXED:
// 1. Uses DB timestamp, not browser time.
// 2. Only adds a point when new telemetry is received.
// =============================
function updateCharts(telemetryData) {
    if (!tempChart || !socChart || !heaterChart) {
        console.warn("Charts not initialized");
        return;
    }

    // Use database row ID if available.
    // If ID is not available, use database timestamp.
    const telemetryKey = telemetryData.id ?? telemetryData.timestamp;

    // If this is the same telemetry row as before, do not add it again.
    if (telemetryKey === lastChartTelemetryKey) {
        return;
    }

    // Save the latest telemetry key.
    lastChartTelemetryKey = telemetryKey;

    // Use telemetry timestamp from database.
    // Only fall back to browser time if timestamp is missing.
    const timestamp = telemetryData.timestamp
        ? new Date(telemetryData.timestamp).toLocaleTimeString()
        : new Date().toLocaleTimeString();

    telemetryHistory.push({
        time: timestamp,
        temp_inside: telemetryData.temp_inside,
        temp_outside: telemetryData.temp_outside,
        soc: telemetryData.state_of_charge,
        heater: telemetryData.heater_status ? 1 : 0
    });

    if (telemetryHistory.length > MAX_HISTORY_POINTS) {
        telemetryHistory.shift();
    }

    const labels = telemetryHistory.map(point => point.time);
    const insideTemps = telemetryHistory.map(point => point.temp_inside);
    const outsideTemps = telemetryHistory.map(point => point.temp_outside);
    const socData = telemetryHistory.map(point => point.soc);
    const heaterData = telemetryHistory.map(point => point.heater);

    tempChart.data.labels = labels;
    tempChart.data.datasets[0].data = insideTemps;
    tempChart.data.datasets[1].data = outsideTemps;
    tempChart.update();

    socChart.data.labels = labels;
    socChart.data.datasets[0].data = socData;
    socChart.update();

    heaterChart.data.labels = labels;
    heaterChart.data.datasets[0].data = heaterData;
    heaterChart.update();
}

// =============================
// UPDATE HEATER STATUS DISPLAY FROM DB ONLY
// =============================
function updateHeaterStatusDisplay(isOn) {
    heaterStatusSpan.textContent = isOn ? "ON" : "OFF";

    heaterStatusSpan.classList.toggle("heater-on", isOn);
    heaterStatusSpan.classList.toggle("heater-off", !isOn);
}

// =============================
// =============================
// INITIALIZE CHARTS
// =============================
function initializeCharts() {
    // =============================
    // TEMPERATURE CHART
    // =============================
    const tempCtx = document.getElementById("tempChart");

    tempChart = new Chart(tempCtx, {
        type: "line",

        data: {
            labels: [],
            datasets: [
                {
                    label: "Inside Temperature",
                    data: [],
                    borderColor: "red",
                    backgroundColor: "rgba(255,0,0,0.1)",
                    tension: 0.3,
                    fill: true
                },
                {
                    label: "Outside Temperature",
                    data: [],
                    borderColor: "blue",
                    backgroundColor: "rgba(0,0,255,0.1)",
                    tension: 0.3,
                    fill: true
                }
            ]
        },

        options: {
            responsive: true,
            maintainAspectRatio: false,

            plugins: {
                legend: {
                    labels: {
                        color: "white",
                        font: {
                            weight: "bold"
                        }
                    }
                }
            },

            scales: {
                x: {
                    title: {
                        display: true,
                        text: "Time (HH:MM:SS)",
                        color: "white",
                        font: {
                            weight: "bold"
                        }
                    },
                    ticks: {
                        color: "white",
                        font: {
                            weight: "bold"
                        }
                    }
                },
                y: {
                    title: {
                        display: true,
                        text: "Temperature (°C)",
                        color: "white",
                        font: {
                            weight: "bold"
                        }
                    },
                    ticks: {
                        color: "white",
                        font: {
                            weight: "bold"
                        }
                    }
                }
            }
        }
    });

    // =============================
    // SOC CHART
    // =============================
    const socCtx = document.getElementById("socChart");

    socChart = new Chart(socCtx, {
        type: "line",

        data: {
            labels: [],
            datasets: [
                {
                    label: "Battery SOC (%)",
                    data: [],
                    borderColor: "green",
                    backgroundColor: "rgba(0,255,0,0.1)",
                    tension: 0.3,
                    fill: true
                }
            ]
        },

        options: {
            responsive: true,
            maintainAspectRatio: false,

            plugins: {
                legend: {
                    labels: {
                        color: "white",
                        font: {
                            weight: "bold"
                        }
                    }
                }
            },

            scales: {
                x: {
                    title: {
                        display: true,
                        text: "Time (HH:MM:SS)",
                        color: "white",
                        font: {
                            weight: "bold"
                        }
                    },
                    ticks: {
                        color: "white",
                        font: {
                            weight: "bold"
                        }
                    }
                },
                y: {
                    title: {
                        display: true,
                        text: "State of Charge (%)",
                        color: "white",
                        font: {
                            weight: "bold"
                        }
                    },
                    ticks: {
                        color: "white",
                        font: {
                            weight: "bold"
                        }
                    },
                    min: 0,
                    max: 100
                }
            }
        }
    });

    // =============================
    // HEATER STATUS CHART
    // =============================
    const heaterCtx = document.getElementById("heaterChart");

    heaterChart = new Chart(heaterCtx, {
        type: "line",

        data: {
            labels: [],
            datasets: [
                {
                    label: "Heater Status",
                    data: [],
                    borderColor: "orange",
                    backgroundColor: "rgba(255,165,0,0.1)",
                    stepped: true,
                    fill: true
                }
            ]
        },

        options: {
            responsive: true,
            maintainAspectRatio: false,

            plugins: {
                legend: {
                    labels: {
                        color: "white",
                        font: {
                            weight: "bold"
                        }
                    }
                }
            },

            scales: {
                x: {
                    title: {
                        display: true,
                        text: "Time (HH:MM:SS)",
                        color: "white",
                        font: {
                            weight: "bold"
                        }
                    },
                    ticks: {
                        color: "white",
                        font: {
                            weight: "bold"
                        }
                    }
                },
                y: {
                    title: {
                        display: true,
                        text: "Heater Status",
                        color: "white",
                        font: {
                            weight: "bold"
                        }
                    },
                    min: 0,
                    max: 1,

                    ticks: {
                        color: "white",
                        font: {
                            weight: "bold"
                        },
                        stepSize: 1,

                        callback: function(value) {
                            return value === 1 ? "ON" : "OFF";
                        }
                    }
                }
            }
        }
    });

    return true;
}

// =============================
// TELEMETRY WARNING POPUP
// =============================
function checkTelemetryTimeout(dbTimestamp) {
    if (!dbTimestamp) {
        return;
    }

    const currentTime = new Date();
    const telemetryTime = new Date(dbTimestamp);

    const differenceMinutes = (currentTime - telemetryTime) / (1000 * 60);

    let warningPopup = document.getElementById("telemetryWarningPopup");

    if (differenceMinutes > 3) {
        if (!warningPopup) {
            warningPopup = document.createElement("div");

            warningPopup.id = "telemetryWarningPopup";
            warningPopup.textContent = "⚠️ Communication Link Broken";

            warningPopup.style.position = "fixed";
            warningPopup.style.top = "20px";
            warningPopup.style.right = "20px";
            warningPopup.style.zIndex = "99999";

            warningPopup.style.background = "#7f1d1d";
            warningPopup.style.color = "#fecaca";
            warningPopup.style.border = "2px solid #ef4444";
            warningPopup.style.padding = "12px 18px";
            warningPopup.style.borderRadius = "12px";
            warningPopup.style.fontWeight = "800";
            warningPopup.style.fontSize = "0.8rem";
            warningPopup.style.boxShadow = "0 0 15px rgba(239,68,68,0.45)";

            document.body.appendChild(warningPopup);
        }

    } else {
        if (warningPopup) {
            warningPopup.remove();
        }
    }
}

// =============================
// LOAD TELEMETRY DATA
// =============================
async function loadTelemetryData() {
    try {
        const response = await fetch(`${API_BASE_URL}/data`);

        if (!response.ok) {
            throw new Error("Server not responding");
        }

        const data = await response.json();

        checkTelemetryTimeout(data.timestamp);

        if (data.message) {
            commStatusSpan.textContent = "Waiting";
            return;
        }

        commStatusSpan.textContent = "Connected";
        updateLastUpdateTime();

        // =============================
        // UPDATE MONITORING DISPLAYS
        // =============================
        insideTempSpan.textContent = `${data.temp_inside} °C`;
        outsideTempSpan.textContent = `${data.temp_outside} °C`;
        batteryVoltageSpan.textContent = `${data.voltage} V`;
        socSpan.textContent = `${data.state_of_charge} %`;
        rssiValueSpan.textContent = `${data.rssi} dBm`;

        // =============================
        // UPDATE CHARTS
        // This now only updates when a new DB row arrives.
        // =============================
        updateCharts(data);

        // =============================
        // UPDATE SYSTEM STATUS DISPLAY FROM DB
        // =============================
        const dbPowerMode = data.power_mode || "Normal";
        powerModeSpan.textContent = dbPowerMode;

        // =============================
        // UPDATE HEATER STATUS DISPLAY FROM DB
        // =============================
        updateHeaterStatusDisplay(data.heater_status === true);

        // =============================
        // CONTROL PANEL DISABLE/ENABLE BASED ON DB POWER MODE
        // =============================
        if (dbPowerMode === "LOW") {
            setControlPanelDisabled(true);
            currentPowerMode = "Low Power";
        } else {
            setControlPanelDisabled(false);
            currentPowerMode = "Normal";
        }

        // =============================
        // CHECK PENDING COMMANDS
        // =============================
        if (pendingCommand) {
            if (matchesPendingCommand(data)) {
                pendingCommand = null;
                showToast("Command executed successfully.");
            }
        }

    } catch (error) {
        commStatusSpan.textContent = "Disconnected";
        console.error("Failed to fetch data:", error.message);
    }
}

// =============================
// SHUTDOWN CONFIRMATION
// =============================
function showShutdownConfirmation() {
    const existingModal = document.getElementById("shutdown-confirmation");

    if (existingModal) {
        existingModal.remove();
    }

    const modal = document.createElement("div");
    modal.id = "shutdown-confirmation";
    modal.className = "shutdown-modal";

    modal.innerHTML = `
        <div class="shutdown-dialog">
            <div class="shutdown-icon">🔴⚠️🔴</div>
            <div class="shutdown-title">SYSTEM SHUTDOWN</div>
            <div class="shutdown-message">
                This action will shut down the entire system.<br>
                <strong>This cannot be undone.</strong>
            </div>
            <div class="shutdown-actions">
                <button id="confirm-shutdown" class="shutdown-confirm-btn">⚠️ YES, SHUTDOWN</button>
                <button id="cancel-shutdown" class="shutdown-cancel-btn">Cancel</button>
            </div>
        </div>
    `;

    document.body.appendChild(modal);

    const confirmBtn = document.getElementById("confirm-shutdown");
    const cancelBtn = document.getElementById("cancel-shutdown");

    confirmBtn.addEventListener("click", () => {
        modal.remove();
        executeShutdown();
    });

    cancelBtn.addEventListener("click", () => {
        modal.remove();
        showToast("Shutdown cancelled.");
    });

    modal.addEventListener("click", (event) => {
        if (event.target === modal) {
            modal.remove();
            showToast("Shutdown cancelled.");
        }
    });
}

// =============================
// EXECUTE SHUTDOWN
// =============================
async function executeShutdown() {
    pendingCommand = {
        type: "system",
        value: "SHUTDOWN"
    };

    powerModeSpan.textContent = "Shutdown";

    updateLastUpdateTime();

    heaterOnOption.disabled = true;
    heaterOffOption.disabled = true;
    heaterSwitch.classList.add("disabled-switch");

    btnApplyMode.disabled = true;
    btnApplyMode.classList.add("disabled-btn");

    btnShutdown.disabled = true;
    btnShutdown.classList.add("disabled-btn");

    showToast("System shutdown initiated.");

    clearInterval(telemetryInterval);

    console.log("⚠️ SYSTEM SHUTDOWN INITIATED ⚠️");

    const ok = await sendCommand("system", "SHUTDOWN");

    if (!ok) {
        pendingCommand = null;
        loadTelemetryData();
    }
}

// =============================
// EVENT LISTENERS
// =============================
heaterOnOption.addEventListener("click", () => {
    if (heaterOnOption.disabled) {
        return;
    }

    showHeaterConfirmation(true);
});

heaterOffOption.addEventListener("click", () => {
    if (heaterOffOption.disabled) {
        return;
    }

    showHeaterConfirmation(false);
});

btnApplyMode.addEventListener("click", () => {
    if (btnApplyMode.disabled) {
        return;
    }

    const selectedMode = powerModeSelect.value;
    applyPowerMode(selectedMode);
});

btnShutdown.addEventListener("click", () => {
    if (btnShutdown.disabled) {
        return;
    }

    showShutdownConfirmation();
});

btnDownloadCsv.addEventListener("click", () => {
    downloadDatabaseExport();
});

// =============================
// INITIAL LOAD
// =============================
updateControlPanelHeaterButtons(false);

const chartsInitialized = initializeCharts();

if (chartsInitialized) {
    console.log("Charts initialized successfully");
    loadTelemetryData();
    telemetryInterval = setInterval(loadTelemetryData, 5000);
} else {
    console.error("Failed to initialize charts");
    loadTelemetryData();
    telemetryInterval = setInterval(loadTelemetryData, 5000);
}