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

// Pines de entrada analógica (Sensores de Humedad Capacitivos)
const uint8_t PIN_SENSOR_Z1 = 32;
const uint8_t PIN_SENSOR_Z2 = 35;

// Pines de salida para LEDs y Buzzer
const uint8_t PIN_LED_Z1  = 17; // LED Verde (Riego Zona 1)
const uint8_t PIN_LED_Z2  = 16; // LED Azul/Amarillo (Riego Zona 2)
const uint8_t PIN_LED_ERR = 18; // LED Rojo (Error / Paro)
const uint8_t PIN_BUZZER  = 26; // Buzzer

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

// Función para cambio de estado e impresión por Consola Serie
void cambiar(Estado nuevo, const char* razon) {
  estado = nuevo;
  t_entrada = millis();
  Serial.printf(">> Transición -> Estado: %s | Motivo: %s\n", nombreEstado(estado), razon);
}

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

// Control centralizado de LEDs de zonas y error, más el Buzzer
void ledsYBuzzer(bool ledZ1, bool ledZ2, bool ledErr, bool buzz) {
  digitalWrite(PIN_LED_Z1, ledZ1 ? HIGH : LOW);
  digitalWrite(PIN_LED_Z2, ledZ2 ? HIGH : LOW);
  digitalWrite(PIN_LED_ERR, ledErr ? HIGH : LOW);
  digitalWrite(PIN_BUZZER, buzz ? HIGH : LOW);
}

// Detección de flanco de subida para el botón (antirrebote sin delay)
bool botonPulsado() {
  static uint32_t t_ultimo = 0;
  static bool nivel_prev = false;
  bool nivel = digitalRead(PIN_BOTON) == HIGH;
  bool flanco = nivel && !nivel_prev && (millis() - t_ultimo >= T_ANTIRREBOTE_MS);
  if (flanco) t_ultimo = millis();
  nivel_prev = nivel;
  return flanco;
}

void setup() {
  Serial.begin(115200);

  // Configuración de salidas LED y Buzzer
  pinMode(PIN_LED_Z1, OUTPUT);
  pinMode(PIN_LED_Z2, OUTPUT);
  pinMode(PIN_LED_ERR, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  
  // Configuración de entrada para el Botón
  pinMode(PIN_BOTON, INPUT_PULLDOWN);

  // Resolución para lectura analógica de sensores de humedad en ESP32
  analogReadResolution(12);

  // Asegurar estado inicial apagado en todas las salidas
  digitalWrite(PIN_LED_Z1, LOW);
  digitalWrite(PIN_LED_Z2, LOW);
  digitalWrite(PIN_LED_ERR, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  // Encabezado de arranque
  Serial.println();
  Serial.println("========================================================");
  Serial.println(" Sistema P8 - Riego por Zonas (Demostración por LEDs)");
  Serial.println("========================================================");
  Serial.printf(" Umbrales Z1: Activa <= %.1f%% | Desactiva >= %.1f%%\n", Z1_ACTIVA_PCT, Z1_DESACT_PCT);
  Serial.printf(" Umbrales Z2: Activa <= %.1f%% | Desactiva >= %.1f%%\n", Z2_ACTIVA_PCT, Z2_DESACT_PCT);
  Serial.println("========================================================");

  cambiar(VIGILANDO, "arranque");
}

// ---------------------------------------------------------------------------
// 4. Lazo principal (FSM no bloqueante)
// ---------------------------------------------------------------------------
void loop() {
  // Muestreo cada 1 segundo sin bloquear
  if (millis() - t_medicion >= PERIODO_MED_MS) {
    t_medicion = millis();
humedadZ1 = leerHumedadPct(PIN_SENSOR_Z1, Z1_V_AIRE, Z1_V_AGUA);
humedadZ2 = leerHumedadPct(PIN_SENSOR_Z2, Z2_V_AIRE, Z2_V_AGUA);

    Serial.printf("[%8lu ms] H_Z1=%.1f%%  H_Z2=%.1f%%  | Estado: %s\n",
                  millis(), humedadZ1, humedadZ2, nombreEstado(estado));
  }

  // Evaluación de la Máquina de Estados (FSM)
  switch (estado) {

    case VIGILANDO:
      ledsYBuzzer(false, false, false, false);

      if (humedadZ1 <= Z1_ACTIVA_PCT) {
        zonaActiva = 1;
        cambiar(REGANDO, "Zona 1 requiere riego");
      } else if (humedadZ2 <= Z2_ACTIVA_PCT) {
        zonaActiva = 2;
        cambiar(REGANDO, "Zona 2 requiere riego");
      }
      break;

    case REGANDO:
      if (zonaActiva == 1) {
        ledsYBuzzer(true, false, false, false); // LED G17 (Zona 1)
        if (humedadZ1 >= Z1_DESACT_PCT) {
          cambiar(ESPERA_CONFIRMACION, "Zona 1 alcanzo humedad meta");
        }
      } else {
        ledsYBuzzer(false, true, false, false); // LED G16 (Zona 2)
        if (humedadZ2 >= Z2_DESACT_PCT) {
          cambiar(ESPERA_CONFIRMACION, "Zona 2 alcanzo humedad meta");
        }
      }
      break;

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
