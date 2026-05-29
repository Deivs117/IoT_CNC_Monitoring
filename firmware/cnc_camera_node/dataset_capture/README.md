# Dataset Capture — ESP32-CAM

Herramienta de automatizacion Python para capturar y organizar el dataset de imagenes de PCBs usado en el entrenamiento del modelo Edge Impulse del nodo ESP32-CAM.

---

## Prerequisitos

- Python 3.11 o superior (gestionado via [uv](https://docs.astral.sh/uv/))
- El firmware `capture_express.ino` cargado en el ESP32-CAM y accesible en red local
- OpenCV instalado (opcional — requerido solo si no se usa `--no-preview`)

---

## Instalacion de dependencias

```bash
cd firmware/cnc_camera_node/dataset_capture
uv sync
```

---

## Uso

```bash
# Capturar 60 imagenes de la clase PCB_SMD con previsualización
uv run python capture.py --host 192.168.1.50 --class PCB_SMD --count 60

# Capturar con delay personalizado entre imagenes
uv run python capture.py --host 192.168.1.50 --class PCB_TH --count 40 --delay 2.0

# Capturar sin ventana de previsualización (entornos sin pantalla)
uv run python capture.py --host 192.168.1.50 --class Sin_PCB --count 40 --no-preview
```

### Clases validas

| Clase | Descripcion |
|---|---|
| `PCB_Mixta` | PCB con componentes through-hole y SMD coexistiendo |
| `PCB_SMD` | PCB con componentes de montaje superficial unicamente |
| `PCB_TH` | PCB con componentes through-hole / insercion unicamente |
| `Sin_PCB` | Bancada vacia, sin placa visible |

---

## Flujo de captura (con previsualización)

1. El script se conecta al endpoint `GET /capture` del firmware `capture_express`
2. Muestra una ventana OpenCV con el frame en tiempo real para verificar orientacion, enfoque e iluminacion
3. El operador presiona `ENTER` o `s` para confirmar e iniciar la rafaga de capturas
4. Durante la rafaga la ventana sigue mostrando el frame actual
5. Las imagenes se guardan en `dataset/<CLASE>/` con nomenclatura `CLASE_YYYYMMDD_HHMMSS_millis_NNNN.jpg`
6. Al finalizar se muestra un resumen del numero de imagenes capturadas

---

## Estructura generada

```
dataset/
├── PCB_Mixta/    <- PCB con componentes SMD + Through-Hole
├── PCB_SMD/      <- PCB exclusivamente SMD
├── PCB_TH/       <- PCB exclusivamente Through-Hole
└── Sin_PCB/      <- Bancada vacia
```

Esta carpeta es generada por `capture.py` y las imagenes estan excluidas del control de versiones (ver `.gitignore`).

---

## Validacion de imagenes

El script valida cada imagen capturada antes de guardarla:
- Verifica que el `Content-Type` de la respuesta sea `image/jpeg`
- Verifica que el tamano del archivo supere un minimo configurable

Las imagenes que no pasen la validacion se descartan sin interrumpir la rafaga.

---

## Proximos pasos tras la captura

1. Subir las carpetas `dataset/<CLASE>/` a [Edge Impulse Studio](https://studio.edgeimpulse.com)
2. Entrenar el modelo de clasificacion de imagenes
3. Exportar la libreria Arduino desde `Deployment -> Arduino library`
4. Copiar la libreria exportada y compilar `cnc_camera_node.ino`
