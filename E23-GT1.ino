/* ==========================================================================
   FUNDAMENTOS DE IoT - 2do SEMESTRE 2026
   GT1 - Cadena de adquisicion en simulacion
   ESP32 - Wokwi

   Cadena:
   Sensor -> ADC -> Voltaje -> Magnitud fisica
          -> Error sistematico -> Calibracion -> Filtro -> Serial
   ========================================================================== */


// ---------------------------------------------------------------------------
// 1. CONFIGURACION DEL EQUIPO
// ---------------------------------------------------------------------------

const uint8_t PIN_SENSOR = 34;      // GPIO 34 -> ADC1 del ESP32
const uint32_t PERIODO_MS = 1000;   // [ms] una medicion cada 1 segundo

const uint8_t N_FILTRO = 5;         // Ventana de 5 muestras
const bool USAR_MEDIANA = false;    // false = media movil | true = mediana



const float V_REF = 3.3f;            // [V] referencia del ADC
const uint16_t CUENTAS_MAX = 4095;  // ADC de 12 bits: 0 a 4095

const float ESCALA_SENSOR = 60.0f;  // [unidad/V]
const float OFFSET_SENSOR = 2.80f;    // [unidad]


const bool SIMULAR_ERROR_SISTEMATICO = true;

// Error de ganancia y error de offset utilizados en la simulacion.
const float GANANCIA_ERROR_SIM = 1.05f;  // +5% de error
const float OFFSET_ERROR_SIM = 2.0f;     // [unidad]



const float R1 = 11.0f;      // [unidad] referencia real del punto 1
const float M1 = 14.18f;       // [unidad] lectura del sensor en punto 1

const float R2 = 37.0f;      // [unidad] referencia real del punto 2
const float M2 = 41.49f;       // [unidad] lectura del sensor en punto 2


// Variables donde el ESP32 calculara automaticamente m y b.
float M_CAL = 0.9520f;
float B_CAL = -2.50f;


// ---------------------------------------------------------------------------
// 5. SIMULACION DE RUIDO
// ---------------------------------------------------------------------------

const bool SIMULAR_RUIDO = true;
const float RUIDO_CUENTAS = 25.0f;  // [cuentas]


// ---------------------------------------------------------------------------
// 6. ESTADO INTERNO DEL FILTRO
// ---------------------------------------------------------------------------

uint32_t t_ultima_muestra = 0;

float ventana[16];

uint8_t idx_ventana = 0;
uint8_t muestras_validas = 0;


// ---------------------------------------------------------------------------
// 7. CONVERTIR CUENTAS DEL ADC A VOLTAJE Y MAGNITUD FISICA
// ---------------------------------------------------------------------------

float cuentas_a_fisica(int cuentas) {

  // Conversion:
  //
  // V = cuentas * V_REF / (2^N - 1)
  //
  // Para el ESP32:
  //
  // N = 12
  // 2^12 - 1 = 4095

  float tension = (V_REF * cuentas) / CUENTAS_MAX;

  // Conversion de voltaje a magnitud fisica:
  //
  // Magnitud = tension * escala + offset

  return tension * ESCALA_SENSOR + OFFSET_SENSOR;
}


// ---------------------------------------------------------------------------
// 8. SIMULAR ERROR SISTEMATICO
// ---------------------------------------------------------------------------

float simular_error_sistematico(float valor_nominal) {

  if (!SIMULAR_ERROR_SISTEMATICO) {
    return valor_nominal;
  }

  // Sensor imperfecto:
  //
  // M = ganancia * valor_nominal + offset

  return GANANCIA_ERROR_SIM * valor_nominal
         + OFFSET_ERROR_SIM;
}


// ---------------------------------------------------------------------------
// 9. CALIBRACION
// ---------------------------------------------------------------------------

float aplicar_calibracion(float valor_medido) {

  // Correccion de dos puntos:
  //
  // valor_calibrado = m * valor_medido + b

  return M_CAL * valor_medido + B_CAL;
}


// ---------------------------------------------------------------------------
// 10. MEDIA MOVIL
// ---------------------------------------------------------------------------

float media_movil() {

  if (muestras_validas == 0) {
    return 0.0f;
  }

  float suma = 0.0f;

  for (uint8_t i = 0; i < muestras_validas; i++) {
    suma += ventana[i];
  }

  return suma / muestras_validas;
}


// ---------------------------------------------------------------------------
// 11. MEDIANA
// ---------------------------------------------------------------------------

float mediana() {

  if (muestras_validas == 0) {
    return 0.0f;
  }

  float copia[16];

  // Copiar los valores
  for (uint8_t i = 0; i < muestras_validas; i++) {
    copia[i] = ventana[i];
  }

  // Ordenar de menor a mayor
  for (uint8_t i = 1; i < muestras_validas; i++) {

    float clave = copia[i];

    int8_t j = i - 1;

    while (j >= 0 && copia[j] > clave) {
      copia[j + 1] = copia[j];
      j--;
    }

    copia[j + 1] = clave;
  }

  // Devolver el valor central
  return copia[muestras_validas / 2];
}


// ---------------------------------------------------------------------------
// 12. SETUP
// ---------------------------------------------------------------------------

void setup() {

  Serial.begin(115200);

  // ADC de 12 bits:
  // rango 0 a 4095
  analogReadResolution(12);


  // -------------------------------------------------------------------------
  // CALCULO AUTOMATICO DE m Y b
  // -------------------------------------------------------------------------

  // Evitar una division por cero.
  if (M2 != M1) {

    // Pendiente:
    //
    // m = (R2 - R1) / (M2 - M1)

    M_CAL = (R2 - R1) / (M2 - M1);

    // Offset:
    //
    // b = R1 - m * M1

    B_CAL = R1 - M_CAL * M1;

  } else {

    // Si M1 y M2 son iguales, no se puede calcular la calibracion.

    M_CAL = 1.0f;
    B_CAL = 0.0f;

    Serial.println("ERROR: M1 y M2 no pueden ser iguales.");
  }


  // -------------------------------------------------------------------------
  // MOSTRAR LOS DATOS DE CALIBRACION
  // -------------------------------------------------------------------------

  Serial.println();
  Serial.println("======================================");
  Serial.println("      CALIBRACION DE DOS PUNTOS");
  Serial.println("======================================");

  Serial.print("R1 = ");
  Serial.println(R1, 2);

  Serial.print("M1 = ");
  Serial.println(M1, 2);

  Serial.print("R2 = ");
  Serial.println(R2, 2);

  Serial.print("M2 = ");
  Serial.println(M2, 2);

  Serial.println();

  Serial.print("m = ");
  Serial.println(M_CAL, 6);

  Serial.print("b = ");
  Serial.println(B_CAL, 6);

  Serial.println("======================================");
  Serial.println();


  // Cabecera CSV
  Serial.println(
    "t_ms,cuentas,nominal,medido,calibrado,filtrado"
  );
}


// ---------------------------------------------------------------------------
// 13. LOOP PRINCIPAL
// ---------------------------------------------------------------------------

void loop() {

  uint32_t ahora = millis();


  // -------------------------------------------------------------------------
  // ESPERAR HASTA QUE PASE EL PERIODO DE MUESTREO
  // -------------------------------------------------------------------------

  if (ahora - t_ultima_muestra >= PERIODO_MS) {

    t_ultima_muestra = ahora;


    // -----------------------------------------------------------------------
    // 1. LEER ADC DEL ESP32
    // -----------------------------------------------------------------------

    int cuentas = analogRead(PIN_SENSOR);


    // -----------------------------------------------------------------------
    // 2. AGREGAR RUIDO SIMULADO, SI ESTA ACTIVADO
    // -----------------------------------------------------------------------

    if (SIMULAR_RUIDO) {

      int ruido = random(
        -100,
        101
      );

      cuentas += (int)(
        ruido / 100.0f * RUIDO_CUENTAS
      );

      cuentas = constrain(
        cuentas,
        0,
        CUENTAS_MAX
      );
    }


    // -----------------------------------------------------------------------
    // 3. CONVERTIR CUENTAS A MAGNITUD FISICA
    // -----------------------------------------------------------------------

    float valor_nominal =
      cuentas_a_fisica(cuentas);


    // -----------------------------------------------------------------------
    // 4. SIMULAR EL ERROR DEL SENSOR
    // -----------------------------------------------------------------------

    float valor_medido =
      simular_error_sistematico(valor_nominal);


    // -----------------------------------------------------------------------
    // 5. APLICAR CALIBRACION
    // -----------------------------------------------------------------------

    float valor_calibrado =
      aplicar_calibracion(valor_medido);


    // -----------------------------------------------------------------------
    // 6. GUARDAR VALOR EN LA VENTANA DEL FILTRO
    // -----------------------------------------------------------------------

    ventana[idx_ventana] =
      valor_calibrado;

    idx_ventana =
      (idx_ventana + 1) % N_FILTRO;


    if (muestras_validas < N_FILTRO) {
      muestras_validas++;
    }


    // -----------------------------------------------------------------------
    // 7. APLICAR FILTRO
    // -----------------------------------------------------------------------

    float valor_filtrado;

    if (USAR_MEDIANA) {

      valor_filtrado = mediana();

    } else {

      valor_filtrado = media_movil();
    }


    // -----------------------------------------------------------------------
    // 8. MOSTRAR RESULTADOS
    // -----------------------------------------------------------------------

    Serial.printf(
      "%lu,%d,%.2f,%.2f,%.2f,%.2f\n",
      ahora,
      cuentas,
      valor_nominal,
      valor_medido,
      valor_calibrado,
      valor_filtrado
    );
  }
}