#include <WiFi.h>
#include <WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Configuración del Access Point (AP)
const char* ap_ssid = "esp32";
const char* ap_password = "orangone228";

// Variables para STA
String sta_ssid = "";
String sta_password = "";
bool sta_enabled = false;

// Servidor web en el puerto 80
WebServer server(80);

// Cliente NTP
WiFiUDP ntpUDP;
String ntp_server = "pool.ntp.org";
long ntp_offset = 0; // En segundos, default UTC
unsigned long ntp_update_interval = 60000; // En ms
NTPClient timeClient(ntpUDP, ntp_server.c_str(), ntp_offset, ntp_update_interval);

// Modo de trabajo
bool auto_mode = true; // Por default automático

// Reloj estacional (mes-día, 1-based month)
int seasonal_start_month = 11;
int seasonal_start_day = 1;
int seasonal_end_month = 4;
int seasonal_end_day = 15;

// Reloj horario: 7 días (0=Domingo, 6=Sábado), 2 franjas
struct TimeRange {
  int start_hour;
  int start_min;
  int end_hour;
  int end_min;
};
TimeRange daily_ranges[7][2];

// Inicializar defaults
void initDailyRanges() {
  for (int d = 0; d < 7; d++) {
    daily_ranges[d][0] = {6, 0, 10, 0};
    daily_ranges[d][1] = {17, 0, 23, 0};
  }
}

// Pines
const int PIN_BOMBAS = 4;
const int PIN_QUEMADOR = 5;
const int PIN_VALVULA_CIERRA = 6;
const int PIN_VALVULA_ABRE = 7;

// Estados manuales
bool bomba_on = false;
bool quemador_on = false;
bool valvula_cierra_on = false;
bool valvula_abre_on = false;

// Estado del sistema
bool system_active = false;

// Máquina de estados para secuencia
enum SeqState { IDLE, START_BOMBAS, WAIT_BOMBAS, START_QUEMADOR, WAIT_QUEMADOR, START_VALVULA, ACTIVE };
SeqState seq_state = IDLE;
unsigned long seq_timer = 0;

// Para alternancia de válvula
bool valvula_alternating = false;
bool valvula_state = false; // false: abre, true: cierra
unsigned long valvula_timer = 0;

// Días de la semana
const String days[7] = {"Domingo", "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado"};

// Variables para manejo de tiempo interno
unsigned long last_ntp_update = 0;
time_t last_epoch_time = 0;
bool manual_time_set = false;

// Estilo CSS común para todas las páginas
String getCommonStyle() {
  String style = "<style>";
  style += "body { font-family: 'Arial', sans-serif; margin: 0; padding: 0; background-color: #f4f4f9; color: #333; }";
  style += "header { background-color: #2c3e50; color: white; padding: 10px 0; text-align: center; }";
  style += "header h1 { color: white; }";
  style += "nav { background-color: #34495e; }";
  style += "nav ul { list-style: none; padding: 0; margin: 0; display: flex; justify-content: center; }";
  style += "nav ul li { margin: 0 20px; }";
  style += "nav ul li a { color: white; text-decoration: none; font-size: 18px; padding: 10px 15px; display: block; }";
  style += "nav ul li a:hover, nav ul li a.active { background-color: #1abc9c; border-radius: 5px; }";
  style += ".container { max-width: 800px; margin: 20px auto; padding: 20px; background-color: white; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }";
  style += "h1 { color: #2c3e50; text-align: center; }";
  style += "h2 { color: #34495e; margin-bottom: 10px; }";
  style += ".toggle { margin: 20px 0; display: flex; justify-content: center; align-items: center; }";
  style += "input[type=text], input[type=password], input[type=text], input[type=time], input[type=number], input[type=date] { padding: 8px; margin: 10px 0; width: 200px; border: 1px solid #ccc; border-radius: 4px; }";
  style += "button { padding: 10px 20px; background-color: #1abc9c; color: white; border: none; border-radius: 4px; cursor: pointer; }";
  style += "button:hover { background-color: #16a085; }";
  style += ".status { font-size: 18px; text-align: center; margin: 20px 0; }";
  style += "section { margin: 20px 0; padding: 15px; border: 1px solid #ddd; border-radius: 5px; background-color: #f9f9f9; }";
  style += "section h3 { color: #2c3e50; margin-top: 0; }";
  style += ".switch { position: relative; display: inline-block; width: 60px; height: 34px; }";
  style += ".switch input { opacity: 0; width: 0; height: 0; }";
  style += ".slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #f44336; transition: .4s; border-radius: 34px; }"; // Rojo por default (manual)
  style += ".slider:before { position: absolute; content: ''; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }";
  style += "input:checked + .slider { background-color: #4CAF50; }"; // Verde cuando checked (automático)
  style += "input:checked + .slider:before { transform: translateX(26px); }";
  style += "table { width: 100%; border-collapse: collapse; margin: 10px 0; }";
  style += "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }";
  style += "th { background-color: #f2f2f2; }";
  style += ".inline-section { display: flex; justify-content: space-between; flex-wrap: wrap; gap: 20px; }";
  style += ".inline-section section { flex: 1; min-width: 250px; }";
  style += "@media (max-width: 600px) { nav ul { flex-direction: column; } nav ul li { margin: 10px 0; } .inline-section { flex-direction: column; } }";
  style += "</style>";
  return style;
}

// Menú de navegación
String getNavMenu(String activePage) {
  String menu = "<nav><ul>";
  menu += "<li><a href='/'" + String(activePage == "status" ? " class='active'" : "") + ">Estado</a></li>";
  menu += "<li><a href='/config'" + String(activePage == "config" ? " class='active'" : "") + ">Configuración</a></li>";
  menu += "<li><a href='/manual'" + String(activePage == "manual" ? " class='active'" : "") + ">Manual</a></li>";
  menu += "</ul></nav>";
  return menu;
}

// Página principal: Estado
void handleStatus() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>ESP32-S3 Estado</title>";
  html += getCommonStyle();
  html += "</head><body>";
  html += "<header><h1>ESP32-S3 Web Server</h1></header>";
  html += getNavMenu("status");
  html += "<div class='container'>";
  html += "<h2>Estado</h2>";
  html += "<div class='inline-section'>";
  html += "<section>";
  html += "<h3>Fecha y Hora</h3>";
  String formattedTime = timeClient.getFormattedTime();
  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime((time_t *)&epochTime);
  String formattedDate = String(ptm->tm_year + 1900) + "-" + String(ptm->tm_mon + 1) + "-" + String(ptm->tm_mday);
  html += "<div class='status'>Fecha y Hora: " + formattedDate + " " + formattedTime + " (UTC + " + String(ntp_offset / 3600) + ")</div>";
  html += "</section>";
  html += "<section>";
  html += "<h3>Estado de Conexión STA</h3>";
  if (sta_enabled && WiFi.status() == WL_CONNECTED) {
    html += "<div class='status'>Estado STA: Conectado (IP: " + WiFi.localIP().toString() + ")</div>";
  } else {
    html += "<div class='status'>Estado STA: Desconectado</div>";
  }
  html += "</section>";
  html += "</div>";
  html += "<div class='inline-section'>";
  html += "<section>";
  html += "<h3>Modo de Trabajo</h3>";
  html += "<div class='toggle'>";
  html += "<label class='switch'><input type='checkbox' id='modeToggle' " + String(auto_mode ? "checked" : "") + " onchange='toggleMode()'><span class='slider'></span></label>";
  html += "<span style='margin-left: 10px;'>" + String(auto_mode ? "Automático" : "Manual") + "</span>";
  html += "</div>";
  html += "</section>";
  html += "<section>";
  html += "<h3>Estado de Salidas</h3>";
  html += "<div class='status'>Bombas (Pin 4): " + String(bomba_on ? "Prendida" : "Apagada") + "</div>";
  html += "<div class='status'>Quemador (Pin 5): " + String(quemador_on ? "Prendida" : "Apagada") + "</div>";
  html += "<div class='status'>Válvula Cierra (Pin 6): " + String(valvula_cierra_on ? "Prendida" : "Apagada") + "</div>";
  html += "<div class='status'>Válvula Abre (Pin 7): " + String(valvula_abre_on ? "Prendida" : "Apagada") + "</div>";
  html += "</section>";
  html += "</div>";
  html += "<section>";
  html += "<h3>Configuración Estacional</h3>";
  html += "<div class='status'>Inicio: " + String(seasonal_start_month) + "-" + String(seasonal_start_day) + "</div>";
  html += "<div class='status'>Fin: " + String(seasonal_end_month) + "-" + String(seasonal_end_day) + "</div>";
  html += "</section>";
  html += "<section>";
  html += "<h3>Configuración Horaria</h3>";
  html += "<table><tr><th>Día</th><th>Franja 1</th><th>Franja 2</th></tr>";
  for (int d = 0; d < 7; d++) {
    html += "<tr><td>" + days[d] + "</td>";
    html += "<td>" + String(daily_ranges[d][0].start_hour) + ":" + String(daily_ranges[d][0].start_min, DEC) + " - " + String(daily_ranges[d][0].end_hour) + ":" + String(daily_ranges[d][0].end_min, DEC) + "</td>";
    html += "<td>" + String(daily_ranges[d][1].start_hour) + ":" + String(daily_ranges[d][1].start_min, DEC) + " - " + String(daily_ranges[d][1].end_hour) + ":" + String(daily_ranges[d][1].end_min, DEC) + "</td></tr>";
  }
  html += "</table>";
  html += "</section>";
  html += "<script>";
  html += "function toggleMode() {";
  html += "  var toggle = document.getElementById('modeToggle').checked;";
  html += "  fetch('/toggleMode?state=' + toggle, { method: 'POST' })";
  html += "    .then(response => response.text())";
  html += "    .then(data => { alert(data); location.reload(); });";
  html += "}";
  html += "</script>";
  html += "</div></body></html>";
  
  server.send(200, "text/html; charset=UTF-8", html);
}

// Página de configuración
void handleConfig() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>ESP32-S3 Configuración</title>";
  html += getCommonStyle();
  html += "</head><body>";
  html += "<header><h1>ESP32-S3 Web Server</h1></header>";
  html += getNavMenu("config");
  html += "<div class='container'>";
  html += "<h2>Configuración</h2>";
  html += "<div class='inline-section'>";
  html += "<section>";
  html += "<h3>Activar/Desactivar STA</h3>";
  html += "<div class='toggle'>";
  html += "<label>STA Mode: </label>";
  html += "<input type='checkbox' id='staToggle' " + String(sta_enabled ? "checked" : "") + " onchange='toggleSTA()'>";
  html += "</div>";
  html += "</section>";
  html += "<section>";
  html += "<h3>Credenciales STA</h3>";
  html += "<div>";
  html += "<label>STA SSID: </label>";
  html += "<input type='text' id='staSSID' value='" + sta_ssid + "'><br>";
  html += "<label>STA Password: </label>";
  html += "<input type='password' id='staPassword' value='" + sta_password + "'><br>";
  html += "<button onclick='saveSTA()'>Guardar Credenciales STA</button>";
  html += "</div>";
  html += "</section>";
  html += "</div>";
  html += "<section>";
  html += "<h3>Configuración NTP</h3>";
  html += "<div>";
  html += "<label>Servidor NTP: </label>";
  html += "<input type='text' id='ntpServer' value='" + ntp_server + "'><br>";
  html += "<label>Offset Zona Horaria (horas): </label>";
  html += "<input type='number' id='ntpOffset' value='" + String(ntp_offset / 3600) + "'><br>";
  html += "<label>Intervalo de Actualización (segundos): </label>";
  html += "<input type='number' id='ntpInterval' value='" + String(ntp_update_interval / 1000) + "'><br>";
  html += "<button onclick='saveNTP()'>Guardar NTP</button>";
  html += "</div>";
  html += "</section>";
  html += "<section>";
  html += "<h3>Setear Fecha y Hora Manual</h3>";
  html += "<div>";
  html += "<label>Fecha (YYYY-MM-DD): </label>";
  html += "<input type='date' id='manualDate'><br>";
  html += "<label>Hora (HH:MM): </label>";
  html += "<input type='time' id='manualTime'><br>";
  html += "<button onclick='saveManualTime()'>Guardar Fecha/Hora Manual</button>";
  html += "</div>";
  html += "</section>";
  html += "<section>";
  html += "<h3>Reloj Estacional</h3>";
  html += "<div>";
  html += "<label>Inicio (MM-DD): </label>";
  html += "<input type='text' id='seasonalStart' value='" + String(seasonal_start_month) + "-" + String(seasonal_start_day < 10 ? "0" : "") + String(seasonal_start_day) + "'><br>";
  html += "<label>Fin (MM-DD): </label>";
  html += "<input type='text' id='seasonalEnd' value='" + String(seasonal_end_month) + "-" + String(seasonal_end_day < 10 ? "0" : "") + String(seasonal_end_day) + "'><br>";
  html += "<button onclick='saveSeasonal()'>Guardar Estacional</button>";
  html += "</div>";
  html += "</section>";
  html += "<section>";
  html += "<h3>Reloj Horario</h3>";
  html += "<form id='dailyForm'>";
  for (int d = 0; d < 7; d++) {
    html += "<div style='margin-bottom: 20px;'>";
    html += "<label>" + days[d] + "</label><br>";
    for (int r = 0; r < 2; r++) {
      String prefix = "day" + String(d) + "_range" + String(r);
      html += "<label>Franja " + String(r+1) + ": </label>";
      html += "<input type='time' name='" + prefix + "_start' value='" + String(daily_ranges[d][r].start_hour < 10 ? "0" : "") + String(daily_ranges[d][r].start_hour) + ":" + String(daily_ranges[d][r].start_min < 10 ? "0" : "") + String(daily_ranges[d][r].start_min) + "'>";
      html += " a ";
      html += "<input type='time' name='" + prefix + "_end' value='" + String(daily_ranges[d][r].end_hour < 10 ? "0" : "") + String(daily_ranges[d][r].end_hour) + ":" + String(daily_ranges[d][r].end_min < 10 ? "0" : "") + String(daily_ranges[d][r].end_min) + "'><br>";
    }
    html += "</div>";
  }
  html += "<button type='button' onclick='saveDaily()'>Guardar Horario</button>";
  html += "</form>";
  html += "</section>";
  html += "<script>";
  html += "function toggleSTA() {";
  html += "  var toggle = document.getElementById('staToggle').checked;";
  html += "  fetch('/toggleSTA?state=' + toggle, { method: 'POST' })";
  html += "    .then(response => response.text())";
  html += "    .then(data => { alert(data); location.reload(); });";
  html += "}";
  html += "function saveSTA() {";
  html += "  var ssid = document.getElementById('staSSID').value;";
  html += "  var password = document.getElementById('staPassword').value;";
  html += "  fetch('/setSTA?ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password), { method: 'POST' })";
  html += "    .then(response => response.text())";
  html += "    .then(data => { alert(data); location.reload(); });";
  html += "}";
  html += "function saveNTP() {";
  html += "  var server = document.getElementById('ntpServer').value;";
  html += "  var offset = document.getElementById('ntpOffset').value;";
  html += "  var interval = document.getElementById('ntpInterval').value;";
  html += "  fetch('/setNTP?server=' + encodeURIComponent(server) + '&offset=' + offset + '&interval=' + interval, { method: 'POST' })";
  html += "    .then(response => response.text())";
  html += "    .then(data => { alert(data); location.reload(); });";
  html += "}";
  html += "function saveManualTime() {";
  html += "  var date = document.getElementById('manualDate').value;";
  html += "  var time = document.getElementById('manualTime').value;";
  html += "  fetch('/setManualTime?date=' + encodeURIComponent(date) + '&time=' + encodeURIComponent(time), { method: 'POST' })";
  html += "    .then(response => response.text())";
  html += "    .then(data => { alert(data); location.reload(); });";
  html += "}";
  html += "function saveSeasonal() {";
  html += "  var start = document.getElementById('seasonalStart').value;";
  html += "  var end = document.getElementById('seasonalEnd').value;";
  html += "  fetch('/setSeasonal?start=' + encodeURIComponent(start) + '&end=' + encodeURIComponent(end), { method: 'POST' })";
  html += "    .then(response => response.text())";
  html += "    .then(data => { alert(data); location.reload(); });";
  html += "}";
  html += "function saveDaily() {";
  html += "  var formData = new FormData(document.getElementById('dailyForm'));";
  html += "  var params = new URLSearchParams(formData);";
  html += "  fetch('/setDaily?' + params.toString(), { method: 'POST' })";
  html += "    .then(response => response.text())";
  html += "    .then(data => { alert(data); location.reload(); });";
  html += "}";
  html += "</script>";
  html += "</div></body></html>";
  
  server.send(200, "text/html; charset=UTF-8", html);
}

// Página de manual
void handleManual() {
  // Código existente, sin cambios
}

// Manejadores POST
void handleToggleSTA() {
  // Código existente
}

void handleSetSTA() {
  // Código existente
}

void handleToggleMode() {
  // Código existente
}

void handleSetSeasonal() {
  // Código existente
}

void handleSetDaily() {
  // Código existente
}

void handleToggleBomba() {
  // Código existente
}

void handleToggleQuemador() {
  // Código existente
}

void handleToggleValvulaCierra() {
  // Código existente
}

void handleToggleValvulaAbre() {
  // Código existente
}

void handleSetNTP() {
  if (server.hasArg("server") && server.hasArg("offset") && server.hasArg("interval")) {
    ntp_server = server.arg("server");
    ntp_offset = server.arg("offset").toInt() * 3600; // Horas a segundos
    ntp_update_interval = server.arg("interval").toInt() * 1000; // Segundos a ms
    timeClient.end();
    timeClient = NTPClient(ntpUDP, ntp_server.c_str(), ntp_offset, ntp_update_interval);
    timeClient.begin();
    timeClient.forceUpdate();
    server.send(200, "text/plain; charset=UTF-8", "Configuración NTP guardada y actualizada");
  } else {
    server.send(400, "text/plain; charset=UTF-8", "Parámetros no proporcionados");
  }
}

void handleSetManualTime() {
  if (server.hasArg("date") && server.hasArg("time")) {
    String date = server.arg("date");
    String time = server.arg("time");
    int year, month, day, hour, min;
    sscanf(date.c_str(), "%d-%d-%d", &year, &month, &day);
    sscanf(time.c_str(), "%d:%d", &hour, &min);
    struct tm tm_struct;
    tm_struct.tm_year = year - 1900;
    tm_struct.tm_mon = month - 1;
    tm_struct.tm_mday = day;
    tm_struct.tm_hour = hour;
    tm_struct.tm_min = min;
    tm_struct.tm_sec = 0;
    tm_struct.tm_isdst = -1; // No DST info
    time_t new_epoch = mktime(&tm_struct) - ntp_offset; // Ajustar por offset a UTC
    timeClient.setEpochTime(new_epoch);
    last_epoch_time = new_epoch;
    last_ntp_update = millis();
    manual_time_set = true;
    server.send(200, "text/plain; charset=UTF-8", "Fecha y hora manual guardada");
  } else {
    server.send(400, "text/plain; charset=UTF-8", "Parámetros no proporcionados");
  }
}

// Función para verificar estacional
bool isWithinSeasonal(struct tm *ptm) {
  // Código existente
}

// Función para verificar horario
bool isWithinDaily(struct tm *ptm) {
  // Código existente
}

// Actualizar pines
void updatePins() {
  // Código existente
}

// Detener sistema
void stopSystem() {
  // Código existente
}

// Iniciar secuencia
void startSequence() {
  // Código existente
}

void setup() {
  Serial.begin(115200);
  
  // Inicializar pines
  pinMode(PIN_BOMBAS, OUTPUT);
  pinMode(PIN_QUEMADOR, OUTPUT);
  pinMode(PIN_VALVULA_CIERRA, OUTPUT);
  pinMode(PIN_VALVULA_ABRE, OUTPUT);
  digitalWrite(PIN_BOMBAS, LOW);
  digitalWrite(PIN_QUEMADOR, LOW);
  digitalWrite(PIN_VALVULA_CIERRA, LOW);
  digitalWrite(PIN_VALVULA_ABRE, LOW);
  
  initDailyRanges();
  
  // Configurar modo AP + STA
  WiFi.mode(WIFI_AP_STA);
  
  // Iniciar el Access Point
  WiFi.softAP(ap_ssid, ap_password);
  Serial.println("Access Point iniciado");
  Serial.print("IP del AP: ");
  Serial.println(WiFi.softAPIP());
  
  // Configurar manejadores del servidor
  server.on("/", handleStatus);
  server.on("/config", handleConfig);
  server.on("/manual", handleManual);
  server.on("/toggleSTA", HTTP_POST, handleToggleSTA);
  server.on("/setSTA", HTTP_POST, handleSetSTA);
  server.on("/toggleMode", HTTP_POST, handleToggleMode);
  server.on("/setSeasonal", HTTP_POST, handleSetSeasonal);
  server.on("/setDaily", HTTP_POST, handleSetDaily);
  server.on("/toggleBomba", HTTP_POST, handleToggleBomba);
  server.on("/toggleQuemador", HTTP_POST, handleToggleQuemador);
  server.on("/toggleValvulaCierra", HTTP_POST, handleToggleValvulaCierra);
  server.on("/toggleValvulaAbre", HTTP_POST, handleToggleValvulaAbre);
  server.on("/setNTP", HTTP_POST, handleSetNTP);
  server.on("/setManualTime", HTTP_POST, handleSetManualTime);
  
  // Iniciar el servidor web
  server.begin();
  Serial.println("Servidor web iniciado");
}

void loop() {
  server.handleClient();
  if (sta_enabled && WiFi.status() == WL_CONNECTED) {
    if (timeClient.update()) {
      last_epoch_time = timeClient.getEpochTime();
      last_ntp_update = millis();
      manual_time_set = false; // Reset manual si NTP actualiza
    }
  } else if (manual_time_set || last_epoch_time > 0) {
    // Actualizar tiempo internamente con millis()
    unsigned long elapsed = millis() - last_ntp_update;
    timeClient.setEpochTime(last_epoch_time + (elapsed / 1000));
  }
  
  if (auto_mode) {
    time_t epochTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime((time_t *)&epochTime);
    bool should_active = isWithinSeasonal(ptm) && isWithinDaily(ptm);
    if (should_active && !system_active) {
      startSequence();
      system_active = true;
    } else if (!should_active && system_active) {
      stopSystem();
      system_active = false;
    }
    
    // Máquina de estados para secuencia
    switch (seq_state) {
      case START_BOMBAS:
        bomba_on = true;
        updatePins();
        seq_timer = millis();
        seq_state = WAIT_BOMBAS;
        break;
      case WAIT_BOMBAS:
        if (millis() - seq_timer >= 5000) {
          seq_state = START_QUEMADOR;
        }
        break;
      case START_QUEMADOR:
        quemador_on = true;
        updatePins();
        seq_timer = millis();
        seq_state = WAIT_QUEMADOR;
        break;
      case WAIT_QUEMADOR:
        if (millis() - seq_timer >= 5000) {
          seq_state = START_VALVULA;
        }
        break;
      case START_VALVULA:
        valvula_alternating = true;
        valvula_timer = millis();
        seq_state = ACTIVE;
        break;
      case ACTIVE:
        // Continuar alternancia
        break;
      default:
        break;
    }
    
    if (valvula_alternating && (millis() - valvula_timer >= 2000)) {
      valvula_timer = millis();
      valvula_state = !valvula_state;
      valvula_cierra_on = valvula_state;
      valvula_abre_on = !valvula_state;
      updatePins();
    }
  } else {
    // Modo manual
    updatePins();
  }
}
