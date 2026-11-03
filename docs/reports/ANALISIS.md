# Análisis del informe de Codex sobre FullCopy

Fecha: 2026-11-02
Proyecto: `/home/crisal/fullcopy` (rama `feat/refactor`, versión 1.0.2)

---

## 1. Pregunta

> Este es un informe de Codex sobre este proyecto, ¿podrías explicarme en cada error, con la línea de código del proyecto, en qué consiste el problema?

---

## 2. Informe original de Codex

Sí: encontré problemas de comportamiento y varias señales de código poco cuidado. Eso permite evaluar su calidad, pero no atribuir con certeza qué partes escribió una IA.

El reporte se basa en revisión estática de las implementaciones, cabeceras y pruebas relacionadas. No modifiqué archivos ni ejecuté la aplicación: este entorno no tiene MinGW ni Wine disponibles.

Uso estas distinciones: bug para comportamiento incorrecto identificable en el código; riesgo para fallos sujetos a condiciones excepcionales; limpieza para mejoras sin efecto funcional; dudoso cuando depende de la intención del proyecto.

### src/Copier.cpp

1. **Directorios con reparse points terminan como carpetas vacías**
   - Líneas: 469–472, junto con 262–268.
   - Categoría: copia incorrecta / Win32.
   - Severidad: alta — bug.
   - Problema: se incorpora el directorio a entries_, pero directoryEntry && !reparse impide recorrerlo. Después, copy() crea una carpeta normal en el destino y devuelve éxito. Para una junction o un enlace simbólico a directorio, no se conserva el enlace ni se copia su contenido, y tampoco se informa que fue omitido.
   - Corrección: definir una política explícita. La opción más sencilla es informar esos elementos como no compatibles u omitidos y contarlos en el resumen. Seguir enlaces requiere además controlar ciclos.
2. **Recorridos innecesarios hasta el final del árbol**
   - Líneas: 330–337 y 348–354.
   - Categoría: lógica repetida / eficiencia.
   - Severidad: baja — limpieza.
   - Problema: al renombrar u omitir un directorio, ambos bucles siguen revisando todas las entradas posteriores. Como el escaneo almacena el árbol en preorden, sus descendientes son contiguos.
   - Corrección: detener el recorrido al encontrar la primera entrada fuera del subárbol. No hace falta crear una abstracción genérica para conseguir esa mejora.
3. **Comprobación de directorio reimplementada**
   - Líneas: 15–20 y 204–208.
   - Categoría: duplicación.
   - Severidad: baja — limpieza.
   - Problema: load() repite la consulta de atributos que ya encapsula isDirectory().
   - Corrección: llamar a isDirectory(getFullSourcePath(L"")). Conservaría la comprobación; lo redundante es su implementación, no necesariamente volver a consultar el sistema de archivos.
4. **API pública y estado sin consumidores**
   - Líneas: 407–414; declaraciones en include/Copier.hpp:86–87.
   - Categoría: código muerto / estado redundante.
   - Severidad: baja — limpieza.
   - Problema: no encontré llamadas a getDestinationRoot() ni a isSizeApproved() en el proyecto. sizeApproved_ solo almacena un resultado que check() ya devuelve.
   - Corrección: eliminar esos getters si no forman parte de una API prevista y devolver directamente el resultado de check(). tempSize_ sí tiene un consumidor en el controlador.
5. **Indentación que oculta la estructura real**
   - Líneas: 118–125, 488 y 523–525.
   - Categoría: estilo inconsistente.
   - Severidad: baja — estética.
   - Problema: el cuerpo del destructor de FindHandle está mal alineado; también hay declaraciones fuera de su nivel de indentación y espacios vacíos sobrantes.
   - Corrección: uniformar el formato. El destructor funciona, pero su presentación dificulta comprobar las llaves.

### src/Window.cpp

6. **Error de GetMessageW confundido con salida normal**
   - Líneas: 85–93.
   - Categoría: manejo de errores Win32.
   - Severidad: media — bug en la ruta de error.
   - Problema: GetMessageW puede devolver -1. El bucle termina y devuelve message.wParam como si hubiera recibido WM_QUIT. Ese campo puede contener un mensaje anterior o no estar inicializado si falla la primera llamada.
   - Corrección: distinguir explícitamente los resultados positivo, cero y -1; devolver un código de error definido y conservar el diagnóstico.
7. **Creación de controles sin comprobar fallos**
   - Líneas: 189–311.
   - Categoría: manejo de errores / interfaz.
   - Severidad: media — riesgo funcional.
   - Problema: createControls() no verifica los HWND devueltos. Window::create() puede devolver éxito aunque falten controles esenciales.
   - Corrección: hacer que createControls() informe éxito o fallo y abortar la creación de la ventana si falta un control indispensable.
8. **Configuración del selector de carpetas sin comprobar**
   - Líneas: 323–328.
   - Categoría: manejo de errores COM.
   - Severidad: media — riesgo funcional.
   - Problema: se ignoran los resultados de GetOptions() y SetOptions(). Si falla la configuración, el diálogo puede abrirse sin las restricciones que el código supone.
   - Corrección: comprobar ambos HRESULT y mostrar un error si no puede configurarse la selección de carpetas.
9. **Estado visual incorrecto si no se crea el hilo**
   - Líneas: 405–416.
   - Categoría: estado de interfaz.
   - Severidad: baja — bug.
   - Problema: después de showCopyStarted(), un fallo de CreateThread llama a showStartFailed(). Los controles vuelven a habilitarse, pero queda el texto "Reading the source directory..." aunque no hay ninguna operación ejecutándose.
   - Corrección: establecer un estado de fallo o restaurar el mensaje inicial.
10. **Asignación dinámica sin necesidad visible**
    - Líneas: 369–374; miembro decision_ en include/Window.hpp:83.
    - Categoría: sobreingeniería.
    - Severidad: baja — limpieza; dudoso.
    - Problema: decision_ se crea, se usa mediante una llamada bloqueante a show() y se destruye inmediatamente.
    - Corrección: considerar un Decision local en la pila. Si se conserva la asignación dinámica, usar std::make_unique.
11. **Porcentaje almacenado sin necesidad**
    - Líneas: 97–112; include/Window.hpp:72.
    - Categoría: estado redundante.
    - Severidad: baja — limpieza.
    - Problema: loadPercentage_ solo se escribe y consulta dentro de updateProgressBar().
    - Corrección: usar el parámetro ya limitado a 0–100 y eliminar el miembro.

### src/Decision.cpp

12. **Un fallo al abrir el diálogo se interpreta como decisión de omitir**
    - Líneas: 60–64; consumidor en Controller.cpp:219–222.
    - Severidad: media — bug.
    - Problema: show() devuelve false tanto si el usuario cancela como si falla CreateWindowExW.
    - Corrección: distinguir "renombrado", "cancelado" y "error al mostrar el diálogo".
13. **Retorno de registro ignorado con un comentario demasiado permisivo**
    - Líneas: 123–133.
    - Severidad: baja — riesgo.
    - Problema: se ignoran todos los errores de RegisterClassExW, no solamente ERROR_CLASS_ALREADY_EXISTS.
    - Corrección: aceptar específicamente ERROR_CLASS_ALREADY_EXISTS y propagar cualquier otro fallo.
14. **Controles sin validar y miembro sin uso**
    - Líneas: 167–220; include/Decision.hpp:54.
    - Severidad: media / baja.
    - Problema: no se comprueba la creación del edit ni del botón. acceptButton_ se almacena, pero nunca se consulta.
    - Corrección: verificar la creación de los controles y conservar únicamente los handles necesarios.
15. **Duplicación y formato muy distinto al resto**
    - Líneas: 178–218, 240–253.
    - Severidad: baja — limpieza.
    - Problema: repite los parámetros comunes de CreateWindowExW que Window ya concentra en un ayudante. Indentación inconsistente.
    - Corrección: compartir un ayudante pequeño para controles y aplicar el mismo formato.

### src/Controller.cpp

16. **Espera bloqueante peligrosa durante la destrucción**
    - Líneas: 33–36, 169–174 y llamada a SendMessageW en 219.
    - Severidad: alta — riesgo condicionado.
    - Problema: el destructor espera indefinidamente al trabajador sin procesar mensajes. Si el trabajador espera que la interfaz atienda SendMessageW, ambos hilos quedan esperando entre sí.
    - Corrección: mantener el procesamiento de mensajes hasta terminar el trabajador, o sustituir la petición síncrona por un protocolo cancelable.
17. **Excepciones convertidas en un fallo sin diagnóstico**
    - Líneas: 178–185.
    - Severidad: media.
    - Problema: catch (...) descarta toda información sobre la causa.
    - Corrección: capturar primero std::exception, guardar diagnóstico en WorkerResult y mantener una captura final de respaldo.
18. **Indirecciones muy finas**
    - Líneas: 40–47, 101–104 y 153–155.
    - Severidad: baja — dudoso.
    - Problema: create(), run(), onProgress() y rename() solo delegan una llamada.
    - Corrección: conservarlas si se quiere mantener el límite MVC.

### include/Common.hpp

19. **La frontera de callbacks no contiene excepciones**
    - Líneas: 48–72.
    - Severidad: media — riesgo.
    - Problema: el dispatcher llama a handlers que pueden lanzar excepciones sin impedir que salgan de la WndProc.
    - Corrección: definir una frontera de captura con diagnóstico y una salida controlada.

### src/main.cpp

20. **Inicializaciones importantes sin tratamiento de fallo**
    - Líneas: 11–17.
    - Severidad: media — riesgo funcional.
    - Problema: se ignora InitCommonControlsEx; si CoInitializeEx falla, la app sigue ofreciendo el selector de carpetas.
    - Corrección: comprobar ambas y decidir explícitamente qué hacer ante fallo.
21. **Comentario que promete una destrucción no garantizada**
    - Línea: 19.
    - Severidad: baja.
    - Problema: dice que destruir Controller destruye su ventana, pero Window no tiene destructor que libere el HWND.
    - Corrección: describir la condición real o implementar la garantía.

### Interfaz y manifiesto

22. **Diseño en píxeles fijos y sin estrategia DPI**
    - Ubicación: Window.cpp:54–58, 215–308; Decision.cpp:44–57, 178–218; resources/fullcopy.manifest.
    - Severidad: media — limitación de interfaz.
    - Problema: dimensiones fijas en píxeles y sin declaración DPI.
    - Corrección: medidas escalables primero; declarar DPI awareness después.

### Aspectos que Codex no considera defectos

- No hay mezcla problemática de ANSI y Unicode ni APIs "alucinadas".
- Las WndProc tienen fallback a DefWindowProcW; el dispatcher limpia la asociación en WM_NCDESTROY.
- No hay fuga GDI evidente.
- Los wrappers DirectoryHandle y FindHandle aportan RAII; no son sobreingeniería.
- Revalidar directorios al iniciar el trabajo tiene sentido.
- El temporizador de respaldo tiene una finalidad concreta y una prueba.
- Los casts entre handles y enteros exigidos por Win32 no son peligrosos por sí mismos.

### Los cinco problemas que Codex corregiría primero

1. Directorios enlazados convertidos silenciosamente en carpetas vacías.
2. Posible bloqueo en la salida excepcional (SendMessageW + espera del destructor).
3. Fallos del diálogo de renombrado contabilizados como omisiones.
4. Creación e inicialización de la interfaz sin comprobar.
5. Manejo de errores de GetMessageW y diagnóstico de excepciones.

---

## 3. Explicación punto por punto con el código real

Revisé el código de los 22 puntos y el informe es correcto en todos. Tres puntos merecen más atención de la que les da: el #1 es más grave de lo que parece, y el #6 y el #16 se combinan en un mismo fallo.

### src/Copier.cpp

#### 1. Las carpetas que son enlaces se copian vacías (alta, bug)
```cpp
// Copier.cpp:470-472
const bool directoryEntry = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
const bool reparse = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
children.push_back({child, directoryEntry, directoryEntry && !reparse, true});
//                                         ^ descend = false si es enlace
```
```cpp
// Copier.cpp:262-268
if (entry.isDirectory) {
    if (CreateDirectoryW(destination.c_str(), nullptr)) {
        return true;   // crea una carpeta normal y vacía
    }
```
**Qué pasa:** una *junction* o un enlace simbólico a carpeta se añade a la lista, pero el escaneo no entra en él porque `descend` vale `false`. Después, `copy()` crea en el destino una carpeta vacía y devuelve éxito. El usuario ve "terminado" sin ningún aviso, y el contenido no se copió.
**Algo más:** algunas carpetas de OneDrive o de otros servicios en la nube también llevan el atributo `REPARSE_POINT`. Si es tu caso, el fallo afectaría a carpetas normales del usuario, no solo a enlaces creados a mano.

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

La revisión del código de 1.0.2 confirma que no se recorren los directorios con REPARSE_POINT y que la copia crea un directorio normal. Las suites existentes no prueban junctions: este comportamiento no se reprodujo mediante un test. docs/testing.md lo documenta como limitación. No se probó OneDrive.

#### 2. Bucles que recorren hasta el final sin necesidad (baja, limpieza)
```cpp
// Copier.cpp:332-337 (rename) y 350-354 (skip)
for (std::size_t i = index + 1; i < entries_.size(); ++i) {
    if (startsWith(path, oldPrefix)) { ... }
}
```
**Qué pasa:** la lista está en preorden, así que el contenido de una carpeta aparece justo después de ella y sin huecos. Cuando aparece la primera entrada que no empieza por el prefijo, ya no quedan más hijos. El bucle podría terminar con un `else break;`, pero sigue revisando el resto de la lista. El resultado es correcto; solo es lento en árboles grandes.

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

Observación estática: los bucles siguen recorriendo las entradas posteriores. Los tests de renombrado y omisión pasaron, pero no miden el trabajo innecesario ni su coste; no se demostró un fallo funcional o de rendimiento.

#### 3. Comprobación de directorio repetida (baja, limpieza)
```cpp
// Copier.cpp:16-20: ya existe el ayudante
bool isDirectory(const std::wstring &path) { ... }

// Copier.cpp:205-208: en load() se vuelve a escribir lo mismo a mano
const DWORD attributes = GetFileAttributesW(getFullSourcePath(L"").c_str());
if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
```
**Qué pasa:** es el mismo código copiado. Además, en la línea 217 sí se usa `isDirectory()`, así que el estilo es inconsistente dentro de la misma función. Bastaría con `if (!isDirectory(getFullSourcePath(L""))) return false;`.

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

Observación estática: permanece la comprobación duplicada. Los tests de validación de directorios pasaron. La duplicación no constituye un fallo funcional demostrado por esas pruebas.

#### 4. Getters y estado que nadie usa (baja, código muerto)
```cpp
// Copier.cpp:407-414
const std::wstring &Copier::getDestinationRoot() const { return destinationRoot_; }
bool Copier::isSizeApproved() const { return sizeApproved_; }
```
**Qué pasa:** comprobado con grep: ninguna de las dos funciones se llama en `src/` ni en `tests/`. `sizeApproved_` solo guarda lo que `check()` ya devuelve (líneas 237-244). Se puede borrar el miembro y escribir `return tempSize_ <= limit && ...;`.

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

La búsqueda de referencias mantiene la observación sobre los getters y el estado redundante. Es una conclusión estática; las pruebas no demuestran que una API pública pueda eliminarse sin afectar consumidores externos.

#### 5. Indentación que confunde (baja, estética)
```cpp
// Copier.cpp:118-125
            ~FindHandle() {
            if (handle_ != INVALID_HANDLE_VALUE) {   // ← debería ir más a la derecha
                FindClose(handle_);
            }
        }                                            // ← cierra el destructor con sangría de la clase

        FindHandle(const FindHandle&) = delete;      // ← parece fuera de la clase
```
También `std::wstring root;` en la línea 488 tiene menos sangría que el resto del cuerpo, y las líneas 523-524 son espacios en blanco sobrantes. El código compila bien, pero cuesta ver dónde termina cada bloque.

### src/Window.cpp

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

La inconsistencia de indentación se observa en el código. La compilación de las suites terminó sin advertencias con -Wall -Wextra -Werror; no es un defecto funcional demostrado por tests.

#### 6. Un error de `GetMessageW` se trata como un cierre normal (media, bug)
```cpp
// Window.cpp:87-93
while (GetMessageW(&message, nullptr, 0, 0) > 0) { ... }
return static_cast<int>(message.wParam);
```
**Qué pasa:** `GetMessageW` devuelve tres tipos de valor: `>0` para un mensaje normal, `0` para `WM_QUIT` y `-1` para un error. Con `> 0`, el bucle termina igual con `0` que con `-1`, y luego devuelve `message.wParam` como si fuera el código de salida. Si hubo un error, ese `wParam` pertenece al último mensaje procesado, o no tiene ningún valor si falló la primera llamada (`MSG message;` no se inicializa).
`Decision.cpp:78-87` sí distingue los tres casos. La ventana principal no lo hace.

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

La ruta señalada sigue presente en Window::run(). Las pruebas de interfaz pasaron, pero ninguna fuerza GetMessageW a devolver -1. El error descrito no se reprodujo por ejecución.

#### 7. No se comprueba si los controles se crearon (media, riesgo)
```cpp
// Window.cpp:77: create() ignora cualquier fallo
createControls();
ShowWindow(window_, showCommand);
return true;

// Window.cpp:216: ningún resultado se comprueba
sourceDirectory_ = createControl(WS_EX_CLIENTEDGE, L"EDIT", ...);
```
**Qué pasa:** `createControls()` devuelve `void`. Si `CreateWindowExW` falla (por ejemplo, porque no hay memoria o falta la clase `PROGRESS_CLASSW`), el handle queda en `nullptr`, la ventana se muestra igual y `create()` devuelve `true`. El programa no se cae, porque las llamadas Win32 con `nullptr` simplemente fallan, pero el usuario ve una ventana incompleta y sin explicación.

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

La creación de controles sigue sin validar los resultados señalados. Las pruebas de interfaz pasaron con creación normal; no simulan fallos de CreateWindowExW. Riesgo identificado por lectura, no reproducido.

#### 8. El selector de carpetas se configura sin comprobar errores (media, riesgo)
```cpp
// Window.cpp:324-326
DWORD options = 0;
dialog->GetOptions(&options);            // HRESULT ignorado
dialog->SetOptions(options | FOS_PICKFOLDERS | ...);  // HRESULT ignorado
```
**Qué pasa:** si `SetOptions` falla, el diálogo se abre como un selector de **archivos** normal, porque falta `FOS_PICKFOLDERS`. El usuario podría elegir un archivo y su ruta acabaría en la caja de "directorio". Hay que comprobar los dos con `FAILED(...)` y mostrar un error.

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

Los resultados de GetOptions y SetOptions siguen sin comprobarse. Las suites no prueban el selector de carpetas ni inyectan errores COM. La consecuencia descrita no quedó comprobada mediante tests.

#### 9. El texto de estado queda mal si falla `CreateThread` (baja, bug)
```cpp
// Controller.cpp:77-84
window_.showCopyStarted();          // pone "Reading the source directory..."
workerThread_ = CreateThread(...);
if (workerThread_ == nullptr) {
    window_.showStartFailed();      // Window.cpp:414-417: rehabilita controles, NO cambia el texto
}
```
**Qué pasa:** después del error, la barra de estado sigue diciendo "Reading the source directory...", aunque no hay nada en marcha. `showStartFailed()` debería llamar también a `setStatus(...)`. `showCopyFailed()` en la línea 447 ya lo hace.

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

El código mantiene showCopyStarted() antes de CreateThread y showStartFailed() no actualiza el texto de estado. Las pruebas de inicio rechazado pasaron, pero no provocan un fallo de CreateThread; no verifican esta ruta.

#### 10. `decision_` en el montón sin motivo (baja, dudoso)
```cpp
// Window.cpp:370-372
decision_.reset(new Decision(instance_, controller_, request));
const bool renamed = decision_->show(window_);   // bloquea hasta que se cierra
decision_.reset();
```
**Qué pasa:** el objeto vive solo dentro de esta función. Grep confirma que `decision_` no se usa en ningún otro sitio. Basta con `Decision decision(instance_, controller_, request); return decision.show(window_);`. El miembro solo tendría sentido si otra parte de la ventana necesitara acceder al diálogo mientras está abierto, y ahora no ocurre.

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

La asignación dinámica y su uso local siguen presentes. Es una consideración de diseño, no un fallo funcional. Los tests de renombrado, omisión y cierre durante el diálogo pasaron.

#### 11. `loadPercentage_` sobra (baja, limpieza)
```cpp
// Window.cpp:106-112
loadPercentage_ = percentage;
SendMessageW(progressBar_, PBM_SETPOS, static_cast<WPARAM>(loadPercentage_), 0);
setStatus(L"Copying... " + std::to_wstring(loadPercentage_) + L"%");
```
**Qué pasa:** se escribe y se lee en la misma función y en ningún otro sitio (confirmado con grep). Se puede usar `percentage` directamente y borrar el miembro de `Window.hpp:72`.

### src/Decision.cpp

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

La revisión mantiene la observación sobre loadPercentage_. Las pruebas de interfaz pasaron; no demuestran un defecto funcional causado por este miembro.

#### 12. Un fallo del diálogo cuenta como "el usuario omitió" (media, bug)
```cpp
// Decision.cpp:60-64
window_ = CreateWindowExW(...);
if (window_ == nullptr) {
    return false;          // mismo valor que "el usuario cerró la ventana"
}
```
```cpp
// Controller.cpp:220: el hilo de copia recibe FALSE y omite la entrada
|| SendMessageW(window, WM_APP_RENAME_REQUEST, ...) == FALSE) {
    copier_.skip(i);
    ++skipped;
```
**Qué pasa:** si la ventana no se puede crear, la entrada se marca como omitida y aparece en el resumen como "skipped by the user or impossible to shorten". Fue un error técnico, no una decisión del usuario. La solución es que `show()` devuelva un `enum` con los valores Renamed, Cancelled y Error.

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

El código sigue devolviendo false ante el fallo de creación del diálogo y el controlador lo interpreta como omisión. El test de omisión voluntaria pasó; no se fuerza el fallo de creación. La confusión de causas no se reprodujo en ejecución.

#### 13. Se ignoran todos los errores de `RegisterClassExW` (baja, riesgo)
```cpp
// Decision.cpp:132-133
// Fails harmlessly when the class is already registered.
RegisterClassExW(&windowClass);
```
**Qué pasa:** el comentario solo justifica un caso, que la clase ya esté registrada desde la segunda vez que se abre el diálogo. Pero el código ignora cualquier error. Lo correcto sería algo como `if (!RegisterClassExW(...) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) → error`. Si el registro falla de verdad, `CreateWindowExW` también falla y se llega al punto 12.

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

Se sigue ignorando el resultado de RegisterClassExW. Las pruebas abrieron los diálogos correctamente, pero no simulan errores de registro. Riesgo confirmado por inspección, no reproducido por tests.

#### 14. Controles sin validar y `acceptButton_` sin uso (media / baja)
```cpp
// Decision.cpp:193 y 207
textBox_ = CreateWindowExW(...);       // no se comprueba
acceptButton_ = CreateWindowExW(...);  // se guarda pero nunca se lee
```
**Qué pasa:** si `textBox_` falla, el usuario ve un diálogo sin caja de texto y no puede renombrar. `acceptButton_` (declarado en `Decision.hpp:54`) solo aparece en el constructor y en esta asignación, así que se puede borrar. En `Window`, en cambio, su `acceptButton_` sí se usa en la línea 471.

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

La revisión mantiene las observaciones sobre la validación de controles y el uso de acceptButton_. Los tests de diálogo pasaron con controles creados correctamente; no simulan su fallo.

#### 15. Código duplicado y formato distinto (baja, limpieza)
```cpp
// Decision.cpp:178-218: el cuerpo de la función tiene MENOS sangría que su cabecera
    // Explanation text.
    CreateWindowExW(0, L"STATIC", ..., window_,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_DECISION_LABEL)),
                    instance_, nullptr);
```
**Qué pasa:** se repiten los mismos parámetros (`window_`, el cast del id, `instance_`, `nullptr`) que en `Window` ya están agrupados en la lambda `createControl` (Window.cpp:191). Además, en las líneas 244, 249, 252 y 253 los `break` y la asignación están desalineados respecto al resto del `switch`.

### src/Controller.cpp

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

La duplicación y el formato son observaciones estáticas. Las pruebas de interfaz pasaron; no hay un fallo funcional demostrado por este punto.

#### 16. Posible bloqueo mutuo al salir (alta, riesgo condicionado)
```cpp
// Controller.cpp:33-38
Controller::~Controller() {
    if (workerThread_ != nullptr) {
        cancelRequested_.store(true);
        finishWorker();     // → WaitForSingleObject(workerThread_, INFINITE) en la línea 171
    }
}
```
```cpp
// Controller.cpp:220: el hilo de copia
if (... || cancelRequested_.load() || SendMessageW(window, WM_APP_RENAME_REQUEST, ...) == FALSE)
```
**Qué pasa:** `SendMessageW` desde otro hilo espera hasta que el hilo de la interfaz procese el mensaje. Si el hilo principal está dentro de `WaitForSingleObject(INFINITE)`, no procesa mensajes, y cada hilo queda esperando al otro.
**Cuándo ocurre:** solo si `run()` termina mientras la copia sigue en marcha, por ejemplo por el `-1` del punto 6, por un `WM_QUIT` de otro origen o por una excepción. Hay además una carrera concreta: el hilo de copia lee `cancelRequested_ == false`, justo después el destructor lo pone a `true` y empieza a esperar, y entonces el hilo llama a `SendMessageW` y se queda bloqueado. La ventana sigue existiendo porque `Window` no tiene destructor, así que `SendMessageW` no falla y simplemente espera para siempre.
El cierre normal con el botón X no tiene este problema, porque `onClose()` espera a que el hilo termine solo.

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

El código conserva WaitForSingleObject(INFINITE) y la petición síncrona SendMessageW. Pasó el test de cierre normal mientras el diálogo está abierto, pero no prueba la salida excepcional del bucle ni la carrera descrita. No se reprodujo un bloqueo; tampoco queda descartado.

#### 17. `catch (...)` descarta la causa del error (media, diagnóstico)
```cpp
// Controller.cpp:182-186
try {
    self->workerResult_ = self->runCopy();
} catch (...) {
    self->workerResult_ = {};   // solo queda "falló", sin saber por qué
}
```
**Qué pasa:** el `catch (...)` es necesario, porque una excepción que sale de un hilo termina el programa. El problema es que se pierde el motivo: un `std::bad_alloc` y cualquier otro error acaban en el mismo mensaje genérico. Conviene añadir antes un `catch (const std::exception &e)` que guarde `e.what()` o un código en `WorkerResult`.

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

El catch (...) sigue descartando el diagnóstico. Las suites no inyectan excepciones en el trabajador, por lo que no verificaron este comportamiento en ejecución.

#### 18. Funciones que solo delegan (baja, dudoso)
```cpp
// Controller.cpp:41-48, 102-104, 153-155
bool Controller::create(int showCommand) { return window_.create(showCommand); }
int  Controller::run() { return window_.run(); }
void Controller::onProgress(int p) { window_.updateProgressBar(p); }
RenameResult Controller::rename(...) { return copier_.rename(...); }
```
**Qué pasa:** cada una es una línea que pasa la llamada a otro objeto. El propio informe reconoce que tienen sentido si se quiere mantener la separación MVC: la vista no conoce al modelo y todo pasa por el controlador. Recomendación: **no tocarlas**.

### include/Common.hpp

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

Las funciones delegadoras siguen presentes y las pruebas pasaron. Es una decisión de estructura MVC, no un error demostrado; los resultados no justifican eliminarlas.

#### 19. Las excepciones pueden escapar de la WndProc (media, riesgo)
```cpp
// Common.hpp:72
return (self->*handler)(message, wParam, lParam);   // sin try/catch
```
**Qué pasa:** el código de Windows (`user32`) que llama a la WndProc está escrito en C y no espera excepciones de C++. Si un handler lanza una, por ejemplo un `std::bad_alloc` al concatenar `std::wstring` en `updateProgressBar` o en `Decision::createControls`, el comportamiento queda indefinido: en x64 puede saltarse marcos del sistema o terminar el proceso. Lo recomendado es un `try/catch` en este punto que registre el error y cierre de forma controlada. No sirve un `catch` que devuelva 0 y siga como si nada.

### src/main.cpp

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

El dispatcher sigue sin una frontera de captura. Ninguna prueba provoca una excepción en un handler. No se comprobó el efecto de una excepción atravesando la WndProc ni puede deducirse de estos tests una consecuencia concreta en Windows.

#### 20. Inicializaciones sin comprobar (media, riesgo)
```cpp
// main.cpp:14
InitCommonControlsEx(&commonControls);      // resultado ignorado

// main.cpp:17
const HRESULT comResult = CoInitializeEx(...);
// ...solo se usa en la línea 31 para decidir si llamar a CoUninitialize()
```
**Qué pasa:** si COM no se inicializa, la aplicación arranca igual. Al pulsar "Browse...", `CoCreateInstance` falla y aparece "The folder dialog could not be opened.", sin que el usuario sepa por qué. Si falla `InitCommonControlsEx`, no se puede crear la barra de progreso, y se llega al punto 7.

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

Las inicializaciones conservan el tratamiento de resultados señalado. Los tests no fuerzan fallos de InitCommonControlsEx o CoInitializeEx, y sus ejecutables tienen su propio main. No validan esas rutas de wWinMain.

#### 21. Un comentario promete algo que el código no hace (baja)
```cpp
// main.cpp:19
// The block destroys the Controller (and its window) before COM is released.
```
**Qué pasa:** destruir `Controller` destruye el objeto C++ `Window`, pero **no** la ventana de Windows (el `HWND`), porque `Window` no tiene un destructor que llame a `DestroyWindow`. En la salida normal no importa, porque la ventana ya se destruyó antes (`WM_DESTROY` → `PostQuitMessage`). En una salida anormal, el `HWND` seguiría vivo y apuntaría a un objeto ya destruido.

### Interfaz y manifiesto

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

Window sigue sin destructor explícito que destruya el HWND. Los tests de cierre normal y liberación de carpetas pasaron; no comprobaron la vida del HWND tras una salida excepcional del controlador. La observación sobre el comentario es estática.

#### 22. Tamaños fijos en píxeles y sin soporte de DPI (media, limitación)
```cpp
// Window.cpp:16-17
const int CLIENT_WIDTH  = 522;
const int CLIENT_HEIGHT = 244;
// Window.cpp:215
createControl(0, L"STATIC", L"Source directory:", 0, 12, 12, 400, 18, -1);
```
```xml
<!-- fullcopy.manifest: no hay <dpiAware> ni <dpiAwareness> -->
```
**Qué pasa:** con la pantalla al 150 % o al 200 %, Windows escala la ventana como un mapa de bits y se ve borrosa. Si se añadiera solo la declaración DPI en el manifiesto, la ventana se vería nítida pero diminuta, y el texto podría no caber en los controles. Por eso el informe advierte que primero hay que escalar las medidas, por ejemplo con `GetDpiForWindow` y `MulDiv(valor, dpi, 96)`, y solo después declarar el soporte.

#### post first test

Verificación sobre FullCopy 1.0.2, commit 0e006c1 (2026-11-02). Se recompilaron y ejecutaron las suites existentes: 6 tests de integración y 6 de interfaz aprobados, sin advertencias de compilación.

El código conserva dimensiones fijas y el manifiesto no declara DPI awareness. No se hicieron pruebas visuales a 150 % o 200 %; la borrosidad o el recorte no quedaron comprobados por estas suites.

---

## 4. Orden de prioridades

Coincide con el del informe, con un matiz: arreglar el **#6** (`GetMessageW`) reduce también el riesgo del **#16**, porque es una de las formas de que `run()` termine con el hilo de copia aún vivo. El **#1** sigue siendo el más importante, porque el programa dice "terminado" cuando faltan datos.

## Tests Doing

Fecha: 2026-11-02. Código comprobado: FullCopy 1.0.2, commit `0e006c1730ca82e38880324b24d47e8e44df2acb`. Se recompilaron las dos suites desde el código actualizado; no se reutilizaron los ejecutables anteriores de los tests.

### Comprobación de la versión

Comandos ejecutados en PowerShell desde `C:\Windows\System32\fullcopy`:

```powershell
Get-Content Makefile | Select-Object -First 20
& 'C:/Program Files/Git/cmd/git.exe' log -1 --format='%h %s'
& 'C:/Program Files/Git/cmd/git.exe' status --short
& 'C:/Program Files/Git/cmd/git.exe' show --stat HEAD
```

Resultado: `VERSION := 1.0.2`, commit `0e006c1 docs: release version 1.0.2`; `git status --short` no mostró cambios locales.

### Compilación de las pruebas

Directorio de trabajo utilizado: `C:\Users\lumin\.codex\visualizations\2026\11\02\01a19eab-9200-7e53-bda6-f639d5522c46`.

Se utilizó el compilador MinGW portátil de `toolchain/w64devkit/bin`. Los binarios se guardaron en esta carpeta alternativa porque previamente Windows había impedido escribir en la carpeta del proyecto bajo System32.

Comandos ejecutados en PowerShell desde ese directorio:

```powershell
$ErrorActionPreference = 'Stop'
$env:PATH = "$PWD/toolchain/w64devkit/bin;$env:PATH"
$root = 'C:/Windows/System32/fullcopy'
$flags = @('-std=c++17','-O2','-Wall','-Wextra','-Werror','-DUNICODE','-D_UNICODE','-D_WIN32_WINNT=0x0601',"-I$root/include")
& g++ @flags "$root/tests/copier_tests.cpp" "$root/src/Copier.cpp" "$root/src/Common.cpp" -o bin/copier_tests-1.0.2.exe -static -lshell32 -lole32 -luuid -lgdi32 -luser32
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& g++ @flags "$root/tests/ui_tests.cpp" "$root/src/Controller.cpp" "$root/src/Window.cpp" "$root/src/Decision.cpp" "$root/src/Copier.cpp" "$root/src/Common.cpp" -o bin/ui_tests-1.0.2.exe -static -lcomctl32 -lshell32 -lole32 -luuid -lgdi32 -luser32
exit $LASTEXITCODE
```

Resultado: código de salida 0; compilación sin errores ni advertencias, con advertencias tratadas como errores. En esta repetición se compilaron las suites, no un nuevo ejecutable principal de FullCopy 1.0.2.

### Ejecución

Comandos ejecutados desde el mismo directorio de trabajo:

```powershell
& ./bin/copier_tests-1.0.2.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& ./bin/ui_tests-1.0.2.exe
exit $LASTEXITCODE
```

Las pruebas de interfaz abrieron y cerraron ventanas automáticamente. Ambas suites terminaron con código de salida 0. Salida obtenida:

```text
PASS: binary/Unicode copy and folder release
PASS: rename, validation, skip and reload
PASS: file and directory path boundaries
PASS: failures and empty source
PASS: directory checks before a copy
PASS: deep scan, cancellation and handle release
PASS: rejected starts show their error
PASS: copy with quoted paths and folder release
PASS: rename a long name in the Decision window
PASS: skip a long directory with its content
PASS: close while the Decision window is open
PASS: completion without worker notification
```

### Pruebas realizadas y alcance

| Suite | Prueba | Resultado |
|---|---|---|
| Integración | Copia binaria y nombres Unicode; liberación de carpetas | PASS |
| Integración | Renombrado, validación, omisión y recarga | PASS |
| Integración | Límites de longitud de rutas de archivos y directorios | PASS |
| Integración | Fallos contemplados por la suite y origen vacío | PASS |
| Integración | Validación de directorios antes de copiar | PASS |
| Integración | Escaneo profundo, cancelación y liberación de handles | PASS |
| Interfaz | Inicios rechazados y sus mensajes de error | PASS |
| Interfaz | Copia con rutas entre comillas y liberación de carpetas | PASS |
| Interfaz | Renombrado de un nombre largo en el diálogo | PASS |
| Interfaz | Omisión de un directorio largo y su contenido | PASS |
| Interfaz | Cierre normal mientras está abierto el diálogo de renombrado | PASS |
| Interfaz | Finalización mediante temporizador sin notificación del trabajador | PASS |

Total: 12 pruebas aprobadas, 0 fallidas. No se añadieron tests ni se modificó el código del proyecto.

También se revisaron las implementaciones, las cabeceras, el manifiesto y las referencias de los símbolos señalados en el informe. Esa inspección estática se distingue de los resultados de ejecución en cada apartado `post first test`.

No se realizaron pruebas específicas de junctions, OneDrive, fallos forzados de GetMessageW/CreateThread/CreateWindowExW/RegisterClassExW, errores COM, excepciones inyectadas, salida excepcional con posible interbloqueo ni evaluación visual a diferentes escalas DPI. Los 12 resultados PASS no confirman ni descartan esos escenarios.
