/**
 * app.js — Dashboard CNC PCB Monitor
 *
 * Placeholders reemplazados por 03_frontend_hosting.sh al momento del despliegue:
 *   __API_BASE_URL__     → URL base del Function App (ej: https://cnc-iot-func.azurewebsites.net/api)
 *   __API_FUNCTION_KEY__ → Clave de acceso para funciones con authLevel: function
 */

const API_BASE_URL     = "__API_BASE_URL__";
const API_FUNCTION_KEY = "__API_FUNCTION_KEY__";
const REFRESH_INTERVAL_MS = 15_000; // Actualización automática cada 15 segundos

// Umbrales locales para colorear tarjetas (coinciden con los valores por defecto del backend)
const THRESHOLDS = {
  tempMin:   15.0,
  tempMax:   45.0,
  humMin:    20.0,
  humMax:    80.0,
  vibAnom:    0.80,
};

// ── Configuración del nodo de cámara ─────────────────────────────────────────
const CAMERA_DEVICE_ID = "cnc_camera_01";

const PCB_CLASSES = ["PCB_Mixta", "PCB_SMD", "PCB_TH", "Sin_PCB"];

const PCB_CLASS_COLORS = {
  PCB_Mixta: "#f59e0b",
  PCB_SMD:   "#3b82f6",
  PCB_TH:    "#10b981",
  Sin_PCB:   "#6b7280",
};

const PCB_CLASS_ICONS = {
  PCB_Mixta: "🔀",
  PCB_SMD:   "🔲",
  PCB_TH:    "🔌",
  Sin_PCB:   "⬜",
};

let refreshTimer = null;

// ---------------------------------------------------------------------------
// Inicio
// ---------------------------------------------------------------------------
document.addEventListener("DOMContentLoaded", () => {
  fetchData();
  fetchCameraData();
  refreshTimer = setInterval(fetchData, REFRESH_INTERVAL_MS);
  setInterval(fetchCameraData, REFRESH_INTERVAL_MS);
});

// ---------------------------------------------------------------------------
// Construcción de URLs con clave de función
// ---------------------------------------------------------------------------
function buildUrl(path, params = {}) {
  const url = new URL(`${API_BASE_URL}/${path}`);
  if (API_FUNCTION_KEY) {
    url.searchParams.set("code", API_FUNCTION_KEY);
  }
  Object.entries(params).forEach(([k, v]) => {
    if (v !== null && v !== undefined && v !== "") {
      url.searchParams.set(k, v);
    }
  });
  return url.toString();
}

// ---------------------------------------------------------------------------
// Carga de datos
// ---------------------------------------------------------------------------
async function fetchData() {
  const limit    = document.getElementById("sel-limit").value;
  const deviceId = document.getElementById("inp-device").value.trim();

  setStatus("connecting");

  try {
    const res = await fetch(buildUrl("datos", { limit, device_id: deviceId }));
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();

    renderMetrics(data);
    renderTable(data);
    setStatus("ok");
    document.getElementById("last-update").textContent =
      `Actualizado: ${new Date().toLocaleTimeString()}`;
  } catch (err) {
    console.error("Error al obtener datos:", err);
    setStatus("error");
    document.getElementById("last-update").textContent = "Error de conexión";
  }
}

// ---------------------------------------------------------------------------
// Renderizado de métricas (tarjetas superiores — nodo principal)
// ---------------------------------------------------------------------------
function renderMetrics(data) {
  if (!data || data.length === 0) {
    ["temp", "hum", "vib", "anom-vib", "alert"].forEach((id) => {
      setText(`val-${id}`, "—");
      setBadge(`badge-${id}`, "", "");
      setCardState(`card-${id}`, "");
    });
    hideAlertReasons();
    return;
  }

  const latest     = data[0];
  const sensors    = latest.sensors    || {};
  const predictions = latest.predictions || {};
  const alerts     = latest.alerts     || {};

  // Temperatura
  const temp = parseFloat(sensors.temperature);
  const tempAlert = !isNaN(temp) && (temp < THRESHOLDS.tempMin || temp > THRESHOLDS.tempMax);
  setText("val-temp", isNaN(temp) ? "—" : temp.toFixed(1));
  setCardState("card-temp", tempAlert ? "alert" : "ok");
  setBadge("badge-temp", tempAlert ? "⚠ Fuera de rango" : "✓ Normal", tempAlert ? "warn" : "ok");

  // Humedad
  const hum = parseFloat(sensors.humidity);
  const humAlert = !isNaN(hum) && (hum < THRESHOLDS.humMin || hum > THRESHOLDS.humMax);
  setText("val-hum", isNaN(hum) ? "—" : hum.toFixed(1));
  setCardState("card-hum", humAlert ? "alert" : "ok");
  setBadge("badge-hum", humAlert ? "⚠ Fuera de rango" : "✓ Normal", humAlert ? "warn" : "ok");

  // Estado vibracional
  const vibStatus = (sensors.vibration_status || "—").toLowerCase();
  const vibAlert  = vibStatus === "anomalia";
  setText("val-vib", sensors.vibration_status || "—");
  setCardState("card-vib", vibAlert ? "alert" : "ok");
  setBadge("badge-vib", vibAlert ? "⚠ Anomalía" : "✓ Normal", vibAlert ? "alert" : "ok");

  // Score de anomalía vibracional
  const vibScore = predictions.vibration_anomaly_score;
  const vibScoreAlert = vibScore !== null && vibScore !== undefined && parseFloat(vibScore) >= THRESHOLDS.vibAnom;
  setText("val-anom-vib", vibScore !== null && vibScore !== undefined ? parseFloat(vibScore).toFixed(3) : "—");
  setCardState("card-anom-vib", vibScoreAlert ? "alert" : "ok");
  setBadge("badge-anom-vib", vibScoreAlert ? "⚠ Alto" : "✓ Normal", vibScoreAlert ? "alert" : "ok");

  // Estado de alerta general
  const alertActive = alerts.active === true;
  setText("val-alert", alertActive ? "ALERTA" : "Normal");
  setCardState("card-alert", alertActive ? "alert" : "ok");
  setBadge("badge-alert", alertActive ? "🚨 Activa" : "✓ Inactiva", alertActive ? "alert" : "ok");

  // Panel de motivos de alerta
  if (alertActive && Array.isArray(alerts.reasons) && alerts.reasons.length > 0) {
    showAlertReasons(alerts.reasons);
  } else {
    hideAlertReasons();
  }
}

// ---------------------------------------------------------------------------
// Renderizado de la tabla
// ---------------------------------------------------------------------------
function renderTable(data) {
  const tbody = document.getElementById("table-body");

  if (!data || data.length === 0) {
    tbody.innerHTML = '<tr><td colspan="8" class="table-empty">Sin datos disponibles</td></tr>';
    return;
  }

  tbody.innerHTML = data.map((item) => {
    const s = item.sensors     || {};
    const p = item.predictions || {};
    const a = item.alerts      || {};
    const cam = item.camera    || {};
    const ts = item.timestamp ? new Date(item.timestamp * 1000).toLocaleString() : "—";
    const alertRow = a.active ? "row--alert" : "";

    const vibScore = p.vibration_anomaly_score !== null && p.vibration_anomaly_score !== undefined
      ? parseFloat(p.vibration_anomaly_score).toFixed(3)
      : "—";

    // Columna "Clase PCB": muestra la clase si es documento de cámara; vacío si es del nodo principal
    const pcbClass = item.device_type === "camera"
      ? `${PCB_CLASS_ICONS[cam.pcb_class] || "📷"} ${cam.pcb_class || "—"}`
      : '<span class="cell--dimmed">—</span>';

    return `<tr class="${alertRow}">
      <td>${escapeHtml(ts)}</td>
      <td>${escapeHtml(item.device_id || "—")}</td>
      <td>${s.temperature !== undefined ? parseFloat(s.temperature).toFixed(1) : "—"}</td>
      <td>${s.humidity    !== undefined ? parseFloat(s.humidity).toFixed(1)    : "—"}</td>
      <td class="${(s.vibration_status || "").toLowerCase() === "anomalia" ? "cell--alert" : ""}">
        ${escapeHtml(s.vibration_status || "—")}
      </td>
      <td class="${parseFloat(vibScore) >= THRESHOLDS.vibAnom ? "cell--alert" : ""}">
        ${escapeHtml(vibScore)}
      </td>
      <td>${pcbClass}</td>
      <td>${a.active ? '<span class="badge badge--alert">Alerta</span>' : '<span class="badge badge--ok">Normal</span>'}</td>
    </tr>`;
  }).join("");
}

// ---------------------------------------------------------------------------
// Descarga de CSV
// ---------------------------------------------------------------------------
async function downloadCSV() {
  const deviceId = document.getElementById("inp-device").value.trim();
  const url = buildUrl("datos/csv", { device_id: deviceId });

  try {
    const res = await fetch(url);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const blob = await res.blob();
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = `telemetria_cnc_${Date.now()}.csv`;
    a.click();
    URL.revokeObjectURL(a.href);
  } catch (err) {
    console.error("Error al descargar CSV:", err);
    alert("No se pudo descargar el CSV. Revisa la consola para más detalles.");
  }
}

// ---------------------------------------------------------------------------
// Control de actuador
// ---------------------------------------------------------------------------
async function sendCommand(comando) {
  const responseEl = document.getElementById("actuator-response");
  responseEl.className = "actuator-response actuator-response--pending";
  responseEl.textContent = `Enviando comando ${comando}…`;
  responseEl.classList.remove("hidden");

  try {
    const res = await fetch(buildUrl("actuador"), {
      method:  "POST",
      headers: { "Content-Type": "application/json" },
      body:    JSON.stringify({ comando }),
    });

    const json = await res.json();

    if (res.ok && json.ok) {
      const via = json.delivered || "desconocido";
      responseEl.className = "actuator-response actuator-response--ok";
      responseEl.textContent = `✓ Comando ${comando} entregado (${via})`;
    } else {
      responseEl.className = "actuator-response actuator-response--error";
      responseEl.textContent = `✗ Error: ${json.error || "respuesta inesperada"}`;
    }
  } catch (err) {
    console.error("Error al enviar comando:", err);
    responseEl.className = "actuator-response actuator-response--error";
    responseEl.textContent = `✗ Error de red: ${err.message}`;
  }

  // Ocultar mensaje después de 5 segundos
  setTimeout(() => responseEl.classList.add("hidden"), 5000);
}

// ---------------------------------------------------------------------------
// Carga de datos del nodo de cámara (cnc_camera_01)
// ---------------------------------------------------------------------------
async function fetchCameraData() {
  try {
    const res = await fetch(
      buildUrl("datos", { limit: 1, device_id: CAMERA_DEVICE_ID })
    );
    if (!res.ok) return;
    const data = await res.json();
    if (data && data.length > 0 && data[0].device_type === "camera") {
      renderCameraCard(data[0]);
    }
  } catch (err) {
    console.warn("[CAM] Error al obtener datos de cámara:", err);
  }
}

// ---------------------------------------------------------------------------
// Renderizado del card de clasificación PCB y panel de probabilidades
// ---------------------------------------------------------------------------
function renderCameraCard(item) {
  const cam   = item.camera || {};
  const cls   = cam.pcb_class || null;
  const conf  = cam.confidence != null ? `${(cam.confidence * 100).toFixed(1)}%` : null;
  const probs = cam.probabilities || {};

  // ── Tarjeta de clasificación ──────────────────────────────────────────────
  if (cls) {
    const icon = PCB_CLASS_ICONS[cls] || "📷";
    setText("val-pcb-class", `${icon} ${cls}`);
    setText("unit-pcb-class", conf ? `${conf} confianza` : "");
    setCardState("card-pcb-class", cls === "Sin_PCB" ? "" : "ok");
    setBadge("badge-pcb-class", conf ? `✓ ${conf}` : "✓", "ok");
  } else {
    setText("val-pcb-class", "—");
    setText("unit-pcb-class", "");
    setCardState("card-pcb-class", "");
    setBadge("badge-pcb-class", "", "");
  }

  // ── Panel de barras de probabilidad ──────────────────────────────────────
  const panel = document.getElementById("pcb-panel");
  const bars  = document.getElementById("pcb-bars");
  const meta  = document.getElementById("pcb-meta");

  if (!cls) {
    panel.classList.add("hidden");
    return;
  }

  panel.classList.remove("hidden");

  bars.innerHTML = PCB_CLASSES.map((c) => {
    const val   = probs[c] != null ? probs[c] : 0;
    const pct   = (val * 100).toFixed(1);
    const width = Math.round(val * 100);
    const color = PCB_CLASS_COLORS[c] || "#6b7280";
    const active = c === cls ? "pcb-bar--active" : "";
    return `
      <div class="pcb-bar-row ${active}">
        <span class="pcb-bar-label">${PCB_CLASS_ICONS[c] || ""} ${escapeHtml(c)}</span>
        <div class="pcb-bar-track">
          <div class="pcb-bar-fill" style="width:${width}%;background:${color}"></div>
        </div>
        <span class="pcb-bar-value">${pct}%</span>
      </div>`;
  }).join("");

  const ts = item.timestamp
    ? new Date(item.timestamp * 1000).toLocaleTimeString()
    : "—";
  const infMs = cam.inference_ms != null ? `${cam.inference_ms}ms` : "—";
  meta.textContent =
    `Dispositivo: ${escapeHtml(item.device_id || CAMERA_DEVICE_ID)}`
    + ` | Inferencia: ${infMs}`
    + ` | Modelo: ${escapeHtml(cam.model_version || "—")}`
    + ` | Actualizado: ${ts}`;
}

// ---------------------------------------------------------------------------
// Helpers de UI
// ---------------------------------------------------------------------------
function setStatus(state) {
  const dot   = document.getElementById("status-dot");
  const label = document.getElementById("status-label");
  dot.className = `status-dot status-${state}`;
  label.textContent = { ok: "Conectado", error: "Error", connecting: "Conectando…" }[state] || state;
}

function setText(id, value) {
  const el = document.getElementById(id);
  if (el) el.textContent = value;
}

function setCardState(id, state) {
  const el = document.getElementById(id);
  if (!el) return;
  el.classList.remove("metric-card--ok", "metric-card--alert");
  if (state) el.classList.add(`metric-card--${state}`);
}

function setBadge(id, text, type) {
  const el = document.getElementById(id);
  if (!el) return;
  el.textContent = text;
  el.className = `metric-card__badge${type ? ` badge--${type}` : ""}`;
}

function showAlertReasons(reasons) {
  const panel = document.getElementById("alert-reasons-panel");
  const text  = document.getElementById("alert-reasons-text");
  text.textContent = reasons.join(" | ");
  panel.classList.remove("hidden");
}

function hideAlertReasons() {
  document.getElementById("alert-reasons-panel").classList.add("hidden");
}

function escapeHtml(str) {
  return String(str)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}
