/* ==========================================================================
   FUNDAMENTOS DE IoT - Sistema de Riego por Zonas P8 (ESP32)
   Medición e Intervalos 100% INDEPENDIENTES por Zona + Prioridad Z1
   ========================================================================== */

#include <Arduino.h>
#include <Wire.h> 
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"

// Pantalla OLED en G21 (SDA) y G22 (SCL)
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

// Pines de entrada analógica (Sensores de Humedad Capacitivos)
const uint8_t PIN_SENSOR_Z1 = 32;
const uint8_t PIN_SENSOR_Z2 = 35;

// Pines de salida para LEDs y Buzzer
const uint8_t PIN_LED_Z1  = 17; // LED Verde (Riego Zona 1)
const uint8_t PIN_LED_Z2  = 16; // LED Azul/Amarillo (Riego Zona 2)
const uint8_t PIN_LED_ERR = 18; // LED Rojo (Error / Paro)
const uint8_t PIN_BUZZER  = 26; // Buzzer

// Pines de salida para Relés (Bombas de agua / Válvulas)
const uint8_t PIN_RELE_Z1 = 27; // IN1
const uint8_t PIN_RELE_Z2 = 4;  // IN2

// Lógica de activación del relé (Active LOW)
#define RELE_ON  LOW
#define RELE_OFF HIGH

// Pin de entrada para Botón de Paro/Rearme
const uint8_t PIN_BOTON   = 25;

// Valores de calibración analógica (mV)
const float Z1_V_AIRE = 2314.9;
const float Z1_V_AGUA = 280.3;

const float Z2_V_AIRE = 2246.2;
const float Z2_V_AGUA = 913.7;

// Umbrales de humedad (%) para control
const float Z1_ACTIVA_PCT  = 30.0;
const float Z1_DESACT_PCT  = 60.0;
const float Z2_ACTIVA_PCT  = 25.0;
const float Z2_DESACT_PCT  = 55.0;

// Constantes de Muestreo (ms)
const uint32_t T_MED_NORMAL_MS  = 2500;   // 2.5 segundos cuando la zona necesita vigilancia continua
const uint32_t T_MED_REPOSO_MS  = 600000; // 10 minutos (600.000 ms) cuando la zona superó su umbral
const uint32_t T_CONF_MS        = 3000;   // Tiempo de confirmación (3 s)

// ---------------------------------------------------------------------------
// Temporizadores e Intervalos INDEPENDIENTES por Zona
// ---------------------------------------------------------------------------
uint32_t t_medicion_z1 = 0;
uint32_t t_medicion_z2 = 0;

uint32_t periodo_med_z1 = T_MED_NORMAL_MS;
uint32_t periodo_med_z2 = T_MED_NORMAL_MS;

// ---------------------------------------------------------------------------
// Definición de Estados y Variables Globales
// ---------------------------------------------------------------------------
enum Estado {
  VIGILANDO,
  REGANDO,
  ESPERA_CONFIRMACION,
  ERROR_SEGURO
};

Estado estado = VIGILANDO;

float humedadZ1 = 0.0;
float humedadZ2 = 0.0;

uint32_t t_entrada  = 0;

// --- VARIABLES Y CLIENTES MQTT ---
WiFiClient   red;
PubSubClient mqtt(red);

String clientId, topicDatos, topicEstado, topicCmd;

uint32_t t_pub       = 0;
uint32_t tWiFi       = 0;
uint32_t tReconexion = 0;

const uint32_t PERIODO_PUB_MS   = 10000; // Publicación MQTT cada 10 s
const uint32_t REINTENTO_WIFI_MS = 15000;
const uint32_t ESPERA_INICIAL    = 2000;
const uint32_t ESPERA_MAXIMA     = 30000;
uint32_t esperaReconexion        = ESPERA_INICIAL;
bool sensorOk                    = true;

const char* nombreEstado(Estado e) {
  switch (e) {
    case VIGILANDO:           return "VIGILANDO";
    case REGANDO:             return "REGANDO";
    case ESPERA_CONFIRMACION: return "ESPERA_CONFIRMACION";
    case ERROR_SEGURO:        return "ERROR_SEGURO";
    default:                  return "DESCONOCIDO";
  }
}

void cambiar(Estado nuevo, const char* razon) {
  estado = nuevo;
  t_entrada = millis();
  Serial.printf(">> Transición -> Estado: %s | Motivo: %s\n", nombreEstado(estado), razon);
}

float leerHumedadPct(uint8_t pin, float v_aire, float v_agua) {
  uint32_t suma = 0;
  for (int i = 0; i < 10; i++) {
    suma += analogRead(pin);
  }
  float raw = suma / 10.0;
  float mV = (raw / 4095.0) * 3300.0;
  float pct = (v_aire - mV) * 100.0 / (v_aire - v_agua);
  return constrain(pct, 0.0, 100.0);
}

void recibirComando(char* topic, byte* payload, unsigned int largo) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, largo);
  if (error) {
    Serial.printf("[cmd] JSON invalido en %s: %s\n", topic, error.c_str());
    return;
  }
  Serial.printf("[cmd] Recibido en %s\n", topic);
}

void mantenerWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  
  uint32_t ahora = millis();
  if (ahora - tWiFi < REINTENTO_WIFI_MS) return;
  tWiFi = ahora;

  Serial.println("[WiFi] Reintentando conexion limpia...");
  WiFi.disconnect(true);
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void mantenerMQTT() {
  if (mqtt.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;

  uint32_t ahora = millis();
  if (ahora - tReconexion < esperaReconexion) return;
  tReconexion = ahora;

  Serial.printf("[MQTT] Conectando como %s ... ", clientId.c_str());
  
  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS,
                   topicEstado.c_str(), 1, true, "offline")) {
    Serial.println("OK -> Connected to MQTT broker");
    mqtt.publish(topicEstado.c_str(), "online", true);
    mqtt.subscribe(topicCmd.c_str(), 1);
    esperaReconexion = ESPERA_INICIAL;
  } else {
    Serial.printf("FALLO rc=%d (reintento en %u s)\n", mqtt.state(), esperaReconexion / 1000);
    esperaReconexion = (esperaReconexion * 2 > ESPERA_MAXIMA) ? ESPERA_MAXIMA : esperaReconexion * 2;
  }
}

void publicarDatos() {
  if (!mqtt.connected()) return;

  JsonDocument doc;
  if (sensorOk) {
    doc["humedad_z1"] = roundf(humedadZ1 * 10.0f) / 10.0f;
    doc["humedad_z2"] = roundf(humedadZ2 * 10.0f) / 10.0f;
  }
  doc["sensor_ok"] = sensorOk ? 1 : 0;
  doc["rssi_dbm"]  = WiFi.RSSI();

  char payload[256];
  size_t n = serializeJson(doc, payload, sizeof(payload));

  if (mqtt.publish(topicDatos.c_str(), (const uint8_t*)payload, n, true)) {
    Serial.printf("[PUB] %s -> %s\n", topicDatos.c_str(), payload);
  } else {
    Serial.println("[PUB] Error al publicar payload");
  }
}

void ledsYBuzzer(bool z1, bool z2, bool err, bool buz) {
  digitalWrite(PIN_LED_Z1, z1 ? HIGH : LOW);
  digitalWrite(PIN_LED_Z2, z2 ? HIGH : LOW);
  digitalWrite(PIN_LED_ERR, err ? HIGH : LOW);
  digitalWrite(PIN_BUZZER, buz ? HIGH : LOW);

  digitalWrite(PIN_RELE_Z1, z1 ? RELE_ON : RELE_OFF);
  digitalWrite(PIN_RELE_Z2, z2 ? RELE_ON : RELE_OFF);
}

bool botonPulsado() {
  static uint32_t t_ultimo = 0;
  static uint8_t lecturas_consecutivas = 0;
  
  if (millis() - t_ultimo >= 50) {
    t_ultimo = millis();
    
    if (digitalRead(PIN_BOTON) == HIGH) {
      lecturas_consecutivas++;
    } else {
      lecturas_consecutivas = 0;
    }
  }
  
  if (lecturas_consecutivas >= 3) {
    lecturas_consecutivas = 0;
    return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED_Z1, OUTPUT);
  pinMode(PIN_LED_Z2, OUTPUT);
  pinMode(PIN_LED_ERR, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  
  pinMode(PIN_RELE_Z1, OUTPUT);
  pinMode(PIN_RELE_Z2, OUTPUT);

  digitalWrite(PIN_RELE_Z1, RELE_OFF);
  digitalWrite(PIN_RELE_Z2, RELE_OFF);

  pinMode(PIN_BOTON, INPUT_PULLDOWN);

  analogReadResolution(12);

  digitalWrite(PIN_LED_Z1, LOW);
  digitalWrite(PIN_LED_Z2, LOW);
  digitalWrite(PIN_LED_ERR, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  Wire.begin(21, 22);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3c)) {
    Serial.println("Error: No se detectó la pantalla OLED SSD1306");
  }

  Serial.println();
  Serial.println("========================================================");
  Serial.println(" Sistema P8 - Riego por Zonas (Medición Independiente)");
  Serial.println("========================================================");

  cambiar(VIGILANDO, "arranque");

  clientId    = String(MQTT_USER) + "-" + NODO;
  topicDatos  = String("curso/") + MQTT_USER + "/" + PROYECTO + "/" + NODO;
  topicEstado = topicDatos + "/estado";
  topicCmd    = topicDatos + "/cmd";

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 10000) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Conectado. IP local = ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] Sin red inicial; continuando en modo local.");
  }
  tWiFi = millis();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(recibirComando);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(15);
  mqtt.setSocketTimeout(3);
}

void actualizarOLED() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  oled.setCursor(0, 0);
  oled.println(F("-- RIEGO P8 --"));

  oled.setCursor(0, 16);
  oled.print(F("Est: "));
  oled.println(nombreEstado(estado));

  oled.setCursor(0, 32);
  oled.print(F("H_Z1: ")); oled.print(humedadZ1, 1); oled.println(F("%"));

  oled.setCursor(0, 48);
  oled.print(F("H_Z2: ")); oled.print(humedadZ2, 1); oled.println(F("%"));

  oled.display();
}

// ---------------------------------------------------------------------------
// Lazo principal (FSM no bloqueante)
// ---------------------------------------------------------------------------
void loop() {

  mantenerWiFi();
  mantenerMQTT();
  mqtt.loop();

  uint32_t ahora = millis();

  // Publicación constante MQTT cada 10 s
  if (ahora - t_pub >= PERIODO_PUB_MS) {
    t_pub = ahora;
    publicarDatos();
  }

  // =========================================================================
  // 1. MEDICIÓN E INTERVALO INDEPENDIENTE - ZONA 1
  // =========================================================================
  if (ahora - t_medicion_z1 >= periodo_med_z1) {
    t_medicion_z1 = ahora;
    humedadZ1 = leerHumedadPct(PIN_SENSOR_Z1, Z1_V_AIRE, Z1_V_AGUA);

    // Si Z1 superó su umbral de desacoplamiento (60%), su tiempo cambia a 10 min
    if (humedadZ1 >= Z1_DESACT_PCT) {
      periodo_med_z1 = T_MED_REPOSO_MS; // 10 minutos (600.000 ms)
    } else {
      periodo_med_z1 = T_MED_NORMAL_MS; // 2.5 segundos
    }
    
    Serial.printf("[%8lu ms] H_Z1=%.1f%% (Periodo Z1: %u s)\n", ahora, humedadZ1, periodo_med_z1 / 1000);
    actualizarOLED();
  }

  // =========================================================================
  // 2. MEDICIÓN E INTERVALO INDEPENDIENTE - ZONA 2
  // =========================================================================
  if (ahora - t_medicion_z2 >= periodo_med_z2) {
    t_medicion_z2 = ahora;
    humedadZ2 = leerHumedadPct(PIN_SENSOR_Z2, Z2_V_AIRE, Z2_V_AGUA);

    // Si Z2 superó su umbral de desacoplamiento (55%), su tiempo cambia a 10 min
    if (humedadZ2 >= Z2_DESACT_PCT) {
      periodo_med_z2 = T_MED_REPOSO_MS; // 10 minutos (600.000 ms)
    } else {
      periodo_med_z2 = T_MED_NORMAL_MS; // 2.5 segundos
    }

    Serial.printf("[%8lu ms] H_Z2=%.1f%% (Periodo Z2: %u s)\n", ahora, humedadZ2, periodo_med_z2 / 1000);
    actualizarOLED();
  }

  // =========================================================================
  // 3. MÁQUINA DE ESTADOS (FSM)
  // =========================================================================
  switch (estado) {

    case VIGILANDO:
      ledsYBuzzer(false, false, false, false);

      // CONDICIÓN SOLICITADA: Si ambas necesitan riego, Zona 1 tiene preferencia sobre Zona 2
      if (humedadZ1 <= Z1_ACTIVA_PCT) {
        cambiar(REGANDO, "Zona 1 bajo umbral (Prioridad)");
      } 
      else if (humedadZ2 <= Z2_ACTIVA_PCT) {
        cambiar(REGANDO, "Zona 2 bajo umbral");
      }
      break;

    case REGANDO: {
      bool necesitaZ1 = (humedadZ1 < Z1_DESACT_PCT);
      bool necesitaZ2 = (humedadZ2 < Z2_DESACT_PCT);

      // Riego escalonado respetando la prioridad de la Zona 1
      if (necesitaZ1) {
        ledsYBuzzer(true, false, false, false);
      } 
      else if (necesitaZ2) {
        ledsYBuzzer(false, true, false, false);
      } 
      else {
        ledsYBuzzer(false, false, false, false);
        cambiar(ESPERA_CONFIRMACION, "Histéresis alcanzada en ambas zonas");
      }

      yield(); 
      break;
    }

    case ESPERA_CONFIRMACION:
      ledsYBuzzer(false, false, false, false);

      if (millis() - t_entrada >= T_CONF_MS) {
        cambiar(VIGILANDO, "Humedad estable tras Tconf");
      }
      break;

    case ERROR_SEGURO:
      ledsYBuzzer(false, false, true, (millis() / 300) % 2);

      if (botonPulsado()) {
        cambiar(VIGILANDO, "Boton: Rearme de sistema");
      }
      break;
  }

  // Paro de emergencia
  if (estado != ERROR_SEGURO && botonPulsado()) {
    cambiar(ERROR_SEGURO, "Boton: Paro de emergencia");
  }
}
