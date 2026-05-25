# Frontend — CNC PCB Monitor Dashboard

Dashboard web estático para monitoreo en tiempo real de la fresadora CNC. Muestra métricas de temperatura, humedad, estado vibracional y anomaly scores; permite controlar el actuador y exportar datos en CSV.

---

## Estructura de archivos

```
frontend/
├── index.html   # Estructura HTML del dashboard (header, métricas, actuador, tabla)
├── app.js       # Lógica de UI: polling, renderizado, control de actuador, descarga CSV
└── style.css    # Tema oscuro con variables CSS, grid de métricas, estilos responsivos
```

No hay framework ni bundler. Es HTML/CSS/JS puro — compatible con cualquier servidor estático.

---

## Apertura local (sin servidor)

```bash
# Abrir directamente en el navegador
open frontend/index.html       # macOS
start frontend/index.html      # Windows
xdg-open frontend/index.html   # Linux
```

> **Nota:** Abrir `index.html` directamente como `file://` funciona para ver el layout, pero las llamadas `fetch()` al backend fallarán por restricciones CORS del navegador. Para desarrollo real, usar Live Server (ver abajo).

---

## Desarrollo local con Live Server

Con la extensión [Live Server](https://marketplace.visualstudio.com/items?itemName=ritwickdey.LiveServer) de VS Code:

1. Abre la carpeta raíz del repositorio en VS Code.
2. Clic derecho sobre `frontend/index.html` → **Open with Live Server**.
3. El navegador abre `http://127.0.0.1:5500/frontend/index.html`.

---

## Configuración de la conexión al backend

`app.js` usa dos constantes que deben apuntar al Function App desplegado:

```js
const API_BASE_URL     = "__API_BASE_URL__";      // URL base del Function App
const API_FUNCTION_KEY = "__API_FUNCTION_KEY__";  // Clave de función (authLevel: function)
```

Los valores `__API_BASE_URL__` y `__API_FUNCTION_KEY__` son **placeholders**. En producción son reemplazados automáticamente por el script `Deploy/03_frontend_hosting.sh` antes de subir los archivos al hosting.

### Para desarrollo local

Reemplaza los placeholders manualmente con los valores reales (sin commitear):

```js
const API_BASE_URL     = "https://<tu-func-app>.azurewebsites.net/api";
const API_FUNCTION_KEY = "<tu-function-host-key>";
```

Obtener los valores:
- **`API_BASE_URL`**: Portal Azure → Function App → Información general → URL, agregar `/api` al final.
- **`API_FUNCTION_KEY`**: Portal Azure → Function App → Claves de aplicación → Claves de host → `default`.

### Llamadas HTTP que realiza el frontend

| Acción | Método | Ruta | Parámetros |
|---|---|---|---|
| Cargar telemetría | GET | `/api/datos` | `?code=<key>&limit=<n>&device_id=<id>` |
| Descargar CSV | GET | `/api/datos/csv` | `?code=<key>&device_id=<id>` |
| Enviar comando | POST | `/api/actuador` | `?code=<key>` · body: `{"comando":"ON\|OFF\|RESET"}` |

La clave `code` se añade automáticamente por la función `buildUrl()` en `app.js` cuando `API_FUNCTION_KEY` está definida.

---

## Despliegue en Azure Static Website (recomendado con scripts)

El script `Deploy/03_frontend_hosting.sh` automatiza todos los pasos:

1. Habilita Static Website en el Storage Account.
2. Inyecta `API_BASE_URL` y `API_FUNCTION_KEY` en una copia temporal de `app.js`.
3. Sube los archivos al contenedor `$web`.
4. Actualiza las reglas CORS del Function App con la URL del frontend.

```bash
cd Deploy/
./deploy.sh --only-front   # Requiere infra_outputs.env con FUNC_BASE_URL y FUNC_KEY
```

La URL del dashboard queda guardada en `Deploy/infra_outputs.env` como `FRONTEND_URL`.

---

## Despliegue en GitHub Pages

GitHub Pages sirve el contenido de la rama `gh-pages` o de la carpeta `docs/`.

### Pasos

1. **Inyectar variables antes de publicar** (GitHub Pages no ejecuta scripts de build):

   ```bash
   # Crear copia del frontend con variables reales
   cp -r frontend/ /tmp/frontend-deploy/
   sed -i "s|__API_BASE_URL__|https://<func-app>.azurewebsites.net/api|g" /tmp/frontend-deploy/app.js
   sed -i "s|__API_FUNCTION_KEY__|<host-key>|g" /tmp/frontend-deploy/app.js
   ```

2. Copiar los archivos procesados a la rama `gh-pages` o carpeta `docs/`.

3. En la configuración del repositorio → Pages → Source: seleccionar la rama/carpeta.

4. Agregar la URL de GitHub Pages a las reglas CORS del Function App:
   ```bash
   az functionapp cors add \
     --name <FUNC_APP_NAME> \
     --resource-group <RG_NAME> \
     --allowed-origins "https://<usuario>.github.io"
   ```

> **Consideración de seguridad:** `API_FUNCTION_KEY` quedará visible en el código fuente del navegador. Esto es aceptable para proyectos internos/académicos. Para producción, implementar un proxy o usar Azure API Management.

---

## Despliegue en Vercel

Vercel puede servir el frontend directamente desde la carpeta `frontend/` del repositorio.

### Pasos

1. Conectar el repositorio en [vercel.com](https://vercel.com).
2. Configurar el proyecto:
   - **Framework Preset:** Other (sin framework)
   - **Root Directory:** `frontend`
   - **Build Command:** *(dejar vacío)*
   - **Output Directory:** `.` (el directorio actual)
3. Añadir las variables de entorno en el dashboard de Vercel:
   - `API_BASE_URL` → `https://<func-app>.azurewebsites.net/api`
   - `API_FUNCTION_KEY` → `<host-key>`
4. Agregar un archivo `vercel.json` en `frontend/` para la inyección de variables:

   ```json
   {
     "build": {
       "env": {
         "API_BASE_URL": "@api_base_url",
         "API_FUNCTION_KEY": "@api_function_key"
       }
     }
   }
   ```

   > **Alternativa más simple:** Usar un script de build en `package.json` que haga el `sed` de placeholders antes del despliegue.

5. Agregar la URL de Vercel a las reglas CORS del Function App.

---

## Problemas comunes

| Síntoma | Causa probable | Solución |
|---|---|---|
| Dashboard muestra "Error de conexión" | `API_BASE_URL` o `API_FUNCTION_KEY` incorrectos | Verificar valores en `app.js` o variables de entorno del hosting |
| Respuesta HTTP 401 / 403 | Clave de función incorrecta o expirada | Regenerar host key en Portal Azure → Function App |
| Respuesta HTTP 0 / CORS error | URL del frontend no está en las reglas CORS del Function App | Agregar la URL exacta (con protocolo, sin slash final) a las reglas CORS |
| Tabla vacía pero sin error | `device_id` del filtro no coincide con ningún registro | Limpiar el campo "Dispositivo" o verificar el ID en Cosmos DB |
| Timestamp muestra fechas incorrectas | El `timestamp` en Cosmos DB no es un Unix epoch | El firmware debe publicar `timestamp` como entero Unix (segundos desde 1970) |
| Descarga CSV vacía | Sin datos en Cosmos DB o filtro de dispositivo demasiado estricto | Verificar en el Portal que el contenedor `Telemetry` tiene documentos |
