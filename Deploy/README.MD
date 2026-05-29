# Deploy del proyecto IoT_CNC_Monitoring desde Azure Cloud Shell

Este documento describe el uso de los scripts de la carpeta `Deploy/` del repositorio `Deivs117/IoT_CNC_Monitoring` para aprovisionar infraestructura, desplegar backend y publicar el frontend, trabajando directamente desde **Azure Cloud Shell** en el portal de Azure.

La guía está pensada para que el despliegue sea:

- repetible,
- práctico,
- seguro,
- fácil de ejecutar desde navegador,
- resistente a desconexiones de sesión.

---

## Flujo de despliegue completo

```mermaid
flowchart TD
    A([Azure Cloud Shell]) -->|git clone| B[Repositorio local]
    B -->|01_infraestructura.sh| C["Azure Resource Group\n+ IoT Hub\n+ Cosmos DB\n+ Storage\n+ 2 dispositivos IoT"]
    C -->|02_backend.sh| D["Azure Function App\n5 functions desplegadas"]
    D -->|03_frontend_hosting.sh| E["Static Website\nfrontend/index.html + app.js"]

    C --> F["infra_outputs.env\n(FUNC_BASE_URL, COSMOSDB_*, IOTHUB_*)"]
    F --> D
    F --> E

    style C fill:#0078D4,color:#fff
    style D fill:#0078D4,color:#fff
    style E fill:#0078D4,color:#fff
```

## Infraestructura desplegada

```mermaid
graph LR
    subgraph Azure["Azure Resource Group rg-cnc-iot"]
        HUB["IoT Hub\ncnc-iot-hub\ndevices: cnc_fresadora_01\n         cnc_camera_01"]
        COSMOS["Cosmos DB Serverless\ncnc-iot-cosmos\nCNCMonitor/Telemetry"]
        FUNC["Function App\ncnc-iot-func\n• telemetry_processor\n• get_datos\n• descargar_csv\n• control_actuador\n• ingestar_camara"]
        STORAGE["Storage (Functions)\ncnciotfunc<hash>"]
        STATICWEB["Storage (Frontend)\ncnciotfront<hash>\nStatic Website"]
    end

    ESP1["ESP32 principal\ncnc_fresadora_01"] -->|MQTTS 8883| HUB
    ESP2["ESP32-CAM\ncnc_camera_01"] -->|MQTTS 8883| HUB
    HUB --> FUNC
    FUNC --> COSMOS
    COSMOS --> FUNC
    FUNC --> STATICWEB
    STORAGE -.->|AzureWebJobsStorage| FUNC
```

---

# 1. Objetivo de esta carpeta

La carpeta `Deploy/` contiene scripts Bash para automatizar el despliegue cloud del sistema.

## Scripts incluidos

| Script | Propósito |
|---|---|
| `01_infraestructura.sh` | Aprovisiona la infraestructura base en Azure |
| `02_backend.sh` | Crea y despliega Azure Functions |
| `03_frontend_hosting.sh` | Publica el frontend estático |
| `04_cleanup.sh` | Elimina todo el entorno desplegado |
| `deploy.sh` | Orquestador principal del flujo completo o parcial |
| `infra_outputs.env.template` | Plantilla de referencia para variables y outputs |

---

# 2. Qué despliega exactamente

## Infraestructura y componentes

Los scripts permiten desplegar:

- **Azure Resource Group**
- **Azure IoT Hub**
- **Registro de dos dispositivos ESP32** (`cnc_fresadora_01` y `cnc_camera_01`)
- **Azure Cosmos DB (Serverless)**
- **Azure Function App**
- **Storage Account para Azure Functions**
- **Storage Account para hosting estático del frontend**
- **Configuración inicial de CORS**
- **Inyección de variables para el frontend**

---

# 3. Escenario recomendado de uso

Dado que el despliegue se hará desde la **terminal Shell del portal de Azure**, el flujo recomendado es:

1. Abrir **Azure Cloud Shell** en modo **Bash**
2. Clonar el repositorio
3. Cargar/exportar secretos
4. Ejecutar scripts desde `Deploy/`
5. Respaldar archivos sensibles para no perderlos si la sesión se desconecta

---

# 4. Requisitos previos

Antes de ejecutar el despliegue, asegúrate de tener:

- acceso al portal de Azure,
- permisos suficientes sobre la suscripción,
- acceso a **Azure Cloud Shell**,
- `git` disponible,
- `az` CLI autenticado,
- Azure Functions Core Tools disponibles si vas a publicar Functions desde Cloud Shell.

---

# 5. Abrir Azure Cloud Shell

1. Entra al portal de Azure.
2. Haz clic en el icono de **Cloud Shell**.
3. Selecciona **Bash**.
4. Espera a que la terminal termine de cargar.

> Importante: usa **Bash**, no PowerShell, porque todos los scripts de esta carpeta están escritos para shell Bash.

---

# 6. Clonar el repositorio

Desde Azure Cloud Shell:

```bash
cd ~
git clone https://github.com/Deivs117/IoT_CNC_Monitoring.git
cd IoT_CNC_Monitoring
```

Verifica que la carpeta exista correctamente:

```bash
ls -la
ls -la Deploy
```

# 7. Orden natural del despliegue

La secuencia recomendada para desplegar el proyecto es la siguiente:

## Opción A: ejecución paso a paso

```bash
cd ~/IoT_CNC_Monitoring/Deploy

bash 01_infraestructura.sh
bash 02_backend.sh
bash 03_frontend_hosting.sh
```

## Opción B: uso del orquestador principal

```bash
cd ~/IoT_CNC_Monitoring/Deploy

bash deploy.sh
```

## Opción C: despliegue parcial

### Solo infraestructura

```bash
cd ~/IoT_CNC_Monitoring/Deploy

bash deploy.sh --only-infra
```

### Solo backend

```bash
cd ~/IoT_CNC_Monitoring/Deploy

bash deploy.sh --only-backend
```

### Solo frontend

```bash
cd ~/IoT_CNC_Monitoring/Deploy

bash deploy.sh --only-front
```

### Omitir infraestructura

```bash
cd ~/IoT_CNC_Monitoring/Deploy

bash deploy.sh --no-infra
```

### Omitir backend

```bash
cd ~/IoT_CNC_Monitoring/Deploy

bash deploy.sh --no-backend
```

### Omitir frontend

```bash
cd ~/IoT_CNC_Monitoring/Deploy

bash deploy.sh --no-front
```

---

# Hacer ejecutables todos los scripts `.sh`

Si quieres ejecutar los scripts directamente con `./nombre_script.sh`, primero debes darles permisos de ejecución.

## Comando para hacer ejecutables todos los scripts de `Deploy/`

```bash
cd ~/IoT_CNC_Monitoring/Deploy

chmod +x *.sh
```

## Verificar permisos

```bash
ls -l *.sh
```

## Ejecutar después de dar permisos

```bash
./01_infraestructura.sh
./02_backend.sh
./03_frontend_hosting.sh
./04_cleanup.sh
./deploy.sh
```

---

# Recomendación práctica

Aunque los hagas ejecutables, para evitar errores de ruta o de contexto, sigue siendo buena práctica ejecutar desde la carpeta `Deploy/`:

```bash
cd ~/IoT_CNC_Monitoring/Deploy
./deploy.sh
```

O si prefieres no depender de permisos:

```bash
cd ~/IoT_CNC_Monitoring/Deploy
bash deploy.sh
```

# 8. Consideraciones importantes sobre Azure Cloud Shell

Azure Cloud Shell puede cerrar la sesión visual si:

- se cierra la pestaña del navegador,
- se interrumpe la conexión,
- expira la sesión web,
- se recarga el portal.

## Qué se conserva y qué no

### Normalmente se conserva
- los archivos guardados en tu directorio personal (`~`),
- el repositorio clonado,
- los archivos `.env` o `.sh` que guardes manualmente.

### No se conserva automáticamente
- las variables exportadas solo con `export`,
- el contexto temporal de la sesión shell,
- valores no guardados a disco.

## Conclusión práctica
Si exportas secretos únicamente con `export`, los perderás cuando se cierre la sesión.  
Por eso, la práctica correcta es:

- guardar los secretos en un archivo privado,
- luego usar `source` para cargarlos cuando sea necesario.

---

# 9. Variables y secretos que pueden intervenir

## Variables más importantes

Estas variables pueden ser necesarias durante el despliegue:

- `TELEGRAM_BOT_TOKEN`
- `TELEGRAM_CHAT_ID`

Y opcionalmente puedes sobreescribir nombres de recursos:

- `RG_NAME`
- `LOCATION`
- `IOT_HUB_NAME`
- `IOT_DEVICE_ID`
- `COSMOS_ACCOUNT`
- `FUNC_APP_NAME`
- `FUNC_STORAGE`
- `FRONTEND_SA`

Si no defines estas variables, los scripts usarán sus valores por defecto.

---

## 9.1. Configuración de Telegram en Azure Functions

Las variables `TELEGRAM_BOT_TOKEN` y `TELEGRAM_CHAT_ID` son necesarias para que
el backend envíe notificaciones de alerta al bot de Telegram.  
**No deben hardcodearse en el código.** Deben inyectarse como App Settings del Function App.

## Cómo fluyen al Function App

`02_backend.sh` las toma del entorno de la sesión y las inyecta automáticamente
en los App Settings del Function App con este comando:

```bash
az functionapp config appsettings set \
  --name "$FUNC_APP_NAME" \
  --resource-group "$RG_NAME" \
  --settings \
    "TELEGRAM_BOT_TOKEN=${TELEGRAM_BOT_TOKEN:-}" \
    "TELEGRAM_CHAT_ID=${TELEGRAM_CHAT_ID:-}"
```

Por eso debes definirlas **antes** de ejecutar `02_backend.sh` (ver sección 10).

## Cómo configurarlas manualmente (sin re-ejecutar 02_backend.sh)

Si necesitas actualizarlas sin volver a desplegar todo el backend:

```bash
# Cargar variables del archivo de secretos
source ~/iot_cnc_secrets.env
source Deploy/infra_outputs.env

# Actualizar solo los App Settings de Telegram
az functionapp config appsettings set \
  --name "$FUNC_APP_NAME" \
  --resource-group "$RG_NAME" \
  --settings \
    "TELEGRAM_BOT_TOKEN=${TELEGRAM_BOT_TOKEN}" \
    "TELEGRAM_CHAT_ID=${TELEGRAM_CHAT_ID}"
```

## Cómo verificar que están activas en el Function App

### Vía Azure CLI

```bash
source Deploy/infra_outputs.env

az functionapp config appsettings list \
  --name "$FUNC_APP_NAME" \
  --resource-group "$RG_NAME" \
  --query "[?name=='TELEGRAM_BOT_TOKEN' || name=='TELEGRAM_CHAT_ID']" \
  --output table
```

La salida debe mostrar ambas variables con un valor no vacío.

### Vía portal de Azure

1. Abre el Function App en el portal de Azure.
2. Ve a **Settings → Environment variables** (o **Configuration → Application settings**).
3. Busca `TELEGRAM_BOT_TOKEN` y `TELEGRAM_CHAT_ID`.
4. Verifica que ambas tienen valor asignado.

> **No copies el token en pantalla si estás grabando o compartiendo sesión.**

## Comportamiento anti-spam (cooldown de 5 minutos)

El backend implementa protección anti-spam en `shared_code/alerts.py`:

- **Primera detección de fallo**: se envía la alerta de inmediato.
- **Fallo persistente**: se suprime la notificación mientras no hayan pasado
  5 minutos desde el último envío.
- **Recordatorio**: al cumplirse el cooldown y si la condición sigue activa,
  se envía un recordatorio automáticamente.
- **Recuperación** (vuelta a estado NORMAL): se limpia el estado en memoria,
  permitiendo que un nuevo fallo vuelva a disparar la alerta inmediatamente.

El intervalo de cooldown puede ajustarse sin tocar código usando la variable:

```bash
az functionapp config appsettings set \
  --name "$FUNC_APP_NAME" \
  --resource-group "$RG_NAME" \
  --settings "ALERT_COOLDOWN_SECONDS=300"
```

El valor por defecto es `300` (5 minutos).

## Resumen de variables de Telegram

| Variable | Descripción | Obligatoria |
|---|---|---|
| `TELEGRAM_BOT_TOKEN` | Token del bot obtenido desde BotFather | Sí (para alertas) |
| `TELEGRAM_CHAT_ID` | ID del chat o grupo destino de las alertas | Sí (para alertas) |
| `ALERT_COOLDOWN_SECONDS` | Segundos entre recordatorios de fallo activo | No (default: 300) |

> Si `TELEGRAM_BOT_TOKEN` o `TELEGRAM_CHAT_ID` están vacíos, las alertas se
> deshabilitan silenciosamente sin interrumpir el procesamiento de telemetría.

---

# 10. Prompt interactivo de Telegram — nuevo flujo automático

A partir de la versión actual de los scripts, **no es necesario exportar manualmente** las variables de Telegram antes de desplegar.

`deploy.sh` y `02_backend.sh` incluyen el helper `_shared_env.sh` que gestiona todo el proceso automáticamente.

## Flujo automático

```
bash deploy.sh
   │
   ├─ ¿Existe ~/iot_cnc_secrets.env? ──Sí──▶ carga valores automáticamente
   │                                          │
   │                                          ├─ ¿Faltan variables? ──No──▶ continúa sin preguntar
   │                                          └─ ¿Faltan variables? ──Sí──▶ pide las que falten
   │
   └─ No existe archivo ──▶ pide TELEGRAM_BOT_TOKEN (oculto) y TELEGRAM_CHAT_ID
                             │
                             └─ ¿Guardar en ~/iot_cnc_secrets.env? [s/N]
                                  │
                                  Sí ──▶ guarda con permisos 600
                                  No ──▶ disponible solo esta sesión
```

## Primer despliegue (sin archivo de secretos)

```bash
cd ~/IoT_CNC_Monitoring/Deploy
bash deploy.sh
```

El script mostrará:

```
──────────────────────────────────────────────────────
  Configuración de Telegram para alertas de CNC
──────────────────────────────────────────────────────
  No se encontró '/home/user/iot_cnc_secrets.env'.
  Se pedirán las credenciales de Telegram.

  Si no tienes un bot de Telegram configurado, presiona
  Enter dos veces para omitir (las alertas quedarán deshabilitadas).
──────────────────────────────────────────────────────

  Introduce TELEGRAM_BOT_TOKEN: ****  ← sin eco en pantalla
  Introduce TELEGRAM_CHAT_ID: 123456789

  ¿Guardar credenciales en '/home/user/iot_cnc_secrets.env' para próximas ejecuciones? [s/N]: s
  ✅ Guardado en '/home/user/iot_cnc_secrets.env' (permisos 600).
```

Después continúa automáticamente con infraestructura, backend y frontend.

## Re-despliegue (con archivo de secretos ya guardado)

```bash
bash deploy.sh
```

No se vuelven a pedir las credenciales — se cargan automáticamente desde `~/iot_cnc_secrets.env`.

## Ejecución parcial

```bash
bash deploy.sh --only-backend
bash deploy.sh --no-infra
bash deploy.sh --only-front
```

El prompt de Telegram aparece igual antes de ejecutar cualquier fase.

## Ejecución de `02_backend.sh` por separado

```bash
bash 02_backend.sh
```

El helper `_shared_env.sh` también está incluido en `02_backend.sh`, por lo que el comportamiento es idéntico: carga desde archivo si existe, o pide interactivamente si faltan.

## Resumen al finalizar

Al terminar `deploy.sh`, el resumen final muestra si Telegram quedó configurado:

```
╔══════════════════════════════════════════════════════════╗
║           ✅  Despliegue completado                      ║
╠══════════════════════════════════════════════════════════╣
║  IoT Hub:      cnc-iot-hub                              ║
║  Cosmos DB:    cnc-iot-cosmos                           ║
║  Function App: cnc-iot-func                             ║
║  API URL:      https://cnc-iot-func.azurewebsites.net   ║
║  Dashboard:    https://cnciotfront...web.core.windows.net║
║  Telegram:     ✅ credenciales cargadas                 ║
║  Tiempo total: 245s                                     ║
╚══════════════════════════════════════════════════════════╝
```

---

# 11. Cómo actualizar o cambiar las credenciales de Telegram

## Opción A: editar manualmente el archivo

```bash
nano ~/iot_cnc_secrets.env
```

Modifica las líneas correspondientes y guarda. La próxima vez que ejecutes `deploy.sh` o `02_backend.sh` se usarán los nuevos valores.

## Opción B: borrar el archivo y que el prompt lo recree

```bash
rm ~/iot_cnc_secrets.env
bash deploy.sh
# el script pedirá nuevamente las credenciales
```

## Opción C: actualizar solo los App Settings en Azure sin redesplegar

```bash
source ~/iot_cnc_secrets.env
source ~/IoT_CNC_Monitoring/Deploy/infra_outputs.env

az functionapp config appsettings set \
  --name "$FUNC_APP_NAME" \
  --resource-group "$RG_NAME" \
  --settings \
    "TELEGRAM_BOT_TOKEN=${TELEGRAM_BOT_TOKEN}" \
    "TELEGRAM_CHAT_ID=${TELEGRAM_CHAT_ID}"
```

---

# 12. Método recomendado para exportar secretos sin perderlos

La mejor práctica en Azure Cloud Shell es usar un archivo persistente con permisos restringidos.

**El script `deploy.sh` lo crea automáticamente** si lo aceptas en el prompt (ver sección 10).  
Si prefieres crearlo manualmente:

## Crear el archivo manualmente

```bash
cat > ~/iot_cnc_secrets.env <<'EOF'
export TELEGRAM_BOT_TOKEN="TU_TOKEN_REAL"
export TELEGRAM_CHAT_ID="TU_CHAT_ID_REAL"

# Opcionales: overrides de nombres o región
export RG_NAME="rg-cnc-iot"
export LOCATION="eastus"
export IOT_HUB_NAME="cnc-iot-hub"
export IOT_DEVICE_ID="cnc_fresadora_01"
export COSMOS_ACCOUNT="cnc-iot-cosmos"
export FUNC_APP_NAME="cnc-iot-func"

# INSTANCE_SUFFIX: sufijo corto y legible para Storage Accounts globalmente únicos.
# Elige un valor estable y corto (ej. iniciales o alias del proyecto).
# Con INSTANCE_SUFFIX="deivid" se crean: cnciotfuncdeivid, cnciotfrontdeivid
export INSTANCE_SUFFIX="deivid"
EOF
```

## Paso 2: proteger el archivo

```bash
chmod 600 ~/iot_cnc_secrets.env
```

## Paso 3: cargar las variables en la sesión actual

```bash
source ~/iot_cnc_secrets.env
```

## Paso 4: verificar que cargaron

```bash
echo "$RG_NAME"
echo "$LOCATION"
echo "$FUNC_APP_NAME"
```

> Recomendación: no imprimas tokens sensibles en pantalla salvo que sea estrictamente necesario.

---

# 13. Cómo evitar perder variables si se desconecta la terminal

Si Azure Cloud Shell se desconecta, las variables cargadas con `export` desaparecen de la sesión.

## Para recuperarlas
Solo necesitas volver a ejecutar:

```bash
source ~/iot_cnc_secrets.env
```

Esto restaurará tus variables de entorno.

---

# 14. Cómo cargar automáticamente los secretos al abrir Cloud Shell

Si quieres automatizar la carga en futuras sesiones, puedes agregar esto a `~/.bashrc`:

```bash
if [ -f "$HOME/iot_cnc_secrets.env" ]; then
  source "$HOME/iot_cnc_secrets.env"
fi
```

Puedes hacerlo así:

```bash
echo 'if [ -f "$HOME/iot_cnc_secrets.env" ]; then source "$HOME/iot_cnc_secrets.env"; fi' >> ~/.bashrc
source ~/.bashrc
```

## Ventaja
Cada nueva sesión Bash cargará tus variables automáticamente.

## Precaución
Haz esto solo si:
- tu Cloud Shell es de uso personal,
- entiendes que esos secretos quedarán disponibles automáticamente en nuevas sesiones.

---

# 15. Archivo `infra_outputs.env` y por qué es tan importante

El script `01_infraestructura.sh` genera el archivo:

```bash
Deploy/infra_outputs.env
```

Este archivo contiene:

- nombres de recursos creados,
- cadenas de conexión,
- outputs intermedios del despliegue,
- placeholders para URL del backend,
- placeholders para URL del frontend,
- valores reutilizados por `02_backend.sh` y `03_frontend_hosting.sh`.

## Importante
Este archivo:

- contiene datos sensibles,
- no debe subirse al repositorio,
- debe mantenerse con permisos restringidos.

El script le aplica:

```bash
chmod 600 Deploy/infra_outputs.env
```

---

# 16. Respaldo recomendado de `infra_outputs.env`

Después de ejecutar `01_infraestructura.sh` o `deploy.sh`, crea una copia adicional:

```bash
cp Deploy/infra_outputs.env ~/infra_outputs.env.backup
chmod 600 ~/infra_outputs.env.backup
```

Esto te protege en caso de:

- borrar accidentalmente el repo clonado,
- sobreescribir el archivo,
- ejecutar limpieza local,
- cometer errores al editar.

## Restauración

```bash
cp ~/infra_outputs.env.backup ~/IoT_CNC_Monitoring/Deploy/infra_outputs.env
chmod 600 ~/IoT_CNC_Monitoring/Deploy/infra_outputs.env
```

---

# 17. Uso detallado de cada script

---

# 17.1 `01_infraestructura.sh`

## Qué hace

Crea la infraestructura base en Azure:

- Resource Group
- IoT Hub
- registro del dispositivo ESP32
- Cosmos DB en modo Serverless
- base de datos `CNCMonitor`
- contenedor `Telemetry`
- Storage Account para Functions
- Storage Account para frontend

También genera:

```bash
Deploy/infra_outputs.env
```

## Cómo ejecutarlo

Desde la raíz del repo:

```bash
bash Deploy/01_infraestructura.sh
```

O desde la carpeta `Deploy/`:

```bash
cd Deploy
bash 01_infraestructura.sh
```

## Variables de entrada opcionales

Puedes definir antes de ejecutarlo:

- `RG_NAME`
- `LOCATION`
- `IOT_HUB_NAME`
- `IOT_DEVICE_ID`
- `COSMOS_ACCOUNT`
- `FUNC_APP_NAME`
- `FUNC_STORAGE`
- `FRONTEND_SA`
- `TELEGRAM_BOT_TOKEN`
- `TELEGRAM_CHAT_ID`

## Qué genera

El archivo `infra_outputs.env` contendrá variables como:

- `RG_NAME`
- `LOCATION`
- `IOT_HUB_NAME`
- `IOT_DEVICE_ID`
- `IOTHUB_EVENTS_CONNECTION_STRING`
- `IOTHUB_SERVICE_CONNECTION_STRING`
- `IOT_HUB_EVENTHUB_NAME`
- `COSMOSDB_CONNECTION`
- `FUNC_STORAGE_CONN`
- `FUNC_BASE_URL` (placeholder inicialmente)
- `FUNC_KEY` (placeholder inicialmente)
- `FRONTEND_URL` (placeholder inicialmente)

## Verificación recomendada

```bash
ls -l Deploy/infra_outputs.env
cp Deploy/infra_outputs.env ~/infra_outputs.env.backup
```

---

# 17.2 `02_backend.sh`

## Qué hace

Este script:

1. crea el Function App,
2. configura App Settings,
3. publica el código desde `backend/azure_functions/`,
4. obtiene la URL base del backend,
5. obtiene la function key por defecto,
6. actualiza `Deploy/infra_outputs.env`.

## Cómo ejecutarlo

```bash
bash Deploy/02_backend.sh
```

## Requisitos previos

Debe existir:

```bash
Deploy/infra_outputs.env
```

Ese archivo debe haber sido generado por `01_infraestructura.sh`.

## Variables requeridas

Se validan internamente, entre otras:

- `RG_NAME`
- `LOCATION`
- `FUNC_APP_NAME`
- `FUNC_STORAGE`
- `FUNC_STORAGE_CONN`
- `IOTHUB_EVENTS_CONNECTION_STRING`
- `IOTHUB_SERVICE_CONNECTION_STRING`
- `IOT_HUB_EVENTHUB_NAME`
- `IOT_DEVICE_ID`
- `COSMOSDB_CONNECTION`

## Qué actualiza en `infra_outputs.env`

- `FUNC_BASE_URL`
- `FUNC_KEY`

## Verificación recomendada

```bash
grep FUNC_BASE_URL Deploy/infra_outputs.env
grep FUNC_KEY Deploy/infra_outputs.env
```

> Recomendación: evita hacer `cat Deploy/infra_outputs.env` completo si estás compartiendo pantalla.

---

# 17.3 `03_frontend_hosting.sh`

## Qué hace

Este script:

1. habilita Static Website en la storage account del frontend,
2. copia el frontend a un directorio temporal,
3. inyecta `API_BASE_URL` y `API_FUNCTION_KEY` en `app.js`,
4. sube los archivos al contenedor `$web`,
5. actualiza CORS del Function App,
6. guarda `FRONTEND_URL` en `infra_outputs.env`.

## Cómo ejecutarlo

```bash
bash Deploy/03_frontend_hosting.sh
```

## Requisitos previos

Debe existir:

```bash
Deploy/infra_outputs.env
```

Y debe contener, como mínimo:

- `FRONTEND_SA`
- `FUNC_BASE_URL`
- `FUNC_APP_NAME`
- `RG_NAME`

Idealmente también:

- `FUNC_KEY`

## Qué actualiza

Agrega o actualiza:

- `FRONTEND_URL`

## Resultado esperado

Al finalizar deberías tener una URL del dashboard similar a:

```text
https://<storage-account>.z13.web.core.windows.net
```

---

# 17.4 `04_cleanup.sh`

## Qué hace

Elimina el Resource Group completo y todos sus recursos asociados.

## Cómo ejecutarlo

Con confirmación interactiva:

```bash
bash Deploy/04_cleanup.sh
```

Sin confirmación:

```bash
FORCE_CLEANUP=true bash Deploy/04_cleanup.sh
```

## Qué elimina

- IoT Hub
- Cosmos DB
- Function App
- Storage Accounts
- demás recursos dentro del Resource Group

También elimina el archivo local:

```bash
Deploy/infra_outputs.env
```

## Recomendación antes de ejecutarlo

Haz una copia del archivo si necesitas conservar referencia:

```bash
cp Deploy/infra_outputs.env ~/infra_outputs.env.pre_cleanup.backup
chmod 600 ~/infra_outputs.env.pre_cleanup.backup
```

---

# 17.5 `deploy.sh`

## Qué hace

Es el orquestador principal del despliegue.

Ejecuta en orden:

1. infraestructura
2. backend
3. frontend

## Cómo ejecutarlo

Despliegue completo:

```bash
cd Deploy
bash deploy.sh
```

## Flags disponibles

### Solo infraestructura

```bash
bash deploy.sh --only-infra
```

### Solo backend

```bash
bash deploy.sh --only-backend
```

### Solo frontend

```bash
bash deploy.sh --only-front
```

### Omitir infraestructura

```bash
bash deploy.sh --no-infra
```

### Omitir backend

```bash
bash deploy.sh --no-backend
```

### Omitir frontend

```bash
bash deploy.sh --no-front
```

---

# 18. Flujo recomendado completo desde Azure Cloud Shell

## Paso 1: abrir Cloud Shell
Abre Azure Cloud Shell en modo Bash.

## Paso 2: clonar el repositorio

```bash
cd ~
git clone https://github.com/Deivs117/IoT_CNC_Monitoring.git
cd IoT_CNC_Monitoring
```

## Paso 3: ejecutar despliegue completo

```bash
cd ~/IoT_CNC_Monitoring/Deploy
bash deploy.sh
```

El script pedirá las credenciales de Telegram si no existe `~/iot_cnc_secrets.env`,
y ofrecerá guardarlas automáticamente para próximas ejecuciones.

## Paso 4: respaldar outputs

```bash
cp infra_outputs.env ~/infra_outputs.env.backup
chmod 600 ~/infra_outputs.env.backup
```

## Paso 5: si se desconecta la terminal

Cuando vuelvas a abrir Cloud Shell:

```bash
cd ~/IoT_CNC_Monitoring
source ~/iot_cnc_secrets.env
cd Deploy
```

Si necesitas restaurar outputs:

```bash
cp ~/infra_outputs.env.backup ~/IoT_CNC_Monitoring/Deploy/infra_outputs.env
chmod 600 ~/IoT_CNC_Monitoring/Deploy/infra_outputs.env
```

---

# 19. Uso de `infra_outputs.env.template`

El archivo:

```bash
Deploy/infra_outputs.env.template
```

es una plantilla de referencia que documenta las variables relevantes del despliegue.

## Cuándo usarlo

Sirve para:

- entender qué variables maneja el sistema,
- reconstruir manualmente `infra_outputs.env`,
- depurar despliegues,
- revisar el naming esperado de variables.

## Importante
En el flujo normal **no necesitas rellenarlo manualmente**, porque:

- `01_infraestructura.sh` genera automáticamente `infra_outputs.env`.

---

# 20. Persistencia recomendada de secretos y variables

## Archivos que conviene conservar

### Archivo de secretos
```bash
~/iot_cnc_secrets.env
```

### Backup del output de infraestructura
```bash
~/infra_outputs.env.backup
```

## Permisos recomendados

```bash
chmod 600 ~/iot_cnc_secrets.env
chmod 600 ~/infra_outputs.env.backup
```

## Recarga rápida en una nueva sesión

```bash
source ~/iot_cnc_secrets.env
```

## Restauración rápida del archivo de outputs

```bash
cp ~/infra_outputs.env.backup ~/IoT_CNC_Monitoring/Deploy/infra_outputs.env
chmod 600 ~/IoT_CNC_Monitoring/Deploy/infra_outputs.env
```

---

# 21. Buenas prácticas de seguridad

1. No subas nunca a Git:
   - `Deploy/infra_outputs.env`
   - `~/iot_cnc_secrets.env`
   - backups con secretos
2. Evita imprimir secretos en pantalla.
3. Usa `chmod 600` en todos los archivos sensibles.
4. No copies tokens en comandos innecesarios.
5. Si compartes pantalla o haces grabación, evita ejecutar:
   ```bash
   cat Deploy/infra_outputs.env
   ```
6. Usa siempre backups cuando completes aprovisionamiento o backend.

---

# 22. Comandos rápidos útiles

## Recargar secretos
```bash
source ~/iot_cnc_secrets.env
```

## Restaurar outputs
```bash
cp ~/infra_outputs.env.backup ~/IoT_CNC_Monitoring/Deploy/infra_outputs.env
chmod 600 ~/IoT_CNC_Monitoring/Deploy/infra_outputs.env
```

## Ejecutar solo infraestructura
```bash
cd ~/IoT_CNC_Monitoring/Deploy
bash deploy.sh --only-infra
```

## Ejecutar solo backend
```bash
cd ~/IoT_CNC_Monitoring/Deploy
bash deploy.sh --only-backend
```

## Ejecutar solo frontend
```bash
cd ~/IoT_CNC_Monitoring/Deploy
bash deploy.sh --only-front
```

## Eliminar todo el entorno
```bash
cd ~/IoT_CNC_Monitoring/Deploy
bash 04_cleanup.sh
```

---

# 23. Troubleshooting

## Problema: `infra_outputs.env` no existe
### Causa probable
No se ejecutó `01_infraestructura.sh`.

### Solución
```bash
cd ~/IoT_CNC_Monitoring/Deploy
bash 01_infraestructura.sh
```

---

## Problema: `02_backend.sh` dice que faltan variables
### Causa probable
`infra_outputs.env` está incompleto o no fue generado correctamente.

### Solución
Verifica:
```bash
ls -l Deploy/infra_outputs.env
```

Si es necesario, regenera:
```bash
bash Deploy/01_infraestructura.sh
```

---

## Problema: perdiste la sesión de Cloud Shell
### Solución
Vuelve a entrar y recarga:

```bash
cd ~/IoT_CNC_Monitoring
source ~/iot_cnc_secrets.env
cd Deploy
```

Si hace falta restaurar outputs:
```bash
cp ~/infra_outputs.env.backup ~/IoT_CNC_Monitoring/Deploy/infra_outputs.env
chmod 600 ~/IoT_CNC_Monitoring/Deploy/infra_outputs.env
```

---

## Problema: el frontend despliega pero no conecta con backend
### Posibles causas
- `FUNC_BASE_URL` vacío
- `FUNC_KEY` vacío
- CORS incorrecto
- `03_frontend_hosting.sh` ejecutado antes de `02_backend.sh`

### Solución recomendada
Verifica en `infra_outputs.env`:
- `FUNC_BASE_URL`
- `FUNC_KEY`
- `FRONTEND_URL`

Luego vuelve a correr:

```bash
bash Deploy/02_backend.sh
bash Deploy/03_frontend_hosting.sh
```

---

## Problema: Telegram no funciona
### Causa probable
Variables no inyectadas en el Function App:
- `TELEGRAM_BOT_TOKEN`
- `TELEGRAM_CHAT_ID`

### Solución

**Paso 1**: verifica que las variables existen en el Function App:
```bash
source Deploy/infra_outputs.env
az functionapp config appsettings list \
  --name "$FUNC_APP_NAME" \
  --resource-group "$RG_NAME" \
  --query "[?name=='TELEGRAM_BOT_TOKEN' || name=='TELEGRAM_CHAT_ID']" \
  --output table
```

**Paso 2**: si están vacías, corrígelas en tu archivo de secretos y reinjecta:
```bash
# Edita ~/iot_cnc_secrets.env con los valores correctos, luego:
source ~/iot_cnc_secrets.env
az functionapp config appsettings set \
  --name "$FUNC_APP_NAME" \
  --resource-group "$RG_NAME" \
  --settings \
    "TELEGRAM_BOT_TOKEN=${TELEGRAM_BOT_TOKEN}" \
    "TELEGRAM_CHAT_ID=${TELEGRAM_CHAT_ID}"
```

**Paso 3**: si prefieres re-desplegar todo el backend:
```bash
source ~/iot_cnc_secrets.env
bash Deploy/02_backend.sh
```

> Ver sección 9.1 para una guía completa sobre la configuración de Telegram.

---

# 24. Recomendación final

Para trabajar cómodamente desde Azure Cloud Shell, la estrategia más robusta es esta:

- clonar el repositorio en Cloud Shell,
- mantener un archivo persistente de secretos:
  ```bash
  ~/iot_cnc_secrets.env
  ```
- mantener un backup del archivo de outputs:
  ```bash
  ~/infra_outputs.env.backup
  ```
- usar `source` al reconectar,
- usar `deploy.sh` como punto de entrada principal.

Esto permite un flujo práctico, repetible y resistente a desconexiones sin depender de recordar manualmente variables sensibles.

---

# 23. Checklist rápido antes de desplegar

- [ ] Abrí Azure Cloud Shell en modo Bash
- [ ] Cloné el repositorio
- [ ] Creé `~/iot_cnc_secrets.env` (incluye `INSTANCE_SUFFIX`)
- [ ] Protegí el archivo con `chmod 600`
- [ ] Ejecuté `source ~/iot_cnc_secrets.env`
- [ ] Entré a `~/IoT_CNC_Monitoring/Deploy`
- [ ] Ejecuté `bash deploy.sh`
- [ ] Guardé backup de `infra_outputs.env`

---

# 24. Convención de nomenclatura de recursos Azure

Todos los recursos del proyecto siguen una convención legible y predecible:

| Recurso | Nombre | Notas |
|---|---|---|
| Resource Group | `rg-cnc-iot` | Prefijo `rg-` estándar Azure |
| IoT Hub | `cnc-iot-hub` | Fijo y legible |
| Dispositivo | `cnc_fresadora_01` | Underscore (restricción IoT Hub) |
| Cosmos DB Account | `cnc-iot-cosmos` | Fijo y legible |
| Cosmos DB | `CNCMonitor` | Base de datos lógica |
| Contenedor | `Telemetry` | Partición `/device_id` |
| Function App | `cnc-iot-func` | Fijo y legible |
| Storage (Functions) | `cnciotfunc<INSTANCE_SUFFIX>` | Sufijo para unicidad global |
| Storage (Frontend) | `cnciotfront<INSTANCE_SUFFIX>` | Sufijo para unicidad global |

`INSTANCE_SUFFIX` debe ser un alias corto, alfanumérico, minúscula, fácil de recordar
(ej. `deivid`, `prod`, `dev01`). Se define una vez en `~/iot_cnc_secrets.env` y se
mantiene estable entre ejecuciones.

Si no se define `INSTANCE_SUFFIX`, el script `01_infraestructura.sh` genera un sufijo
de 6 caracteres (SHA256 truncado del `RG_NAME`) que es determinista entre ejecuciones
del mismo entorno pero menos legible.

---

# 25. Configuración del firmware tras el despliegue

Tras ejecutar `01_infraestructura.sh`, configura el firmware del ESP32:

## Obtener credenciales del dispositivo

```bash
# FQDN del IoT Hub (copiar en config.h → IOT_HUB_HOST)
echo "cnc-iot-hub.azure-devices.net"

# Clave primaria del dispositivo (copiar en config.h → DEVICE_PRIMARY_KEY)
az iot hub device-identity show \
  --hub-name cnc-iot-hub \
  --device-id cnc_fresadora_01 \
  --query "authentication.symmetricKey.primaryKey" \
  --output tsv
```

## Actualizar config.h

```cpp
constexpr char IOT_HUB_HOST[]       = "cnc-iot-hub.azure-devices.net";
constexpr char DEVICE_ID[]          = "cnc_fresadora_01";
constexpr char DEVICE_PRIMARY_KEY[] = "<clave_primaria_obtenida_arriba>";
constexpr char WIFI_SSID[]          = "TU_SSID";
constexpr char WIFI_PASSWORD[]      = "TU_PASSWORD";
```

## Instalar la librería de Edge Impulse en Arduino IDE

1. Descarga el `.zip` desde Edge Impulse Studio:
   `Deployment → Arduino library → CNC_Monitor_Project_inferencing`
2. En Arduino IDE: `Sketch → Include Library → Add .ZIP Library`
3. Selecciona el `.zip` descargado.
4. Verifica que `CNC_Monitor_Project_inferencing.h` esté disponible.

## Compilar y cargar

El firmware compilará contra el SDK de Edge Impulse y publicará directamente
a Azure IoT Hub (MQTTS, puerto 8883) con SAS token generado en el dispositivo.

---