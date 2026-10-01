/* ==========================================================================
   FUNDAMENTOS DE IoT - 2do SEMESTRE 2026
   GT2 (Semana 4) - Proyecto base: nodo de ventilacion
   Universidad Autonoma de Chile - Ingenieria Civil Informatica
   v1.4 - 2026-2 - R. Rodriguez
   QUE HACE ESTE PROGRAMA
   Controla la ventilacion de un espacio con cuatro estados:
     VIGILANDO -> VENTILANDO -> ALERTA  (segun la temperatura del DHT22)
     ERROR_SEGURO               (falla del sensor o boton de paro)
   El ventilador se enciende sobre 28,0 C y se apaga bajo 26,0 C
   (HISTERESIS: dos umbrales, para que no oscile en el borde).
   Sobre 32,0 C se suma la alerta sonora. Tres lecturas invalidas
   seguidas del sensor, o el boton de paro, llevan al estado de error
   con el ventilador apagado. Nada bloquea: no hay delay() en el lazo.

   COMPONENTES (kit base de todos los equipos)
     DHT22 (o DHT11: cambiar TIPO_DHT)  -> GPIO 4  (+ pull-up en DATA)
     Ventilador 5 V via transistor/rele -> GPIO 18 (senal)
     Buzzer                             -> GPIO 27
     LED verde (normal) / LED rojo      -> GPIO 32 / 33 (con R de 220)
     Boton multifuncion (paro/rearme)   -> GPIO 25 (a 3V3, pull-down interno)
     GPIO 21 y 22 quedan libres: son el I2C del OLED de la Semana 5.
   SEGURIDAD (item 5 de la pauta): el ventilador NUNCA se alimenta desde
   un GPIO ni desde el pin de 3,3 V. El GPIO entrega la senal; el
   transistor o el modulo de rele conmuta la energia desde el riel de
   5 V, con masa comun. El montaje se inspecciona antes de energizar.
   En Wokwi el ventilador se representa con un LED.

   COMO SE USA EN LA GT2
   1. Copien el proyecto Wokwi del curso a su cuenta (E NN-GT2).
   2. Comprueben la secuencia normal cambiando la temperatura del DHT22.
   3. Provoquen el error (item 6): desconecten el sensor y esperen las
      3 lecturas invalidas. Verifiquen la salida segura.
   4. Prueba de no bloqueo (item 4): presionen el boton mientras corre
      una temporizacion y comprueben el paso inmediato a ERROR_SEGURO;
      una segunda pulsacion, ya en ERROR_SEGURO, rearma el sistema.
   5. Adapten la estructura a la FSM de SU proyecto (items 7 y 8),
      decidiendo sobre la lectura del sensor verificado en el item 1.
   ========================================================================== */


// ===========================================================================
// Sistema de Riego por Zonas P8 (ESP32)
// ---------------------------------------------------------------------------
// 1. Constantes de Hardware, Pines y Umbrales
// ---------------------------------------------------------------------------
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
const uint8_t PIN_RELE_Z1 = 27; // Cambia al GPIO donde conectaste IN1
const uint8_t PIN_RELE_Z2 = 5; // Cambia al GPIO donde conectaste IN2

// Lógica de activación del relé (los módulos azules suelen activar con LOW)
#define RELE_ON  LOW
#define RELE_OFF HIGH

// Pin de entrada para Botón de Paro/Rearme
const uint8_t PIN_BOTON   = 25;

// Valores de calibracion analogica (mV) según tabla de dispersión
const float Z1_V_AIRE = 2314.9;
const float Z1_V_AGUA = 280.3;

const float Z2_V_AIRE = 2246.2;
const float Z2_V_AGUA = 913.7;

// Umbrales de humedad (%) para control
const float Z1_ACTIVA_PCT  = 30.0;
const float Z1_DESACT_PCT  = 60.0;
const float Z2_ACTIVA_PCT  = 25.0;
const float Z2_DESACT_PCT  = 55.0;

// Tiempos (ms)
const uint32_t PERIODO_MED_MS     = 1000; // Intervalo de lectura (1 s)
const uint32_t T_CONF_MS          = 3000; // Tiempo de confirmación (3 s)
const uint32_t T_ANTIRREBOTE_MS   = 50;   // Antirrebote botón

// ---------------------------------------------------------------------------
// 2. Definición de Estados y Variables Globales
// ---------------------------------------------------------------------------
enum Estado {
  VIGILANDO,
  REGANDO,
  ESPERA_CONFIRMACION,
  ERROR_SEGURO
};

Estado estado = VIGILANDO;

// Variables globales del sistema
float humedadZ1 = 0.0;
float humedadZ2 = 0.0;
uint8_t zonaActiva = 0; // 1 para Zona 1, 2 para Zona 2

// Temporizadores con millis()
uint32_t t_medicion = 0;
uint32_t t_entrada  = 0;

// --- VARIABLES Y CLIENTES MQTT (GT4) ---
WiFiClient   red;
PubSubClient mqtt(red);

String clientId, topicDatos, topicEstado, topicCmd;

uint32_t t_pub       = 0;
uint32_t tWiFi       = 0;
uint32_t tReconexion = 0;

const uint32_t PERIODO_PUB_MS   = 10000; // Publicación cada 10 s
const uint32_t REINTENTO_WIFI_MS = 15000;
const uint32_t ESPERA_INICIAL    = 2000;
const uint32_t ESPERA_MAXIMA     = 30000;
uint32_t esperaReconexion        = ESPERA_INICIAL;
bool sensorOk                    = true;
// Función para cambio de estado e impresión por Consola Serie
void cambiar(Estado nuevo, const char* razon) {
  estado = nuevo;
  t_entrada = millis();
  Serial.printf(">> Transición -> Estado: %s | Motivo: %s\n", nombreEstado(estado), razon);
}

// HASTA ESTA LINEA LLEGA EL PASO 1 (POR SI DEBO BORRAR ESTO)

const char* nombreEstado(Estado e) {
  switch (e) {
    case VIGILANDO:           return "VIGILANDO";
    case REGANDO:             return "REGANDO";
    case ESPERA_CONFIRMACION: return "ESPERA_CONFIRMACION";
    case ERROR_SEGURO:        return "ERROR_SEGURO";
    default:                  return "DESCONOCIDO";
  }
}

// ---------------------------------------------------------------------------
// 3. Entradas y salidas (Funciones auxiliares)
// ---------------------------------------------------------------------------

// Lectura y conversión analógica a porcentaje de humedad (%)
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

// ---------------------------------------------------------------------------
// FUNCIONES DE RED Y MQTT (GT4) - PASO 2
// ---------------------------------------------------------------------------

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

  Serial.println("[WiFi] Reintentando conexion limpia a IoT-Lab...");
  WiFi.disconnect(true); // Desconecta y borra la sesión previa para liberar el driver
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

//HASTA ESA LINEA LLEGA EL PASO 2 (POR SI DEBO BORRAR ESTO)

void ledsYBuzzer(bool z1, bool z2, bool err, bool buz) {
  // Control de LEDs
  digitalWrite(PIN_LED_Z1, z1 ? HIGH : LOW);
  digitalWrite(PIN_LED_Z2, z2 ? HIGH : LOW);
  digitalWrite(PIN_LED_ERR, err ? HIGH : LOW);
  digitalWrite(PIN_BUZZER, buz ? HIGH : LOW);

  // Control de Relés (Bombas)
  digitalWrite(PIN_RELE_Z1, z1 ? RELE_ON : RELE_OFF);
  digitalWrite(PIN_RELE_Z2, z2 ? RELE_ON : RELE_OFF);
}
// Detección de botón filtrada contra ruido inductivo de la bomba
bool botonPulsado() {
  static uint32_t t_ultimo = 0;
  static uint8_t lecturas_consecutivas = 0;
  
  if (millis() - t_ultimo >= 50) { // Evalúa cada 50 ms
    t_ultimo = millis();
    
    if (digitalRead(PIN_BOTON) == HIGH) {
      lecturas_consecutivas++;
    } else {
      lecturas_consecutivas = 0; // Si fue solo un chispazo rápido, se limpia
    }
  }
  
  // Solo activa el Paro si el botón se mantiene presionado 3 veces seguidas (150 ms)
  if (lecturas_consecutivas >= 3) {
    lecturas_consecutivas = 0;
    return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);

  // Configuración de salidas LED y Buzzer
  pinMode(PIN_LED_Z1, OUTPUT);
  pinMode(PIN_LED_Z2, OUTPUT);
  pinMode(PIN_LED_ERR, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  
  // Configuración de salidas para Relés
  pinMode(PIN_RELE_Z1, OUTPUT);
  pinMode(PIN_RELE_Z2, OUTPUT);

  // Asegurar estado inicial apagado
  digitalWrite(PIN_RELE_Z1, RELE_OFF);
  digitalWrite(PIN_RELE_Z2, RELE_OFF);

  // Configuración de entrada para el Botón
  pinMode(PIN_BOTON, INPUT_PULLDOWN);

  // Resolución para lectura analógica de sensores de humedad en ESP32
  analogReadResolution(12);

  // Asegurar estado inicial apagado en todas las salidas
  digitalWrite(PIN_LED_Z1, LOW);
  digitalWrite(PIN_LED_Z2, LOW);
  digitalWrite(PIN_LED_ERR, LOW);
  digitalWrite(PIN_BUZZER, LOW);
// --- PARTE 2: Inicializar bus I2C y Pantalla OLED ---
  Wire.begin(21, 22); // SDA en GPIO 21, SCL en GPIO 22
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3c)) {
    Serial.println("Error: No se detecto la pantalla OLED SSD1306");
  }
  // Encabezado de arranque
  Serial.println();
  Serial.println("========================================================");
  Serial.println(" Sistema P8 - Riego por Zonas (Demostración por LEDs)");
  Serial.println("========================================================");
  Serial.printf(" Umbrales Z1: Activa <= %.1f%% | Desactiva >= %.1f%%\n", Z1_ACTIVA_PCT, Z1_DESACT_PCT);
  Serial.printf(" Umbrales Z2: Activa <= %.1f%% | Desactiva >= %.1f%%\n", Z2_ACTIVA_PCT, Z2_DESACT_PCT);
  Serial.println("========================================================");

  cambiar(VIGILANDO, "arranque");

// --- CONFIGURACIÓN DE RED Y TÓPICOS MQTT (GT4 - PASO 3) ---
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

  // Titulo
  oled.setCursor(0, 0);
  oled.println(F("-- RIEGO P8 --"));

  // Estado actual
 // Estado actual
  oled.setCursor(0, 16);
  oled.print(F("Est: "));
  oled.println(nombreEstado(estado));
  // Lecturas
  oled.setCursor(0, 32);
  oled.print(F("H_Z1: ")); oled.print(humedadZ1, 1); oled.println(F("%"));

  oled.setCursor(0, 48);
  oled.print(F("H_Z2: ")); oled.print(humedadZ2, 1); oled.println(F("%"));

  oled.display();
}

//FINAL PASO 3 (POR SI LO DEBO BORRAR) 

// ---------------------------------------------------------------------------
// 4. Lazo principal (FSM no bloqueante)
// ---------------------------------------------------------------------------
void loop() {

// --- MANTENER RED Y PUBLICAR (GT4 - PASO 4) ---
  mantenerWiFi();
  mantenerMQTT();
  mqtt.loop();

  uint32_t ahora = millis();
  if (ahora - t_pub >= PERIODO_PUB_MS) {
    t_pub = ahora;
    publicarDatos();
  }
  // Muestreo cada 1 segundo sin bloquear
  if (millis() - t_medicion >= PERIODO_MED_MS) {
    t_medicion = millis();
humedadZ1 = leerHumedadPct(PIN_SENSOR_Z1, Z1_V_AIRE, Z1_V_AGUA);
humedadZ2 = leerHumedadPct(PIN_SENSOR_Z2, Z2_V_AIRE, Z2_V_AGUA);

    Serial.printf("[%8lu ms] H_Z1=%.1f%%  H_Z2=%.1f%%  | Estado: %s\n",
                  millis(), humedadZ1, humedadZ2, nombreEstado(estado));
    actualizarOLED(); // <--- AGREGA SOLO ESTA LÍNEA AQUÍ
  }

  // Evaluación de la Máquina de Estados (FSM)
  switch (estado) {

    case VIGILANDO:
      ledsYBuzzer(false, false, false, false);

      // Entra a REGANDO si cualquiera de las dos zonas necesita agua
      if (humedadZ1 <= Z1_ACTIVA_PCT || humedadZ2 <= Z2_ACTIVA_PCT) {
        cambiar(REGANDO, "Humedad bajo umbral de activacion");
      }
      break;

    case REGANDO: {
      // Activa cada relé de forma independiente según su humedad
      bool regarZ1 = (humedadZ1 < Z1_DESACT_PCT);
      bool regarZ2 = (humedadZ2 < Z2_DESACT_PCT);

      ledsYBuzzer(regarZ1, regarZ2, false, false);

      // Si ambas zonas terminan de regar, pasa a confirmación
      if (!regarZ1 && !regarZ2) {
        cambiar(ESPERA_CONFIRMACION, "Ambas zonas alcanzaron la humedad meta");
      }
      break;
    }

    case ESPERA_CONFIRMACION:
      ledsYBuzzer(false, false, false, false);

      if (millis() - t_entrada >= T_CONF_MS) {
        cambiar(VIGILANDO, "Humedad estable tras Tconf");
      }
      break;

    case ERROR_SEGURO:
      // LED Rojo G18 encendido + Buzzer intermitente
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


/* ---------------------------------------------------------------------------
   PARA ADAPTAR A SU PROYECTO (items 7 y 8)
   1. Reemplacen el enum por los estados MINIMOS de su fila del anexo,
      conservando siempre un estado de error con salida segura.
   2. Las transiciones sobre magnitudes usan la lectura CALIBRADA del
      sensor verificado en el item 1, nunca cuentas crudas.
   3. Histeresis: la banda entre los dos umbrales debe ser MAYOR que la
      dispersion observada en la verificacion fisica. Aqui: 2,0 C.
   4. Persistencia: N muestras consecutivas antes de transicionar cuando
      un pico aislado no constituye un evento. Aqui: MAX_INVALIDAS = 3.
   --------------------------------------------------------------------------- */
