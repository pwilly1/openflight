const state = {
  telemetry: {},
  temperatureHistory: [],
  batteryHistory: [],
  lastSample: {},
  busy: false,
};

const $ = (id) => document.getElementById(id);
const scenarioNames = {
  standby: "Standby",
  nominal: "Nominal acquisition",
  "corrupted-packet": "Corrupted packet",
  "low-battery": "Low-battery emergency",
  "frozen-sensor": "Frozen sensor",
  "communication-outage": "Communication outage",
};

function number(value, divisor = 1, digits = 0) {
  return typeof value === "number" ? (value / divisor).toFixed(digits) : "—";
}

function setText(id, value) {
  $(id).textContent = value;
}

function missionTime(value) {
  return new Date(value);
}

function setBusy(busy, message = "") {
  state.busy = busy;
  document.querySelectorAll("button").forEach((button) => { button.disabled = busy; });
  if (message) {
    const status = $("actionStatus");
    status.textContent = message;
    status.className = "action-status";
  }
}

function showAction(message, kind = "success") {
  const status = $("actionStatus");
  status.textContent = message;
  status.className = `action-status ${kind}`;
}

async function post(path, payload = {}) {
  const response = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || "Ground command failed");
  return data;
}

async function runAction(path, payload, progress, complete) {
  if (state.busy) return;
  setBusy(true, progress);
  try {
    await post(path, payload);
    showAction(complete);
    await refresh();
  } catch (error) {
    showAction(error.message, "error");
  } finally {
    setBusy(false);
  }
}

function updateConnection(data) {
  const pill = $("connectionPill");
  pill.classList.toggle("online", data.connected);
  pill.classList.toggle("offline", !data.connected);
  setText("connectionText", data.connected ? "F Prime link online" : "Ground link reconnecting");
}

function updateOverview(data) {
  const mode = data.mode || "UNKNOWN";
  const modeLabel = mode === "SAFE" ? "Safe mode" : mode === "NOMINAL" ? "Nominal" : "Awaiting link";
  setText("modeText", modeLabel);
  setText("modeBadge", mode);
  $("modeBadge").className = `mode-badge ${mode.toLowerCase()}`;
  setText("activeScenario", scenarioNames[data.active_scenario] || data.active_scenario);
  setText("faultState", data.fault);
  setText("collectionState", state.telemetry.CollectionEnabled === 1 ? "Active" : state.telemetry.CollectionEnabled === 0 ? "Stopped" : "Unknown");
  setText("healthySamples", state.telemetry.HealthySamples ?? "—");

  const narrative = mode === "SAFE"
    ? `Fault response is active: ${data.fault}. Restore healthy sensor values before requesting operator-approved recovery.`
    : state.telemetry.CollectionEnabled === 1
      ? "Sensor collection is active. The flight computer is receiving and validating simulated spacecraft data."
      : "Flight software is connected and standing by for a mission scenario or collection command.";
  setText("missionNarrative", narrative);

  document.querySelectorAll(".scenario-card").forEach((card) => {
    card.classList.toggle("active", card.dataset.scenario === data.active_scenario);
  });
}

function updateTelemetry(data) {
  state.telemetry = data.telemetry || {};
  const telemetry = state.telemetry;
  setText("batteryValue", number(telemetry.SensorBattery, 1000, 2));
  setText("temperatureValue", number(telemetry.SensorTemperature, 100, 2));
  setText("rollValue", number(telemetry.SensorRoll, 100, 2));
  setText("pitchValue", number(telemetry.SensorPitch, 100, 2));
  setText("yawValue", number(telemetry.SensorYaw, 100, 2));
  setText("packetValue", telemetry.SensorPackets ?? "—");
  setText("badPacketValue", telemetry.BadPackets ?? "—");

  const battery = telemetry.SensorBattery;
  const temperature = telemetry.SensorTemperature;
  if (typeof battery === "number") {
    $("batteryBar").style.width = `${Math.max(0, Math.min(100, (battery - 3000) / 12))}%`;
    const low = battery < 3300;
    setText("batteryFlag", low ? "Critical" : "Nominal");
    $("batteryFlag").className = `metric-flag ${low ? "alert" : "nominal"}`;
  }
  if (typeof temperature === "number") {
    $("temperatureBar").style.width = `${Math.max(0, Math.min(100, (temperature / 100 + 10) * 1.25))}%`;
  }

  const attitude = [telemetry.SensorRoll, telemetry.SensorPitch, telemetry.SensorYaw];
  setText("attitudeSummary", `R ${number(attitude[0], 100, 1)}° / P ${number(attitude[1], 100, 1)}° / Y ${number(attitude[2], 100, 1)}°`);
  if (typeof telemetry.SensorYaw === "number") {
    $("spacecraft").style.setProperty("--spacecraft-angle", `${telemetry.SensorYaw / 100}deg`);
  }

  if (data.last_update) {
    const parsed = missionTime(data.last_update);
    setText("lastUpdate", Number.isNaN(parsed.valueOf()) ? data.last_update : `Last update ${parsed.toLocaleTimeString()}`);
  }

  if (temperature !== state.lastSample.temperature || battery !== state.lastSample.battery) {
    if (typeof temperature === "number") state.temperatureHistory.push(temperature / 100);
    if (typeof battery === "number") state.batteryHistory.push(battery / 1000);
    state.temperatureHistory = state.temperatureHistory.slice(-60);
    state.batteryHistory = state.batteryHistory.slice(-60);
    state.lastSample = { temperature, battery };
    drawChart();
  }
}

function drawSeries(context, values, width, height, min, max, color) {
  if (values.length < 2) return;
  context.beginPath();
  values.forEach((value, index) => {
    const x = (index / Math.max(1, values.length - 1)) * width;
    const y = height - ((value - min) / Math.max(0.001, max - min)) * height;
    if (index === 0) context.moveTo(x, y); else context.lineTo(x, y);
  });
  context.strokeStyle = color;
  context.lineWidth = 2;
  context.stroke();
}

function drawChart() {
  const canvas = $("trendChart");
  const bounds = canvas.getBoundingClientRect();
  const scale = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.floor(bounds.width * scale));
  canvas.height = Math.max(1, Math.floor(bounds.height * scale));
  const context = canvas.getContext("2d");
  context.scale(scale, scale);
  const width = bounds.width;
  const height = bounds.height;
  context.clearRect(0, 0, width, height);
  context.strokeStyle = "rgba(198, 215, 204, 0.10)";
  context.lineWidth = 1;
  for (let index = 1; index < 5; index += 1) {
    const y = (height / 5) * index;
    context.beginPath(); context.moveTo(0, y); context.lineTo(width, y); context.stroke();
  }
  const temperatures = state.temperatureHistory;
  const batteries = state.batteryHistory;
  const tempMin = Math.min(20, ...temperatures) - 1;
  const tempMax = Math.max(30, ...temperatures) + 1;
  drawSeries(context, temperatures, width, height, tempMin, tempMax, "#d6a35d");
  drawSeries(context, batteries, width, height, 3.0, 4.2, "#8fbb8d");
}

function updateEvents(events = []) {
  setText("eventCount", `${events.length} event${events.length === 1 ? "" : "s"}`);
  const timeline = $("eventTimeline");
  if (!events.length) {
    timeline.innerHTML = '<div class="empty-events">Waiting for flight-computer events…</div>';
    return;
  }
  timeline.replaceChildren(...events.map((event) => {
    const row = document.createElement("div");
    row.className = `event-row ${String(event.severity).includes("WARNING") ? "warning" : "activity"}`;
    const eventTime = missionTime(event.time);
    const time = Number.isNaN(eventTime.valueOf()) ? event.time.slice(11, 19) : eventTime.toLocaleTimeString();
    row.innerHTML = `
      <span class="event-time">${time}</span>
      <span class="event-marker" aria-hidden="true"></span>
      <div><span class="event-name"></span><div class="event-source"></div></div>
      <div class="event-copy"><p class="event-message"></p></div>`;
    row.querySelector(".event-name").textContent = event.name;
    row.querySelector(".event-source").textContent = event.source;
    row.querySelector(".event-message").textContent = event.message;
    return row;
  }));
}

function render(data) {
  updateConnection(data);
  updateTelemetry(data);
  updateOverview(data);
  updateEvents(data.events);
}

async function refresh() {
  try {
    const response = await fetch("/api/state", { cache: "no-store" });
    if (!response.ok) throw new Error("Dashboard bridge unavailable");
    render(await response.json());
  } catch (error) {
    $("connectionPill").className = "status-pill offline";
    setText("connectionText", "Dashboard bridge offline");
  }
}

document.querySelectorAll("[data-command]").forEach((button) => {
  button.addEventListener("click", () => runAction(
    "/api/command",
    { command: button.dataset.command },
    `Dispatching ${button.dataset.command}…`,
    `${button.dataset.command} dispatched to F Prime.`,
  ));
});

document.querySelectorAll("[data-scenario]").forEach((button) => {
  button.addEventListener("click", () => runAction(
    "/api/scenario",
    { scenario: button.dataset.scenario },
    `Starting ${scenarioNames[button.dataset.scenario]}…`,
    `${scenarioNames[button.dataset.scenario]} stimulus dispatched.`,
  ));
});

$("recoverButton").addEventListener("click", () => runAction(
  "/api/recover",
  {},
  "Restoring sensors and collecting three healthy samples…",
  "Recovery sequence dispatched. Confirming spacecraft state…",
));

window.addEventListener("resize", drawChart);
refresh();
setInterval(refresh, 1000);
