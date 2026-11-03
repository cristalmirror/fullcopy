# Pruebas pendientes de FullCopy

Actualizado: 2026-11-03.

Este documento contiene únicamente escenarios pendientes de validación sobre el código actual. Cada caso requiere ejecución y evidencia; los criterios de aceptación describen el resultado esperado, no un resultado ya obtenido. Algunos casos requieren inyección de fallos y pueden revelar defectos que aún necesitan corrección.

## Manejo de errores y cierre

| Prueba pendiente | Procedimiento | Criterio de aceptación |
|---|---|---|
| Error de `GetMessageW` | Forzar el retorno `-1` en el bucle principal y en el diálogo de renombrado, tanto al inicio como después de procesar mensajes. | Se conserva un diagnóstico, se distingue el error del cierre normal y no se devuelve un código procedente de un mensaje anterior. |
| Fallo al crear ventanas y controles | Forzar fallos de `CreateWindowExW` en la ventana principal, el diálogo de renombrado y sus controles esenciales. | La creación falla de forma controlada, libera los recursos parciales y no presenta una interfaz incompleta como válida. Un fallo del diálogo no se cuenta como omisión voluntaria. |
| Fallo al registrar la clase del diálogo | Forzar un error de `RegisterClassExW` distinto de `ERROR_CLASS_ALREADY_EXISTS`; comprobar también este último caso como control. | Solo se tolera la clase ya registrada; los demás errores se propagan con diagnóstico. |
| Fallos del selector de carpetas | Forzar errores de `GetOptions` y `SetOptions`. | Se informa el fallo y no se abre el selector con una configuración distinta de la requerida. |
| Fallo al crear el trabajador | Forzar un fallo de `CreateThread` después de actualizar el estado visual de inicio. | Los controles quedan habilitados y el estado deja de indicar que se está leyendo el origen; se informa el error. |
| Inicialización de la aplicación | Forzar fallos de `InitCommonControlsEx` y `CoInitializeEx`. | El inicio falla de forma controlada o deshabilita explícitamente la funcionalidad no disponible; no se ofrece un selector COM que no puede funcionar. |
| Cierre excepcional con trabajador activo | Provocar la salida excepcional del bucle principal mientras el trabajador espera una respuesta de la interfaz mediante `SendMessageW`, tanto para renombrado como para recursión. | No hay espera circular; el trabajador termina, se liberan sus recursos y el proceso sale. Registrar el tiempo de cierre. |
| Excepciones del trabajador | Inyectar una excepción estándar y otra no estándar durante el trabajo. | La copia termina como fallo y conserva información útil de la causa, sin mostrar éxito ni dejar recursos retenidos. |
| Excepciones en callbacks | Inyectar excepciones, incluido un fallo de asignación de memoria, en handlers invocados por `dispatchWindowMessage`. | Ninguna excepción cruza la frontera de `WndProc`; se registra el error y se realiza una salida controlada. |
| Destrucción de ventanas en rutas excepcionales | Interrumpir la creación o ejecución después de crear el `HWND`, antes del cierre normal. | No quedan ventanas asociadas a objetos destruidos ni recursos de interfaz sin liberar. |

## Archivos, referencias e integridad

| Prueba pendiente | Procedimiento | Criterio de aceptación |
|---|---|---|
| Junction hacia otro volumen | Usar un segundo volumen y copiar un origen que contenga una junction hacia él. | La referencia se informa como inaccesible y no se crea en el destino. |
| Enlaces simbólicos | Probar enlaces a archivos y directorios del mismo volumen, de otro volumen, rotos y con ciclos. | Se cumple la política documentada: copia real del contenido admitido, rechazo informado de referencias no admitidas y detección de ciclos. |
| OneDrive | Copiar archivos y carpetas locales y disponibles solo en la nube; repetir sin conexión o con OneDrive detenido. | Los elementos disponibles conservan su contenido; los no disponibles se informan como inaccesibles, sin falso éxito. |
| Orígenes de red | Copiar desde un recurso compartido, incluyendo referencias y una interrupción de conexión. | No se siguen referencias del origen de red; los errores se informan sin bloqueo ni falso éxito. |
| Errores de acceso y espacio | Usar destino de solo lectura, archivo de origen bloqueado, permisos denegados y un volumen desechable sin espacio. No llenar el volumen del sistema. | Los fallos quedan reflejados en el resultado, sin cierre inesperado ni resumen de éxito falso. |
| Integridad bajo carga | Copiar al menos 10.000 archivos y un archivo de varios GB; comparar rutas relativas y SHA-256. Repetir 20 veces. | Coinciden todos los archivos esperados, no aparecen elementos omitidos y la aplicación sigue siendo utilizable. |
| Profundidad y rutas largas bajo carga | Usar rutas de origen de más de 260 caracteres y aumentar progresivamente la profundidad del árbol más allá del caso automatizado. | No hay desbordamiento de pila ni bloqueo; se documenta la profundidad validada y se conserva el comportamiento de renombrado y omisión. |
| Rendimiento de renombrado y omisión | Medir tiempo y entradas visitadas al renombrar u omitir un subárbol pequeño seguido de muchos elementos ajenos. Comparar versiones con el mismo árbol y entorno. | Se cuantifica el coste del recorrido y cualquier mejora o regresión; no se deduce rendimiento a partir de resultados funcionales. |

## Interfaz, cancelación y recursos

| Prueba pendiente | Procedimiento | Criterio de aceptación |
|---|---|---|
| Liberación después de Browse | Crear y seleccionar un destino mediante Browse, copiar, cerrar y eliminarlo. Repetir cancelando Browse y sin iniciar una copia. | El proceso sale y la carpeta puede eliminarse inmediatamente. Si no, identificar el proceso que la retiene. |
| Cancelación durante trabajo activo | Cerrar durante un escaneo ya iniciado y durante la copia de un archivo grande. | Se procesa la cancelación, se elimina el archivo parcial cuando corresponda y no hay bloqueo ni cierre inesperado. Registrar la demora y evaluar si es aceptable. |
| Interacción real con teclado y ratón | Usar Enter, Escape, Tab, X y Alt+F4; repetir renombrados y omisiones, incluido el cierre con un diálogo abierto. | Foco, controles, subárboles y resumen son correctos; no quedan diálogos huérfanos. |
| Avisos de recursión e inaccesibilidad | Ejecutar la aplicación con ciclos y referencias inaccesibles; aceptar los avisos y probar el cierre durante ellos. | Se muestran las rutas correctas, el escaneo continúa o se cancela según la acción y el resumen refleja los casos detectados. |
| Escalado DPI | Revisar la ventana principal y los diálogos al 100 %, 125 %, 150 % y 200 %; moverlos entre monitores con escalas distintas. | No hay controles superpuestos, texto recortado ni acciones inaccesibles. |
| Memoria, handles y objetos GDI | Tras el calentamiento, medir bytes privados, handles y objetos GDI durante 100 ciclos de Browse, copia y renombrado. | No existe crecimiento sostenido ciclo a ciclo; cualquier crecimiento se investiga con registros. |
| Corrupción de memoria | Ejecutar las pruebas de interfaz y carga con Windows Application Verifier y full page heap cuando estén disponibles. | No aparecen accesos inválidos, corrupción del heap ni errores de uso de recursos; conservar los diagnósticos. |
| Compatibilidad de Windows | Definir las versiones de Windows admitidas y ejecutar en cada una los escenarios funcionales y de interfaz relevantes. | El comportamiento es consistente en la matriz declarada; registrar versión de Windows y sistema de archivos. |

## Registro de cada prueba pendiente

Al ejecutar un caso, registrar la revisión y los cambios locales evaluados, versión de Windows, sistema de archivos, preparación y semilla de datos cuando corresponda, pasos, resultado real, tiempos o mediciones relevantes y logs. Una prueba no ejecutada permanece pendiente; una prueba fallida debe conservar su evidencia y repetirse después de corregir la causa.
