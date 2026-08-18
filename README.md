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
