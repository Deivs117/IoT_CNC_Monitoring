import os

import requests

# ---------------------------------
# CONFIGURACION
# ---------------------------------
# Leer credenciales desde variables de entorno, nunca hardcodeadas.
# Exporta antes de ejecutar:
#   export TELEGRAM_BOT_TOKEN="tu_token"
#   export TELEGRAM_CHAT_ID="tu_chat_id"
TELEGRAM_TOKEN = os.environ.get("TELEGRAM_BOT_TOKEN", "")
TELEGRAM_CHAT_ID = os.environ.get("TELEGRAM_CHAT_ID", "")

# ---------------------------------
# DATOS SIMULADOS DEL MODELO DE IA
# (Aqui luego conectas la salida real del modelo)
# ---------------------------------
modelo_output = {
    "estado": "FALLO",       # "NORMAL", "ADVERTENCIA", "FALLO"
    "confianza": 0.94,       # probabilidad del modelo (0.0 - 1.0)
    "vibracion": 4.7,        # valor de vibracion (g)
    "temperatura": 72.3,     # temperatura (grados C)
    "timestamp": "2026-05-28 10:35:00"
}

# ---------------------------------
# UMBRALES DE ALARMA
# ---------------------------------
UMBRALES = {
    "ADVERTENCIA": 0.60,
    "FALLO": 0.80
}

# ---------------------------------
# FUNCION: Enviar mensaje por Telegram
# ---------------------------------
def enviar_telegram(mensaje: str):
    if not TELEGRAM_TOKEN or not TELEGRAM_CHAT_ID:
        print("TELEGRAM_BOT_TOKEN o TELEGRAM_CHAT_ID no están configurados. Exporta las variables de entorno.")
        return None
    url = f"https://api.telegram.org/bot{TELEGRAM_TOKEN}/sendMessage"
    payload = {
        "chat_id": TELEGRAM_CHAT_ID,
        "text": mensaje,
        "parse_mode": "Markdown"
    }
    response = requests.post(url, json=payload)
    if response.status_code == 200:
        print("Mensaje enviado por Telegram")
    else:
        print(f"Error al enviar: {response.status_code} - {response.text}")
    return response

# ---------------------------------
# FUNCION: Evaluar alarma
# ---------------------------------
def evaluar_alarma(datos: dict):
    estado = datos["estado"]
    confianza = datos["confianza"]

    if estado == "FALLO" and confianza >= UMBRALES["FALLO"]:
        nivel = "*ALARMA CRITICA - FALLO DETECTADO*"
    elif estado == "ADVERTENCIA" and confianza >= UMBRALES["ADVERTENCIA"]:
        nivel = "*ADVERTENCIA - Anomalia detectada*"
    elif estado == "NORMAL":
        print("Estado NORMAL - Sin alarma.")
        return
    else:
        print("Estado desconocido o confianza baja - Sin alarma.")
        return

    mensaje = (
        f"{nivel}\n\n"
        f"Timestamp: {datos['timestamp']}\n"
        f"Estado del modelo: {datos['estado']}\n"
        f"Confianza: {confianza * 100:.1f}%\n"
        f"Vibracion: {datos['vibracion']} g\n"
        f"Temperatura: {datos['temperatura']} grados C\n\n"
        f"Revisar maquina CNC inmediatamente."
    )

    enviar_telegram(mensaje)

# ---------------------------------
# MAIN
# ---------------------------------
if __name__ == "__main__":
    print("Evaluando salida del modelo de IA...")
    evaluar_alarma(modelo_output)
