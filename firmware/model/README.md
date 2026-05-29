# Model — Dataset tabular del nodo principal

Esta carpeta contiene los artefactos de datos del modelo de deteccion de anomalias vibracionales del nodo principal ESP32.

---

## Contenido

```
model/
└── dataset/
    └── dataset_balanced.csv   <- Dataset tabular balanceado (468 muestras)
```

---

## Dataset: `dataset_balanced.csv`

Dataset balanceado con 468 muestras de telemetria del nodo principal, etiquetadas con el estado operativo de la fresadora CNC.

### Columnas

| Columna | Tipo | Descripcion |
|---|---|---|
| `mean_x` | float | Media de aceleracion en el eje X (m/s2) |
| `var_x` | float | Varianza de aceleracion en el eje X |
| `mean_y` | float | Media de aceleracion en el eje Y (m/s2) |
| `var_y` | float | Varianza de aceleracion en el eje Y |
| `mean_z` | float | Media de aceleracion en el eje Z (m/s2) |
| `var_z` | float | Varianza de aceleracion en el eje Z |
| `temperature` | float | Temperatura (grados Celsius) |
| `humidity` | float | Humedad relativa (%) |
| `label` | int | Etiqueta: 0 = operacion normal, 1 = anomalia vibracional |

Las caracteristicas estadisticas (media, varianza) se calculan sobre una ventana de muestras del MPU-6050 (acelerometro 6-ejes) tal como lo realiza el firmware en `sensors.h`.

### Uso

El dataset puede usarse para:
- Entrenar o evaluar el modelo de clasificacion fuera de Edge Impulse
- Analisis exploratorio de las distribuciones de las variables sensoriales
- Validacion cruzada o comparacion de algoritmos clasicos vs. Edge Impulse

El modelo entrenado con Edge Impulse Studio se exporta como libreria Arduino y se integra directamente en el firmware `cnc_main_node.ino`.
