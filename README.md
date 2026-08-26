# iot-2026-e23-p8-riego

# Guía de Taller 1 (GT1) - Grupo E23

## 1. Enlace al Proyecto Wokwi
* **Simulación Wokwi:** https://wokwi.com/projects/472715012705768449

## 2. Calibración de Dos Puntos
* **Referencia 1 ($R_1$):** 11.0
* **Lectura Medida 1 ($M_1$):** 14.18
* **Referencia 2 ($R_2$):** 37.0
* **Lectura Medida 2 ($M_2$):** 41.49
* **Ganancia ($m$):** 0.9520
* **Offset ($b$):** -2.50
* **Verificación ($R_3$):** 23.0 (Lectura obtenida en calibrado: ~22.75)
* **Tolerancia declarada:** ±0.5 °C (o la magnitud física de tu proyecto)






## 3. Configuración del Filtro
* **Ventana ($N$):** 5 muestras
* **Tipo de filtro:** Media móvil
* **Criterio de elección de $N$:** Se eligió N = 5 para atenuar el ruido del ADC sin introducir un retardo excesivo en la respuesta del sistema ante cambios reales de la variable.
Sensor: humedad de suelo capacitivo (2 unidades montadas) | Familia: A (analogico por ADC)
Referencia: dos condiciones conocidas, aire (0 %) y agua (100 %)
Referencia validada por: <Reinier Rodriguez Guillen Yainet Garcia Garcia>

### Condiciones de la medicion (se fijan ANTES de medir)
| Condicion                          | Valor declarado |
|------------------------------------|-----------------|
| Tension de alimentacion MEDIDA     | <V> (obligatorio 3,3 V, verificado con multimetro) |
| Canales empleados                  | Z1: GPIO 32, Z2: GPIO 33 (ADC1) |
| Atenuacion del convertidor         | 11 dB, lectura en mV calibrados de fabrica |
| Divisor a la entrada               | NO se emplea (justificacion en la hoja de conexion) |
| Lectura en aire dentro de la zona util | si / no (si toco el tope, NO se calibro) |
| Tiempo de estabilizacion en cada punto | <s> antes de promediar |
| Sustrato de cada zona              | Z1: <cual>  Z2: <cual> |
| Profundidad de insercion           | hasta <cm>, MARCADA con cinta o plumon |
| Temperatura ambiente               | <°C> |
| N de la media movil                | 5 |

### Tolerancia declarada ANTES de verificar
| Criterio                                          | Tolerancia aceptada |
|---------------------------------------------------|---------------------|
| Dispersion aceptada en condicion estable, por zona | <+/- puntos porcentuales> |
| Separacion minima exigida entre aire y agua        | <mV> |
| Reproducibilidad del punto de agua entre repeticiones | <+/- mV> |

### Calibracion de dos puntos, una fila por zona (en milivolts)
| Zona | mV en aire | mV en agua | Separacion (mV) | m (%/mV) | b (%) |
|------|------------|------------|-----------------|----------|-------|
| 1    | 2314.9   | 280.3    | 2034.7        | -0.049148 | 113.7742    |
| 2    | 2246.2   | 913.7    | 1332.5        | -0.075049 | 168.5719    |

La pendiente m es NEGATIVA en ambas zonas: a mayor humedad, menor lectura.

Reproducibilidad del punto de agua (se repite al menos dos veces por zona):
| Zona | Repeticion 1 (mV) | Repeticion 2 (mV) | Diferencia | Cabe en la tolerancia |
|------|-------------------|-------------------|------------|------------------------|
| 1    | 283                | 354                | 71mV         | no                |
| 2    | 310                | 322               | 12mV         | si               |

Si el punto de agua no reproduce, la condicion no esta controlada: casi siempre
es la profundidad de insercion. No se elige la repeticion que da el resultado
mas bonito; se controla la condicion y se repiten ambas.

### Verificacion en el tercer punto (tierra humeda)
| Zona | mV  | Porcentaje | Valor SIN recortar | Estable y repetible |
|------|-----|------------|--------------------|---------------------|
| 1    | 154  | 100%         | 106.2%                 | no             |
| 2    | 220  | 100%        | 151.1%                 | no             |

El valor sin recortar importa: un 100 % en pantalla no distingue entre llegar
justo y pasarse. Si al sumergir el valor real supera 100, la lectura de hoy es
menor que la del dia de la calibracion y la referencia no reproduce.

No existe patron de humedad en el laboratorio: la verificacion es de COHERENCIA
(0 % al aire, 100 % en agua, valor intermedio estable en tierra humeda), no de
exactitud contra un instrumento de referencia.

### Dispersion medida (paso 4 + script)
| Zona | Condicion registrada | Media (%) | Dispersion (%) | Banda minima (k x disp) |
|------|----------------------|-----------|----------------|-------------------------|
| 1    | <aire / tierra>      | <>        | <>             | <>                      |
| 2    | <>                   | <>        | <>             | <>                      |

k declarado: <valor>

### Contraste con la GT1 (simulacion)
En la GT1 el equipo calibro un sensor analogico simulado, donde el par (m, b)
corregia un error sembrado por software. Aqui la cadena es la misma —cuentas del
ADC1, dos puntos, filtro— pero el error ya existe y no se inyecta, y el modelo
no se corrige: se construye, porque el sensor no tiene funcion de transferencia
publicada.

| Aspecto              | Simulacion (GT1) | Fisico (S4) |
|----------------------|------------------|-------------|
| Origen del error     | sembrado         | propio de cada ejemplar |
| Numero de pares      | 1                | 2, uno por zona |
| Desviacion observada | ---              | <diferencia entre los tres pares> |

### Hallazgo del equipo
<Comparar los dos pares entre si. Indicar cuanto difieren y cual zona resulto la
mas ruidosa, y explicar por que eso obliga a darle una banda de histeresis mayor
que a la otra.>

### Limitaciones registradas
- La escala construida vale para el ejemplar, el sustrato Y la profundidad de
  insercion declarados. Cambiar cualquiera de los tres obliga a recalibrar.
- La lectura se toma en milivolts calibrados de fabrica. El convertidor del
  ESP32 responde de forma util entre unos 150 y 2450 mV: fuera de ese rango la
  medicion se comprime o se recorta.
- El porcentaje informado es una posicion relativa entre aire y agua, no un
  contenido volumetrico de agua medido contra patron.
- La dispersion registrada corresponde a la condicion declarada. En otra
  condicion, la dispersion puede ser distinta.
- P8 es un prototipo educativo.

## Grafo de Estados (FSM)

![Grafo de estados FSM](1000385349 (1).jpg)
