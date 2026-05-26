/**
 * edge_impulse_vibration.h
 * Adaptador del SDK de Edge Impulse para el nodo principal CNC PCB.
 *
 * MIGRACIÓN:
 *   El firmware anterior usaba un modelo TFLite manual con pesos incrustados
 *   (g_model[]), escaladores (SCALER_MEAN/SCALER_STD) e inferencia a través de
 *   Chirale_TensorFlowLite. Ese enfoque ha sido descartado.
 *
 *   Este archivo ahora incluye el SDK exportado por Edge Impulse:
 *     CNC_Monitor_Project_inferencing.h
 *
 *   El SDK proporciona de forma nativa:
 *     - run_classifier()          — inferencia completa con DSP integrado
 *     - signal_t                  — descriptor de datos de entrada
 *     - ei_impulse_result_t       — resultado de inferencia con etiquetas y scores
 *     - EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE — tamaño del buffer de entrada
 *     - EI_CLASSIFIER_LABEL_COUNT          — número de clases
 *     - EI_CLASSIFIER_INTERVAL_MS          — intervalo de muestreo esperado
 *     - result.classification[i].label     — nombre de cada clase
 *     - result.classification[i].value     — probabilidad de cada clase
 *
 * REQUISITO:
 *   Instalar la librería exportada desde Edge Impulse Studio como Arduino library:
 *     CNC_Monitor_Project_inferencing (library.properties: version=1.0.2)
 *
 *   En Arduino IDE: Sketch → Include Library → Add .ZIP Library → seleccionar
 *   el .zip descargado desde Edge Impulse Studio (Deployment → Arduino library).
 */

#pragma once

#include <CNC_Monitor_Project_inferencing.h>
