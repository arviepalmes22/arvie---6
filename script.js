
import { initializeApp } from
  "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";

import {
  getDatabase,
  ref,
  onValue
} from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";

// =====================================================
// ARVIE FIREBASE CONFIGURATION
// =====================================================

const firebaseConfig = {
  apiKey: "AIzaSyDQLkZt4-fYHwlQhV8KGmJAJ8y2n5JtqOo",
  authDomain: "arviee-7474c.firebaseapp.com",
  databaseURL: "https://arviee-7474c-default-rtdb.europe-west1.firebasedatabase.app",
  projectId: "arviee-7474c",
  storageBucket: "arviee-7474c.firebasestorage.app",
  messagingSenderId: "909039910380",
  appId: "1:909039910380:web:c3d84fe46bbdbf02ded4c9",
  measurementId: "G-4H4Y36L548"
};

// =====================================================
// FIREBASE INITIALIZATION
// =====================================================

const firebaseApp = initializeApp(firebaseConfig);
const database = getDatabase(firebaseApp);
const dataRef = ref(database, "ESP32_Data");

// =====================================================
// DOM ELEMENTS
// =====================================================

const statusText = document.getElementById("statusText");
const statusDot = document.getElementById("statusDot");

const currentTemperature =
  document.getElementById("currentTemperature");

const currentHumidity =
  document.getElementById("currentHumidity");

const recordCount =
  document.getElementById("recordCount");

const graphDate =
  document.getElementById("graphDate");

const historyTableBody =
  document.getElementById("historyTableBody");

const historyRecordCount =
  document.getElementById("historyRecordCount");

const toggleHistory =
  document.getElementById("toggleHistory");

const historyContent =
  document.getElementById("historyContent");

const led1Status = document.getElementById("led1Status");
const led2Status = document.getElementById("led2Status");

const led1Button = document.getElementById("led1Button");
const led2Button = document.getElementById("led2Button");

const relayMessage = document.getElementById("relayMessage");

// =====================================================
// VARIABLES
// =====================================================

let allSensorData = {};
let selectedDate = "";
let sensorChart = null;

let relay1State = false;
let relay2State = false;

// =====================================================
// FIREBASE STATUS
// =====================================================

function showConnected() {
  statusText.textContent = "ONLINE";
  statusDot.style.background = "#22d3ee";
}

function showOffline() {
  statusText.textContent = "OFFLINE";
  statusDot.style.background = "#64748b";
}

// =====================================================
// NUMBER HELPERS
// =====================================================

function toNumber(value) {
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

function formatNumber(value) {
  if (value === null || value === undefined) {
    return "--";
  }

  return Number(value).toFixed(1);
}

// =====================================================
// DATE LIST
// =====================================================

function getDateList(data) {
  return Object.keys(data || {})
    .filter(key =>
      data[key] &&
      typeof data[key] === "object"
    )
    .sort()
    .reverse();
}

// =====================================================
// TOTAL RECORDS
// =====================================================

function getTotalRecords(data) {
  let total = 0;

  Object.keys(data || {}).forEach(date => {
    const dayData = data[date];

    if (!dayData || typeof dayData !== "object") {
      return;
    }

    Object.keys(dayData).forEach(time => {
      const reading = dayData[time];

      if (!reading || typeof reading !== "object") {
        return;
      }

      const temperature = toNumber(reading.temperature);
      const humidity = toNumber(reading.humidity);

      if (temperature !== null || humidity !== null) {
        total++;
      }
    });
  });

  return total;
}

// =====================================================
// LATEST READING
// =====================================================

function getLatestReading(data) {
  let latest = null;

  const dates = Object.keys(data || {}).sort();

  for (const date of dates) {
    const times = Object.keys(data[date] || {}).sort();

    for (const time of times) {
      const reading = data[date][time];

      if (!reading || typeof reading !== "object") {
        continue;
      }

      const temperature = toNumber(reading.temperature);
      const humidity = toNumber(reading.humidity);

      if (temperature === null && humidity === null) {
        continue;
      }

      latest = {
        date,
        time,
        temperature,
        humidity
      };
    }
  }

  return latest;
}

// =====================================================
// READINGS FOR SELECTED DATE
// =====================================================

function getReadingsForDate(date) {
  const result = [];

  if (!date) return result;

  const dayData = allSensorData[date];

  if (!dayData || typeof dayData !== "object") {
    return result;
  }

  Object.keys(dayData).sort().forEach(time => {
    const reading = dayData[time];

    if (!reading || typeof reading !== "object") {
      return;
    }

    const temperature = toNumber(reading.temperature);
    const humidity = toNumber(reading.humidity);

    if (temperature === null && humidity === null) {
      return;
    }

    result.push({
      time,
      temperature,
      humidity
    });
  });

  return result;
}

// =====================================================
// DATE SELECTOR
// =====================================================

function populateDateSelect() {
  const dates = getDateList(allSensorData);

  graphDate.innerHTML = "";

  if (dates.length === 0) {
    const option = document.createElement("option");
    option.value = "";
    option.textContent = "No dates available";
    graphDate.appendChild(option);
    return;
  }

  dates.forEach(date => {
    const option = document.createElement("option");
    option.value = date;
    option.textContent = date;
    graphDate.appendChild(option);
  });

  graphDate.value = selectedDate || dates[0];
}

// =====================================================
// CURRENT SENSOR VALUES
// =====================================================

function updateCurrentReading() {
  const latest = getLatestReading(allSensorData);

  if (!latest) {
    currentTemperature.textContent = "-- °C";
    currentHumidity.textContent = "-- %";
    return;
  }

  currentTemperature.textContent =
    formatNumber(latest.temperature) + " °C";

  currentHumidity.textContent =
    formatNumber(latest.humidity) + " %";

  console.log("LATEST SENSOR READING:", latest);
}

// =====================================================
// SENSOR GRAPH
// =====================================================

function updateChart() {
  if (typeof Chart === "undefined") {
    console.error("Chart.js is not loaded.");
    return;
  }

  const date = graphDate.value || selectedDate;
  const readings = getReadingsForDate(date);

  const labels = readings.map(item => item.time);
  const temperatures = readings.map(item => item.temperature);
  const humidities = readings.map(item => item.humidity);

  const canvas = document.getElementById("sensorChart");

  if (!canvas) return;

  if (sensorChart) {
    sensorChart.destroy();
    sensorChart = null;
  }

  sensorChart = new Chart(canvas, {
    type: "line",

    data: {
      labels,

      datasets: [
        {
          label: "Temperature (°C)",
          data: temperatures,
          borderColor: "#06b6d4",
          backgroundColor: "rgba(6, 182, 212, 0.12)",
          yAxisID: "temperature",
          tension: 0.3,
          borderWidth: 3,
          pointRadius: 4,
          pointBackgroundColor: "#22d3ee"
        },
        {
          label: "Humidity (%)",
          data: humidities,
          borderColor: "#a78bfa",
          backgroundColor: "rgba(167, 139, 250, 0.12)",
          yAxisID: "humidity",
          tension: 0.3,
          borderWidth: 3,
          pointRadius: 4,
          pointBackgroundColor: "#c4b5fd"
        }
      ]
    },

    options: {
      responsive: true,
      maintainAspectRatio: false,

      animation: {
        duration: 300
      },

      interaction: {
        mode: "index",
        intersect: false
      },

      plugins: {
        legend: {
          labels: {
            color: "#cbd5e1"
          }
        }
      },

      scales: {
        x: {
          ticks: {
            color: "#64748b"
          },
          grid: {
            color: "#1f2937"
          }
        },

        temperature: {
          type: "linear",
          position: "left",
          title: {
            display: true,
            text: "Temperature (°C)",
            color: "#06b6d4"
          },
          ticks: {
            color: "#64748b"
          },
          grid: {
            color: "#1f2937"
          }
        },

        humidity: {
          type: "linear",
          position: "right",
          title: {
            display: true,
            text: "Humidity (%)",
            color: "#a78bfa"
          },
          ticks: {
            color: "#64748b"
          },
          grid: {
            drawOnChartArea: false
          }
        }
      }
    }
  });

  console.log("GRAPH UPDATED:", date, readings.length, "records");
}

// =====================================================
// SENSOR HISTORY
// =====================================================

function updateHistory() {
  if (!historyTableBody) return;

  const date = graphDate.value || selectedDate;
  const readings = getReadingsForDate(date);

  historyTableBody.innerHTML = "";

  if (readings.length === 0) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");

    cell.colSpan = 3;
    cell.textContent = "No sensor data available.";

    row.appendChild(cell);
    historyTableBody.appendChild(row);

    historyRecordCount.textContent = "0 records";
    return;
  }

  readings.slice().reverse().forEach(reading => {
    const row = document.createElement("tr");

    const timeCell = document.createElement("td");
    const temperatureCell = document.createElement("td");
    const humidityCell = document.createElement("td");

    timeCell.textContent = reading.time;

    temperatureCell.textContent =
      formatNumber(reading.temperature) + " °C";

    humidityCell.textContent =
      formatNumber(reading.humidity) + " %";

    row.appendChild(timeCell);
    row.appendChild(temperatureCell);
    row.appendChild(humidityCell);

    historyTableBody.appendChild(row);
  });

  historyRecordCount.textContent =
    readings.length +
    (readings.length === 1 ? " record" : " records");
}

// =====================================================
// UPDATE DASHBOARD
// =====================================================

function updateDashboard() {
  const dates = getDateList(allSensorData);

  if (dates.length === 0) {
    currentTemperature.textContent = "-- °C";
    currentHumidity.textContent = "-- %";
    recordCount.textContent = "0";

    populateDateSelect();
    updateChart();
    updateHistory();
    return;
  }

  if (!selectedDate || !dates.includes(selectedDate)) {
    selectedDate = dates[0];
  }

  recordCount.textContent = getTotalRecords(allSensorData);

  populateDateSelect();
  updateCurrentReading();
  updateChart();
  updateHistory();
}

// =====================================================
// DATE FILTER - GRAPH AND HISTORY
// =====================================================

graphDate.addEventListener("change", function () {
  selectedDate = this.value;
  updateChart();
  updateHistory();
});

// =====================================================
// SHOW / HIDE HISTORY
// =====================================================

toggleHistory.addEventListener("click", function () {
  const isHidden = historyContent.classList.contains("hidden");

  if (isHidden) {
    historyContent.classList.remove("hidden");
    toggleHistory.textContent = "Hide History";
    updateHistory();
  } else {
    historyContent.classList.add("hidden");
    toggleHistory.textContent = "Show History";
  }
});

// =====================================================
// FIREBASE LISTENER
// =====================================================

console.log("=================================");
console.log("ARVIE ACTIVITY 6");
console.log("Connecting to Firebase...");
console.log("Database path: /ESP32_Data");
console.log("=================================");

showOffline();

onValue(
  dataRef,

  snapshot => {
    console.log("FIREBASE DATA RECEIVED");

    allSensorData = snapshot.val() || {};

    showConnected();
    updateDashboard();
  },

  error => {
    console.error("FIREBASE READ ERROR:", error);
    showOffline();
  }
);

// =====================================================
// RELAY UI
// =====================================================

function updateRelayUI(relay, state) {
  const status = relay === 1 ? led1Status : led2Status;
  const button = relay === 1 ? led1Button : led2Button;

  status.textContent = state ? "ON" : "OFF";
  status.className = "relay-status " + (state ? "on" : "off");

  button.textContent = state ? "TURN OFF" : "TURN ON";
  button.className = "relay-button " + (state ? "on" : "off");
}

function updateRelayStates(data) {
  relay1State = Boolean(data.LED1);
  relay2State = Boolean(data.LED2);

  updateRelayUI(1, relay1State);
  updateRelayUI(2, relay2State);
}

// =====================================================
// GET RELAY STATUS FROM ESP32
// =====================================================

async function refreshRelayStatus() {
  try {
    const response = await fetch("/relay-status", {
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error("Cannot get relay status");
    }

    const data = await response.json();

    updateRelayStates(data);
    relayMessage.textContent = "ESP32 relay connection is active.";
  } catch (error) {
    console.error("RELAY STATUS ERROR:", error);
    relayMessage.textContent = "Cannot communicate with ESP32 relay controls.";
  }
}

// =====================================================
// CONTROL RELAY
// =====================================================

async function controlRelay(relay, turnOn) {
  const button = relay === 1 ? led1Button : led2Button;

  button.disabled = true;
  relayMessage.textContent = "Sending relay command...";

  try {
    const state = turnOn ? "on" : "off";

    const response = await fetch(
      `/relay?relay=${relay}&state=${state}`,
      { cache: "no-store" }
    );

    if (!response.ok) {
      throw new Error("Relay command failed");
    }

    const result = await response.json();

    if (!result.success) {
      throw new Error(result.message || "Relay command rejected");
    }

    if (relay === 1) {
      relay1State = turnOn;
    } else {
      relay2State = turnOn;
    }

    updateRelayUI(relay, turnOn);

    relayMessage.textContent =
      `Relay ${relay} successfully turned ${state.toUpperCase()}.`;

  } catch (error) {
    console.error("RELAY CONTROL ERROR:", error);
    relayMessage.textContent =
      "Relay command failed. Check ESP32 WiFi connection.";

    refreshRelayStatus();
  } finally {
    button.disabled = false;
  }
}

// =====================================================
// RELAY BUTTON EVENTS
// =====================================================

led1Button.addEventListener("click", () => {
  controlRelay(1, !relay1State);
});

led2Button.addEventListener("click", () => {
  controlRelay(2, !relay2State);
});

// =====================================================
// INITIAL RELAY STATUS + PERIODIC REFRESH
// =====================================================

refreshRelayStatus();
setInterval(refreshRelayStatus, 3000);
