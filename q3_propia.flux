// Pregunta Agronómica: ¿Cómo se compara la humedad promedio en ventanas de 30 minutos entre la Zona 1 (Lechuga) y la Zona 2 (Tomate Enano) para evaluar la deshidratación del suelo durante las últimas 24 horas?
from(bucket: "mediciones")
  |> range(start: -24h)
  |> filter(fn: (r) => r._measurement == "mediciones")
  |> filter(fn: (r) => r.equipo == "E23" and r.proyecto == "P8" and r.nodo == "nodo1")
  |> filter(fn: (r) => r._field == "humedad_z1" or r._field == "humedad_z2")
  |> aggregateWindow(every: 30m, fn: mean, createEmpty: false)
  |> yield(name: "comparativa_zonas_30m")