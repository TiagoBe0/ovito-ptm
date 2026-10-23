# AI Agent

Réplica de la funcionalidad *AI Agent* de OVITO Pro 3.16 en este fork: una pestaña del
inspector de datos que corre un agente de codificación (Claude Code, Copilot CLI, Codex…)
dentro de la ventana de OVITO, con acceso en vivo a la escena que el usuario está mirando.

Código: `src/ovito/aiagent/` (plugin `AIAgentGui`), servidor MCP en `scripts/ovito_agent_mcp.py`.

## En qué se diferencia de OVITO Pro

OVITO Pro monta el puente sobre su intérprete de Python embebido: al pulsar *Iniciar*
arranca un kernel Jupyter interno y el paquete `ovito-mcp` (PyPI) ejecuta código Python
dentro de la sesión viva.

**Este fork no tiene intérprete Python embebido** — no hay `pybind11` ni módulo `ovito` en
`src/`, y portarlo equivaldría a reimplementar la API de Python de OVITO entera. Así que el
puente habla directamente con el modelo de objetos C++:

| | OVITO Pro | Este fork |
|---|---|---|
| Canal | kernel Jupyter + `ovito-mcp` | socket local + JSON-RPC (`AgentBridgeServer`) |
| Qué ejecuta el agente | código Python arbitrario | métodos tipados del puente |
| Terminal embebida | sí | sí (`TerminalWidget`, emulador VT propio) |
| Alcance | todo lo que exponga la API de Python | escena, modificadores, datos, viewports |

La consecuencia práctica: el agente no puede escribir un script arbitrario, pero **sí llega
a todos los modificadores de este fork** (DXA tracking, vacancy, amorphous carbon…) sin
que el puente sepa nada de ellos, porque lee y escribe parámetros por reflexión sobre los
`PropertyFieldDescriptor` de OVITO.

## Puesta en marcha (una sola vez)

1. Instalá el agente de codificación y verificá que corre en una terminal normal
   (`claude`, `copilot`, `codex`…). OVITO no provee ni el agente ni el modelo.

2. Registrá el servidor MCP con el agente. No hace falta instalar nada: el script usa sólo
   la biblioteca estándar de Python 3.

   ```bash
   claude mcp add ovito-agent --scope user -- \
       python3 /home/santi/Documents/ovito-DislocationTraking/research/ai_agent/scripts/ovito_agent_mcp.py
   ```

   Para otros agentes, la misma orden en su archivo de configuración: comando `python3`,
   argumento la ruta absoluta del script.

3. En OVITO, abrí la pestaña **AI Agent** del inspector de datos (abajo), elegí el agente,
   confirmá el directorio de trabajo y pulsá **Start**.

El socket de la sesión viaja al agente en la variable de entorno `OVITO_AGENT_SOCKET`, que
es de donde lo lee el servidor MCP. El prompt inicial se lo recuerda además por si el
agente lo necesita.

### Si algo no funciona

- **«no encuentro el ejecutable»**: el agente no está en el `PATH` que ve OVITO. Elegí
  *Custom…* en la lista y escribí la ruta completa en *Command line* (tiene que contener
  el marcador `<PROMPT>`).
- **el agente arranca pero dice que no tiene herramientas de OVITO**: falta el paso 2.
  `claude mcp list` muestra los servidores registrados.
- **las herramientas fallan con «socket does not exist»**: la sesión de OVITO que las
  publicaba se cerró. Volvé a pulsar *Start*.
- Para depurar el puente sin agente, con una sesión abierta:

  ```bash
  OVITO_AGENT_SOCKET=/tmp/ovito-agent-... python3 scripts/ovito_agent_mcp.py --call scene.list
  ```

## API del puente

Petición y respuesta son objetos JSON separados por saltos de línea sobre un socket Unix:
`{"id":1,"method":"scene.list","params":{}}` →
`{"id":1,"ok":true,"pipelines":[...]}`, o `{"id":1,"ok":false,"error":"..."}`.

| Método | Qué hace |
|---|---|
| `session.info` | versión, PID y archivo de sesión |
| `scene.list` | pipelines de la escena con su pila de modificadores |
| `pipeline.describe` | un pipeline completo, con todos los parámetros |
| `pipeline.select` / `pipeline.delete` | seleccionar / borrar un pipeline |
| `modifier.catalog` | clases de modificador disponibles (filtrables, con defaults) |
| `modifier.insert` / `delete` | insertar (arriba de la pila) / borrar |
| `modifier.set_params` / `set_enabled` | escribir parámetros / (des)activar |
| `data.summary` | evaluar el pipeline y describir su salida |
| `data.attributes` | sólo los atributos globales |
| `data.table` | una tabla de datos (histograma, RDF, serie temporal) |
| `data.elements` | valores por elemento, acotados a 10000 |
| `file.import` | importar un archivo de simulación |
| `animation.info` / `animation.goto` | intervalo de animación / saltar de frame |
| `viewport.list` / `viewport.camera` | leer / mover cámaras |
| `viewport.screenshot` | capturar lo que el usuario ve, como PNG |

Convenciones: el índice 0 de modificador es el **de arriba** (el último aplicado);
`pipeline` acepta índice o nombre y, si se omite, usa el seleccionado en la GUI; `frame`
por defecto es el que está en pantalla.

Todo se ejecuta en el hilo de la GUI y cada cambio de escena se registra como operación
deshacible, así que el usuario puede revertir lo que haga el agente con Ctrl+Z.

## Consideraciones de seguridad

Conectar un agente le da la capacidad de modificar la escena y de leer los datos cargados,
con los privilegios de la sesión de OVITO. El socket es privado del usuario
(`QLocalServer::UserAccessOption`), pero el puente **no añade restricciones** sobre lo que
el agente ya puede hacer en la máquina: el agente además corre con acceso normal al sistema
de archivos y a la red. Si el modelo es un LLM en la nube, el código, los mensajes de error
y **las capturas de pantalla** viajan a ese proveedor.

## Estado y pendientes

Implementado y compilando; falta la prueba de campo con una sesión real.

- El emulador de terminal cubre lo que usan las TUI de los agentes (pantalla alternativa,
  regiones de scroll, SGR con color de 24 bits, pegado entre corchetes, informes de
  posición de cursor). **No** reporta eventos de ratón a la aplicación, así que los menús
  que se navegan con el mouse dentro del agente no responden; el teclado sí.
- `modifier.insert` sólo inserta arriba de la pila, como el botón de la GUI.
- No hay `render` a archivo con el motor de alta calidad: las capturas son del viewport
  en pantalla.
