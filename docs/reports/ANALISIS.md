# Pruebas pendientes de FullCopy

Actualizado: 2026-11-05.

Este documento contiene únicamente escenarios pendientes de validación sobre el código actual. Cada caso requiere ejecución y evidencia; los criterios de aceptación describen el resultado esperado, no un resultado ya obtenido. Algunos casos requieren inyección de fallos y pueden revelar defectos que aún necesitan corrección.

Después de cada tabla, la sección **Código relacionado** indica, para cada prueba, el bloque de código que ejercita y qué revisar en él. Las líneas corresponden al commit `80269c3`; si el código cambia, buscar por el nombre de la función.

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

### Código relacionado: manejo de errores y cierre

#### Error de `GetMessageW`

- [`src/Window.cpp:85-94`](../../src/Window.cpp#L85-L94), `Window::run()`: bucle principal.
- [`src/Decision.cpp:78-98`](../../src/Decision.cpp#L78-L98), `Decision::show()`: bucle del diálogo de renombrado.
- [`src/Warnings.cpp:76-96`](../../src/Warnings.cpp#L76-L96), `Warnings::show()`: el mismo bucle en la ventana de avisos.

```cpp
int Window::run() {
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (window_ == nullptr || !IsDialogMessageW(window_, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}
```

```cpp
// Decision::show()
const BOOL result = GetMessageW(&message, nullptr, 0, 0);
if (result == 0) {
// Give WM_QUIT back to the main loop.
    PostQuitMessage(static_cast<int>(message.wParam));
    break;
}
// GetMessageW failed.
if (result == -1) {
    break;
}
```

Qué revisar:

- La condición `> 0` trata el `-1` igual que `WM_QUIT`: el bucle termina y devuelve `message.wParam`, que en ese caso no viene de `WM_QUIT`. Puede ser de un mensaje anterior o, si falla la primera llamada, un valor sin inicializar (`MSG message;`).
- En `Decision` y `Warnings` el error solo hace `break`: no se llama a `GetLastError()` ni se informa nada. `Decision::show()` devuelve entonces `false` y la copia lo trata como si el usuario hubiera omitido la entrada (ver la prueba siguiente).

#### Fallo al crear ventanas y controles

- [`src/Window.cpp:61-80`](../../src/Window.cpp#L61-L80), `Window::create()`: crea la ventana principal y llama a `createControls()`.
- [`src/Window.cpp:193-316`](../../src/Window.cpp#L193-L316), `Window::createControls()`.
- [`src/Decision.cpp:63-76`](../../src/Decision.cpp#L63-L76) y [`src/Decision.cpp:170-224`](../../src/Decision.cpp#L170-L224): ventana y controles del diálogo **Name too long**.
- [`src/Controller.cpp:242-252`](../../src/Controller.cpp#L242-L252), `Controller::runCopy()`: qué hace la copia con el resultado del diálogo.

```cpp
// Decision::show()
window_ = CreateWindowExW(extendedStyle, CLASS_NAME, L"Name too long", style,
                      x, y, width, height, parent_, nullptr, instance_, this);
if (window_ == nullptr) {
    return false;
}
```

```cpp
// Controller::runCopy()
while (!copier_.check(i)) {
    // Skip the entry if no name can fit, if the user cancels, or if the user closes
    // the Decision window.
    if (copier_.getMaxNameLength(i) == 0 || cancelRequested_.load() || SendMessageW(window, WM_APP_RENAME_REQUEST, static_cast<WPARAM>(i), 0) == FALSE) {
        copier_.skip(i);
        ++skipped;
        break;
    }
}
```

Qué revisar:

- `Window::create()` comprueba la ventana principal, pero `createControls()` no comprueba ningún `CreateWindowExW` de los controles: `create()` devuelve `true` aunque falte alguno. `Decision::createControls()` tampoco los comprueba.
- Si falla la ventana del diálogo, `Decision::show()` devuelve `false`, el mismo valor que cuando el usuario la cierra. La copia llama a `copier_.skip(i)` y lo suma a `skipped`: el fallo se cuenta como omisión voluntaria.
- Como referencia, `Warnings::createControls()` ([`src/Warnings.cpp:164-189`](../../src/Warnings.cpp#L164-L189)) sí comprueba sus controles y, si algo falla, muestra el aviso con `MessageBoxW` ([`src/Warnings.cpp:58-69`](../../src/Warnings.cpp#L58-L69)).

#### Fallo al registrar la clase del diálogo

- [`src/Decision.cpp:126-137`](../../src/Decision.cpp#L126-L137), `Decision::registerClass()`, comprobada desde [`src/Decision.cpp:42-46`](../../src/Decision.cpp#L42-L46).

```cpp
bool Decision::registerClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass = {};
    // ...
    windowClass.lpszClassName = CLASS_NAME;

    return RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}
```

```cpp
// Decision::show()
if (!registerClass(instance_)) {
    MessageBoxW(parent_, L"The rename window could not be opened.", L"FullCopy",
                MB_OK | MB_ICONERROR);
    return false;
}
```

Qué revisar:

- Solo se tolera `ERROR_CLASS_ALREADY_EXISTS`, igual que en `Warnings::registerClass()` ([`src/Warnings.cpp:124-134`](../../src/Warnings.cpp#L124-L134)). Cualquier otro error muestra un `MessageBoxW` sobre la ventana principal.
- El mensaje no incluye el código de `GetLastError()`.
- `show()` devuelve `false`, el mismo valor que cuando el usuario cierra la ventana: la copia sigue contando el fallo como omisión (ver la prueba anterior).

#### Fallos del selector de carpetas

- [`src/Window.cpp:327-332`](../../src/Window.cpp#L327-L332), `Window::onBrowse()`.

```cpp
// Pick only existing folders of the file system.
DWORD options = 0;
dialog->GetOptions(&options);
dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
```

Qué revisar:

- No se comprueba el `HRESULT` de ninguna de las dos llamadas. Si `GetOptions` falla, `options` queda en `0` y se pierden las opciones por defecto del diálogo.
- Si `SetOptions` falla, el diálogo se muestra igual ([`src/Window.cpp:335`](../../src/Window.cpp#L335)) sin `FOS_PICKFOLDERS`, es decir, como selector de archivos.

#### Fallo al crear el trabajador

- [`src/Controller.cpp:76-86`](../../src/Controller.cpp#L76-L86), `Controller::onStart()`.
- [`src/Window.cpp:415-427`](../../src/Window.cpp#L415-L427), `Window::showCopyStarted()` y `Window::showStartFailed()`.

```cpp
// Controller::onStart()
cancelRequested_.store(false);
closePending_ = false;
window_.showCopyStarted();

// The copy runs on its own thread, so the window keeps responding.
workerThread_ = CreateThread(nullptr, 0, copyThread, this, 0, nullptr);
if (workerThread_ == nullptr) {
    KillTimer(window_.getHandle(), WORKER_POLL_TIMER);
    window_.showStartFailed();
}
```

```cpp
void Window::showStartFailed() {
    setControlsEnabled(true);
    showError(L"The copy could not be started.");
}
```

Qué revisar:

- `showCopyStarted()` deshabilita los controles y escribe *Reading the source directory...* antes de crear el hilo. Si `CreateThread` falla, `showStartFailed()` vuelve a habilitar los controles y muestra el error, pero no cambia el texto de estado: sigue diciendo *Reading the source directory...*.
- El fallo de `SetTimer` ([`src/Controller.cpp:70-73`](../../src/Controller.cpp#L70-L73)) ocurre antes de `showCopyStarted()`, así que no tiene este problema.

#### Inicialización de la aplicación

- [`src/main.cpp:10-17`](../../src/main.cpp#L10-L17) y [`src/main.cpp:30-33`](../../src/main.cpp#L30-L33), `wWinMain()`.

```cpp
INITCOMMONCONTROLSEX commonControls = {};
commonControls.dwSize = sizeof(commonControls);
commonControls.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
InitCommonControlsEx(&commonControls);

// Needed by the folder selection dialog.
const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
```

Qué revisar:

- El resultado de `InitCommonControlsEx` se ignora. La barra de progreso (`PROGRESS_CLASSW`, [`src/Window.cpp:261-270`](../../src/Window.cpp#L261-L270)) depende de él, y su creación tampoco se comprueba.
- `comResult` solo decide si se llama a `CoUninitialize()`. Si COM no se inicializa, los botones *Browse...* siguen habilitados; al pulsarlos, `CoCreateInstance` falla y se muestra *The folder dialog could not be opened.* ([`src/Window.cpp:321-325`](../../src/Window.cpp#L321-L325)).

#### Cierre excepcional con trabajador activo

- [`src/main.cpp:19-28`](../../src/main.cpp#L19-L28): el bloque que destruye el `Controller` al salir de `run()`.
- [`src/Controller.cpp:35-40`](../../src/Controller.cpp#L35-L40) y [`src/Controller.cpp:181-189`](../../src/Controller.cpp#L181-L189): destructor y `finishWorker()`.
- [`src/Controller.cpp:216-221`](../../src/Controller.cpp#L216-L221) y [`src/Controller.cpp:247`](../../src/Controller.cpp#L247): los dos `SendMessageW` del trabajador (recursión y renombrado).

```cpp
Controller::~Controller() {
    if (workerThread_ != nullptr) {
        cancelRequested_.store(true);
        finishWorker();
    }
}

void Controller::finishWorker() {
    if (workerThread_ != nullptr) {
        WaitForSingleObject(workerThread_, INFINITE);
        // ...
    }
}
```

Qué revisar:

- El cierre normal durante una copia no espera: `onClose()` marca `closePending_` y la ventana se cierra cuando el hilo termina.
- Si `run()` termina por otra vía (una excepción o un error de `GetMessageW`) con el trabajador vivo, el destructor espera con `INFINITE`. Si en ese momento el trabajador está bloqueado en `SendMessageW`, necesita que el hilo principal procese mensajes, y `WaitForSingleObject` no los procesa: es la espera circular que la prueba debe descartar.
- La comprobación de `cancelRequested_` antes de cada `SendMessageW` solo evita envíos nuevos, no uno que ya está esperando respuesta.

#### Excepciones del trabajador

- [`src/Controller.cpp:193-204`](../../src/Controller.cpp#L193-L204), `Controller::copyThread()`.

```cpp
DWORD WINAPI Controller::copyThread(LPVOID parameter) {
    auto *self = static_cast<Controller*>(parameter);
    try {
        self->workerResult_ = self->runCopy();
    } catch (...) {
        self->workerResult_ = {};
    }
    // ...
    PostMessageW(self->window_.getHandle(), WM_APP_WORKER_DONE, 0, 0);
    return self->workerResult_.succeeded ? 0 : 1;
}
```

Qué revisar:

- `catch (...)` sustituye el resultado por uno vacío (`succeeded == false`) y descarta la excepción: no se guarda `what()` ni se distingue una excepción estándar de una no estándar. La interfaz muestra el mensaje genérico de `Window::showCopyFailed()` ([`src/Window.cpp:467-473`](../../src/Window.cpp#L467-L473)).
- Los handles de búsqueda y de directorio del `Copier` usan RAII (`DirectoryHandle` y `FindHandle`, [`src/Copier.cpp:23-37`](../../src/Copier.cpp#L23-L37) y [`src/Copier.cpp:122-141`](../../src/Copier.cpp#L122-L141)), así que se cierran al propagarse la excepción.
- Un punto natural para inyectar la excepción es `Copier::copy()`, que usa `entries_.at(index)` ([`src/Copier.cpp:314`](../../src/Copier.cpp#L314)).

#### Excepciones en callbacks

- [`include/Common.hpp:50-76`](../../include/Common.hpp#L50-L76), `dispatchWindowMessage()`.
- Los tres `windowProcedure`: [`src/Window.cpp:117-124`](../../src/Window.cpp#L117-L124), [`src/Decision.cpp:141-144`](../../src/Decision.cpp#L141-L144) y [`src/Warnings.cpp:138-141`](../../src/Warnings.cpp#L138-L141).

```cpp
// dispatchWindowMessage()
if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    self->*windowMember = nullptr;
    return DefWindowProcW(window, message, wParam, lParam);
}
return (self->*handler)(message, wParam, lParam);
```

Qué revisar:

- Ni `dispatchWindowMessage()` ni los `windowProcedure` tienen `try`/`catch`: una excepción de un handler sale por el `WndProc` y atraviesa `DispatchMessageW`, algo que Windows no admite.
- Handlers que reservan memoria y pueden lanzar `std::bad_alloc`: `Window::askNewName()` (`new Decision`, [`src/Window.cpp:373-379`](../../src/Window.cpp#L373-L379)), `Controller::onRecursionFound()` ([`src/Controller.cpp:129-136`](../../src/Controller.cpp#L129-L136)), `Controller::onCurrentEntry()` ([`src/Controller.cpp:139-145`](../../src/Controller.cpp#L139-L145)), `Window::showCopyFinished()` ([`src/Window.cpp:435-465`](../../src/Window.cpp#L435-L465)) y `Decision::onAccept()` ([`src/Decision.cpp:228-268`](../../src/Decision.cpp#L228-L268)).

#### Destrucción de ventanas en rutas excepcionales

- [`include/Window.hpp:20-25`](../../include/Window.hpp#L20-L25): `Window` no declara destructor.
- [`src/Decision.cpp:31-36`](../../src/Decision.cpp#L31-L36) y [`src/Warnings.cpp:27-32`](../../src/Warnings.cpp#L27-L32): destructores que destruyen su ventana.
- [`include/Common.hpp:70-74`](../../include/Common.hpp#L70-L74): `WM_NCDESTROY` desconecta la ventana de su objeto.

```cpp
// Makes sure the window does not outlive this object.
Decision::~Decision() {
    if (window_ != nullptr) {
        selfDestroy();
    }
}
```

Qué revisar:

- `Decision` y `Warnings` destruyen su `HWND` en el destructor; `Window` no. Si el objeto `Window` se destruye sin pasar por `Window::close()` (por ejemplo, si `run()` termina por un error de `GetMessageW`), la ventana principal sigue existiendo y su `GWLP_USERDATA` apunta a un objeto destruido.
- Si esa ventana recibe algún mensaje después (el proceso todavía llama a `CoUninitialize()`, [`src/main.cpp:31-33`](../../src/main.cpp#L31-L33)), `handleMessage()` se ejecutaría sobre ese objeto.

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

### Código relacionado: archivos, referencias e integridad

#### Junction hacia otro volumen

- [`src/Copier.cpp:520-524`](../../src/Copier.cpp#L520-L524): volumen del origen.
- [`src/Copier.cpp:612-618`](../../src/Copier.cpp#L612-L618), `Copier::scanDirectory()`: decide si se sigue una referencia.
- [`src/Copier.cpp:174-195`](../../src/Copier.cpp#L174-L195): `getRealPath()` y `getVolume()`.
- [`src/Controller.cpp:226-230`](../../src/Controller.cpp#L226-L230) y [`src/Window.cpp:440-442`](../../src/Window.cpp#L440-L442): lista y ventana **Referencia Inaccesible**.

```cpp
// A reference is followed only if its target exists on the source volume.
std::wstring target;
if (sourceVolume.empty() || !getRealPath(getFullSourcePath(path), target)
    || !equalsIgnoreCase(getVolume(target), sourceVolume)) {
    inaccessible_.push_back(path);
    continue;
}
```

Qué revisar:

- El volumen se compara por GUID (`\\?\Volume{...}`), no por letra de unidad. La referencia rechazada no se añade a `entries_`, y por eso no se crea en el destino.
- `generate_manual_tree.exe` crea `junctions\other-volume` apuntando a otra unidad fija, o a la carpeta indicada con `--other-volume DIR`. El CI solo lo comprueba si el runner tiene otra unidad.

#### Enlaces simbólicos

- [`src/Copier.cpp:602-633`](../../src/Copier.cpp#L602-L633), `Copier::scanDirectory()`: tratamiento de referencias.
- [`src/Copier.cpp:331-343`](../../src/Copier.cpp#L331-L343), `Copier::copy()`: copia de archivos.

```cpp
// Junctions and symbolic links point to another path ("name surrogates"). Other
// reparse points, like OneDrive files, are the real file or directory.
const bool reference = (child.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
                       && IsReparseTagNameSurrogate(child.reparseTag);
// ...
// A reference to this directory or to one that contains it would be scanned forever.
bool recursive = false;
for (std::size_t ancestor = self; directoryEntry && !recursive && ancestor != NO_PARENT;
     ancestor = ancestors[ancestor].parent) {
    recursive = isSameOrInside(ancestors[ancestor].realPath, target);
}
```

Qué revisar:

- Junctions y enlaces simbólicos siguen el mismo camino: se siguen si el destino existe en el volumen del origen y, si no, se listan como inaccesibles. Un enlace roto también es inaccesible, porque `getRealPath()` falla.
- La detección de ciclos solo se aplica a directorios (`directoryEntry &&`).
- Un enlace a archivo se copia con `CopyFileExW` sin `COPY_FILE_COPY_SYMLINK`, así que en el destino queda una copia del contenido, no un enlace.
- Ni los tests automáticos ni `generate_manual_tree.exe` crean enlaces simbólicos, solo junctions. Hay que crearlos a mano con `mklink` o `mklink /D`, que necesitan permisos de administrador o el modo de desarrollador.

#### OneDrive

- [`src/Copier.cpp:197-211`](../../src/Copier.cpp#L197-L211), `isCloudError()`: errores que se tratan como "no disponible".
- [`src/Copier.cpp:544-553`](../../src/Copier.cpp#L544-L553): directorios que no se pueden leer.
- [`src/Copier.cpp:345-346`](../../src/Copier.cpp#L345-L346): archivos que no se pueden copiar.
- [`src/Controller.cpp:259-270`](../../src/Controller.cpp#L259-L270): cómo se cuenta cada resultado.

```cpp
// A directory reached through a reference, or a OneDrive directory that can not be
// read now, is not copied and the scan goes on. Its entry is the last one added.
const auto skipUnreadable = [this, &item](DWORD error) {
    if (!item.addEntry || !(item.isReference || isCloudError(error))) {
        return false;
    }
    entries_.pop_back();
    inaccessible_.push_back(item.path);
    return true;
};
```

```cpp
// Copier::copy()
// A OneDrive file that can not be downloaded now is reported apart from the other errors.
return isCloudError(GetLastError()) ? CopyResult::Inaccessible : CopyResult::Failed;
```

Qué revisar:

- Los archivos solo en la nube son puntos de reanálisis, pero no referencias (no son *name surrogates*): se copian como archivos normales y `CopyFileExW` los descarga si OneDrive funciona.
- Solo los errores de `isCloudError()` se listan como inaccesibles. Cualquier otro error al copiar un archivo se cuenta como fallo, y cualquier otro error al leer un directorio detiene el escaneo.

#### Orígenes de red

- [`src/Copier.cpp:642-655`](../../src/Copier.cpp#L642-L655), `getFullSourcePath()`: prefijo `\\?\UNC\`.
- [`src/Copier.cpp:520-524`](../../src/Copier.cpp#L520-L524) y [`src/Copier.cpp:614`](../../src/Copier.cpp#L614): ninguna referencia se sigue.
- [`src/Copier.cpp:562-590`](../../src/Copier.cpp#L562-L590): errores de lectura durante el escaneo.

```cpp
// Only references to the volume of the source are followed. A network source has no
// volume GUID, so none of its references is followed.
std::wstring rootRealPath;
const std::wstring sourceVolume = getRealPath(getFullSourcePath(relativePath), rootRealPath)
                                  ? getVolume(rootRealPath) : std::wstring();
```

Qué revisar:

- Con un origen de red, `sourceVolume` queda vacío y la condición de la línea 614 lista todas sus referencias como inaccesibles.
- Si se corta la conexión durante el escaneo, `FindFirstFileExW` o `FindNextFileW` fallan y `scanDirectory()` devuelve `false`: la copia entera termina con *The copy could not be completed*. Si se corta durante la copia, cada archivo afectado cuenta como fallo.
- La cancelación de un archivo en curso depende de que `CopyFileExW` llame a `copyProgress()` ([`src/Copier.cpp:102-120`](../../src/Copier.cpp#L102-L120)). Con una conexión colgada, medir cuánto tarda.

#### Errores de acceso y espacio

- [`src/Copier.cpp:306-347`](../../src/Copier.cpp#L306-L347), `Copier::copy()`.
- [`src/Copier.cpp:268-277`](../../src/Copier.cpp#L268-L277), `Copier::load()`: creación de la carpeta de destino.
- [`src/Copier.cpp:560-571`](../../src/Copier.cpp#L560-L571): error al abrir un directorio del origen.
- [`src/Window.cpp:435-465`](../../src/Window.cpp#L435-L465), `Window::showCopyFinished()`: resumen.

```cpp
// Copier::copy()
if (entry.isDirectory) {
    // A directory that already exists is fine, but a file with the same name is not.
    if (CreateDirectoryW(destination.c_str(), nullptr)) {
        return CopyResult::Copied;
    }
    const DWORD error = GetLastError();
    return error == ERROR_ALREADY_EXISTS && isDirectory(destination) ? CopyResult::Copied
                                                                     : CopyResult::Failed;
}
```

Qué revisar:

- Destino de solo lectura, sin espacio o archivo de origen bloqueado: `CreateDirectoryW` o `CopyFileExW` fallan, la entrada cuenta como fallo y la copia sigue con las demás. Con el disco lleno, cada archivo restante falla uno por uno.
- Si no se puede crear la carpeta de destino, `load()` devuelve `false` y se muestra *The copy could not be completed*.
- Un subdirectorio del origen sin permiso de lectura no es una referencia ni un error de la nube, así que `scanDirectory()` devuelve `false` y no se copia nada. Hay que decidir si eso cumple el criterio.

#### Integridad bajo carga

No hay un bloque único: la prueba recorre todo el flujo de copia.

- [`src/Controller.cpp:232-278`](../../src/Controller.cpp#L232-L278), `Controller::runCopy()`: bucle de copia.
- [`src/Copier.cpp:331-346`](../../src/Copier.cpp#L331-L346), `Copier::copy()`: copia de cada archivo.

```cpp
// Copier::copy()
const std::wstring source = getFullSourcePath(entry.sourcePath);
if (CopyFileExW(
    source.c_str(),
    destination.c_str(),
    copyProgress,
    cancellation,
    nullptr,
    COPY_FILE_FAIL_IF_EXISTS
) != FALSE) {
    return CopyResult::Copied;
}
```

Qué revisar:

- `COPY_FILE_FAIL_IF_EXISTS` nunca sobrescribe: si las 20 repeticiones usan el mismo destino, a partir de la segunda todos los archivos cuentan como fallos. Usar un destino nuevo en cada repetición.
- Toda la lista de entradas (`entries_`, con dos rutas por entrada) vive en memoria durante la copia; su tamaño crece con el número de archivos.

#### Profundidad y rutas largas bajo carga

- [`src/Copier.cpp:491-640`](../../src/Copier.cpp#L491-L640), `Copier::scanDirectory()`: escaneo iterativo.
- [`include/Copier.hpp:49-55`](../../include/Copier.hpp#L49-L55): límites de longitud del destino.
- [`src/Copier.cpp:450-464`](../../src/Copier.cpp#L450-L464), `Copier::getMaxNameLength()`.
- [`tests/copier_tests.cpp:268-295`](../../tests/copier_tests.cpp#L268-L295): caso automatizado de 300 niveles.

```cpp
// Windows limits (without the "\\?\" prefix):
//  - a file path must be shorter than MAX_PATH (260),
//  - a directory path must leave room for an 8.3 file name (248),
//  - a single name can not be longer than 255 characters.
static const std::size_t MAX_FILE_PATH_LENGTH      = 259;
static const std::size_t MAX_DIRECTORY_PATH_LENGTH = 247;
static const std::size_t MAX_NAME_LENGTH           = 255;
```

Qué revisar:

- El escaneo usa una pila propia (`pending`), no recursión, así que la profundidad no consume la pila de llamadas. `ancestors` guarda una entrada por cada directorio escaneado hasta el final, así que su memoria crece con el número de directorios.
- Por cada referencia, la detección de ciclos sube por la cadena de padres ([`src/Copier.cpp:621-625`](../../src/Copier.cpp#L621-L625)): su coste crece con la profundidad.
- El caso automatizado crea 300 niveles de `\d`. Para ir más allá, usar más niveles o nombres más largos.
- El origen se lee con el prefijo `\\?\`, pero el destino está limitado a 259 y 247 caracteres. Cuando la ruta de los padres ya ocupa todo el límite, `getMaxNameLength()` devuelve `0` y la entrada se omite sin preguntar ([`src/Controller.cpp:247`](../../src/Controller.cpp#L247)).

#### Rendimiento de renombrado y omisión

- [`src/Copier.cpp:370-375`](../../src/Copier.cpp#L370-L375) y [`src/Copier.cpp:393-403`](../../src/Copier.cpp#L393-L403), `Copier::rename()`.
- [`src/Copier.cpp:408-421`](../../src/Copier.cpp#L408-L421), `Copier::skip()`.

```cpp
void Copier::skip(std::size_t index) {
    CopyEntry &entry = entries_[index];
    entry.skipped = true;

    if (entry.isDirectory) {
        const std::wstring prefix = entry.sourcePath + L'\\';
        for (std::size_t i = index + 1; i < entries_.size(); ++i) {
            if (startsWith(entries_[i].sourcePath, prefix)) {
                entries_[i].skipped = true;
            }
        }
    }
}
```

Qué revisar:

- `rename()` compara el nombre nuevo con todas las entradas y luego actualiza el prefijo desde `index + 1` hasta el final. `skip()` también recorre hasta el final.
- Las entradas están en preorden, así que el subárbol es contiguo, pero los bucles no se detienen cuando el prefijo deja de coincidir. Renombrar u omitir un subárbol pequeño seguido de muchas entradas ajenas visita todas esas entradas: ese es el coste que hay que medir.

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

### Código relacionado: interfaz, cancelación y recursos

#### Liberación después de Browse

- [`src/Window.cpp:318-349`](../../src/Window.cpp#L318-L349), `Window::onBrowse()`.
- [`src/main.cpp:30-33`](../../src/main.cpp#L30-L33): liberación de COM.

```cpp
// Window::onBrowse()
// Show it. If the user picks a folder, write its path in the text box.
if (SUCCEEDED(dialog->Show(window_))) {
    IShellItem *item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            SetWindowTextW(targetEdit, path);
            CoTaskMemFree(path);
        }
        item->Release();
    }
}

// COM objects are released with Release(), and the strings they return with CoTaskMemFree().
dialog->Release();
```

Qué revisar:

- Todos los caminos liberan el diálogo, el `IShellItem` y la cadena. Las opciones no incluyen `FOS_NOCHANGEDIR`: si la carpeta no se puede borrar, comprobar también el directorio de trabajo del proceso (por ejemplo, con Process Explorer).
- La copia usa RAII para sus handles (`DirectoryHandle` y `FindHandle` en `Copier.cpp`). Los tests automáticos ya comprueban la liberación de carpetas sin usar Browse.

#### Cancelación durante trabajo activo

- [`src/Controller.cpp:91-101`](../../src/Controller.cpp#L91-L101), `Controller::onClose()`: pide la cancelación.
- [`src/Copier.cpp:528-536`](../../src/Copier.cpp#L528-L536) y [`src/Copier.cpp:573-576`](../../src/Copier.cpp#L573-L576): comprobaciones durante el escaneo.
- [`src/Copier.cpp:102-120`](../../src/Copier.cpp#L102-L120), `copyProgress()`: cancelación dentro de un archivo.
- [`src/Controller.cpp:147-179`](../../src/Controller.cpp#L147-L179): `pollWorker()` y `finishCopy()` cierran la ventana cuando termina el hilo.

```cpp
// Progress callback of CopyFileExW, called while a file is being copied. When the user
// cancels, it stops the copy and Windows deletes the partial file.
DWORD CALLBACK copyProgress(
    // ...
    LPVOID context
) {
    const auto *cancellation = static_cast<const std::atomic_bool*>(context);
    if (cancellation != nullptr && cancellation->load()) {
        return PROGRESS_CANCEL;
    }
    return PROGRESS_CONTINUE;
}
```

Qué revisar:

- No hay botón de cancelar: se cancela cerrando la ventana (X o Alt+F4; *Accept* está deshabilitado durante la copia). `onClose()` marca `cancelRequested_` y `closePending_` y muestra *Cancelling...*.
- Durante el escaneo se comprueba la cancelación en cada directorio y en cada entrada encontrada. Durante un archivo grande depende de cada llamada de `CopyFileExW` a `copyProgress()`; `PROGRESS_CANCEL` hace que Windows borre el archivo parcial.
- Cuando el hilo termina, `finishCopy()` cierra la ventana sin mostrar el resumen.

#### Interacción real con teclado y ratón

- [`src/Window.cpp:85-94`](../../src/Window.cpp#L85-L94) y [`src/Window.cpp:128-190`](../../src/Window.cpp#L128-L190): bucle con `IsDialogMessageW` y `Window::handleMessage()`.
- [`src/Decision.cpp:146-167`](../../src/Decision.cpp#L146-L167), `Decision::handleMessage()`.
- [`src/Warnings.cpp:143-160`](../../src/Warnings.cpp#L143-L160), `Warnings::handleMessage()`.

```cpp
LRESULT Decision::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: // Accept button or Enter key.
                    onAccept();
                    return 0;
                case IDCANCEL: // Escape key.
                    selfDestroy();
                    return 0;
            }
        break;

    case WM_CLOSE:
        selfDestroy();
        return 0;
    }

    return DefWindowProcW(window_, message, wParam, lParam);
}
```

Qué revisar:

- Ventana principal: Enter llega como `IDOK` y solo inicia la copia si *Start* está habilitado. Escape llega como `IDCANCEL`, que `handleMessage()` no trata, así que no hace nada. X y Alt+F4 llegan como `WM_CLOSE` y pasan por `Controller::onClose()`.
- Diálogo **Name too long**: Enter y *Accept* aplican el nombre; Escape, X y Alt+F4 cierran el diálogo y la entrada se omite.
- Ventana de avisos: todas las acciones la cierran.
- Mientras un diálogo está abierto, la ventana principal queda deshabilitada (`EnableWindow(parent_, FALSE)`) y se habilita de nuevo en `selfDestroy()`.
- El orden de Tab sigue el orden de creación de los controles con `WS_TABSTOP` ([`src/Window.cpp:218-312`](../../src/Window.cpp#L218-L312)).

#### Avisos de recursión e inaccesibilidad

- [`src/Controller.cpp:213-221`](../../src/Controller.cpp#L213-L221) y [`src/Controller.cpp:127-136`](../../src/Controller.cpp#L127-L136): aviso de recursión durante el escaneo.
- [`src/Window.cpp:434-465`](../../src/Window.cpp#L434-L465), `Window::showCopyFinished()`: aviso de referencias inaccesibles y resumen.
- [`src/Warnings.cpp:36-102`](../../src/Warnings.cpp#L36-L102), `Warnings::show()`.

```cpp
// Controller::runCopy()
std::size_t recursions = 0;
const auto onRecursion = [this, window, &recursions](const std::wstring &relativePath) {
    ++recursions;
    if (!cancelRequested_.load()) {
        SendMessageW(window, WM_APP_RECURSION_FOUND, 0, reinterpret_cast<LPARAM>(&relativePath));
    }
};
```

Qué revisar:

- El aviso de recursión se muestra durante el escaneo, y el trabajador espera en `SendMessageW` hasta que se cierra. Las referencias inaccesibles se muestran todas juntas al final, antes del resumen.
- La ventana de avisos tiene una sola acción: *Aceptar*, Enter, Escape y X la cierran igual y el escaneo continúa. La única forma de cancelar es cerrar la ventana principal.
- Si ya se pidió la cancelación, el aviso no se muestra (`cancelRequested_` aquí y `closePending_` en `onRecursionFound()`), pero la recursión se cuenta igual.
- Si la ventana de avisos no se puede crear, el aviso se muestra con `MessageBoxW` ([`src/Warnings.cpp:191-195`](../../src/Warnings.cpp#L191-L195)).

#### Escalado DPI

- [`resources/fullcopy.manifest`](../../resources/fullcopy.manifest).
- Tamaños fijos: [`src/Window.cpp:16-17`](../../src/Window.cpp#L16-L17), [`src/Decision.cpp:14-15`](../../src/Decision.cpp#L14-L15) y [`src/Warnings.cpp:11-12`](../../src/Warnings.cpp#L11-L12), más las posiciones de cada control en los `createControls()`.
- [`src/Common.cpp:17-53`](../../src/Common.cpp#L17-L53), `getUiFont()`.

```xml
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <assemblyIdentity type="win32" name="FullCopy" version="1.0.2.0" processorArchitecture="amd64"/>
  <description>FullCopy - directory copier</description>
  <!-- Modern look for buttons, edit boxes and the progress bar. -->
  <dependency>
    <!-- ... Common-Controls 6.0 ... -->
  </dependency>
</assembly>
```

Qué revisar:

- El manifiesto no declara `dpiAware` ni `dpiAwareness`, y el código no llama a `SetProcessDpiAwareness*`. Windows trata el programa como no consciente de DPI y escala la ventana como una imagen: lo esperable es texto borroso, no controles superpuestos.
- Todas las posiciones y tamaños están en píxeles fijos.

#### Memoria, handles y objetos GDI

No hay un bloque único. Esto es lo que se crea y se libera en cada ciclo:

| Recurso | Se crea en | Se libera en |
|---|---|---|
| Diálogo de carpetas (COM) | `Window::onBrowse()`, [`src/Window.cpp:321-322`](../../src/Window.cpp#L321-L322) | `Release()` y `CoTaskMemFree()` en el mismo método |
| Hilo de copia | `Controller::onStart()`, [`src/Controller.cpp:82`](../../src/Controller.cpp#L82) | `CloseHandle()` en `finishWorker()`, [`src/Controller.cpp:181-189`](../../src/Controller.cpp#L181-L189) |
| Temporizador | `SetTimer()`, [`src/Controller.cpp:70`](../../src/Controller.cpp#L70) | `KillTimer()` en `finishWorker()` |
| Handles de búsqueda y de directorio | `Copier::scanDirectory()` y `getRealPath()` | RAII: `FindHandle` y `DirectoryHandle` |
| Ventana **Name too long** | `new Decision` en `Window::askNewName()`, [`src/Window.cpp:373-379`](../../src/Window.cpp#L373-L379) | `decision_.reset()` y `Decision::selfDestroy()` |
| Fuente (GDI) | `getUiFont()`, una sola vez | Al terminar el programa |

Qué revisar:

- Solo hay un objeto de fuente, compartido por todas las ventanas, así que el número de objetos GDI no debería crecer con cada ventana abierta.
- `Decision::registerClass()` se llama en cada renombrado; desde la segunda vez `RegisterClassExW` falla con `ERROR_CLASS_ALREADY_EXISTS` y la clase existente se reutiliza.
- `entries_.clear()` vacía la lista pero no libera su capacidad: después de una copia grande, los bytes privados pueden quedarse altos sin que haya crecimiento por ciclo.

#### Corrupción de memoria

No hay un bloque único. Los puntos más sensibles son aquellos donde los dos hilos comparten datos:

- [`src/Controller.cpp:216-221`](../../src/Controller.cpp#L216-L221) y [`src/Window.cpp:159-161`](../../src/Window.cpp#L159-L161): el trabajador pasa un puntero a un `std::wstring` de su pila. Solo es válido porque `SendMessageW` espera a que el hilo principal termine de usarlo.
- [`src/Controller.cpp:139-145`](../../src/Controller.cpp#L139-L145), `Controller::onCurrentEntry()`: llega por `PostMessageW` y lee `copier_` en el hilo principal mientras el trabajador sigue copiando.
- [`src/Controller.cpp:110-125`](../../src/Controller.cpp#L110-L125) y [`src/Controller.cpp:166-168`](../../src/Controller.cpp#L166-L168): el hilo principal lee y modifica `copier_` mientras el trabajador espera en `SendMessageW`.
- [`src/Controller.cpp:147-163`](../../src/Controller.cpp#L147-L163) y [`src/Controller.cpp:193-204`](../../src/Controller.cpp#L193-L204): `workerResult_` lo escribe el trabajador y lo lee el hilo principal después de `WaitForSingleObject`.
- [`src/Copier.cpp:334-341`](../../src/Copier.cpp#L334-L341): `CopyFileExW` recibe un puntero a `cancelRequested_` como contexto de `copyProgress()`.

#### Compatibilidad de Windows

- [`Makefile:29-30`](../../Makefile#L29-L30): `_WIN32_WINNT=0x0601`.
- [`resources/fullcopy.manifest`](../../resources/fullcopy.manifest): sin sección `<compatibility>`.
- [`.github/workflows/tests.yml:14`](../../.github/workflows/tests.yml#L14): el CI solo usa `windows-latest`.

```make
CXXFLAGS  := -std=c++17 -O2 -Wall -Wextra -I$(INC_DIR) \
             -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -MMD -MP
```

Qué revisar:

- `_WIN32_WINNT=0x0601` fija Windows 7 como nivel mínimo de la API al compilar, pero no garantiza que el programa funcione en Windows 7: hay que probarlo.
- El manifiesto no declara las versiones de Windows admitidas (`supportedOS`).
- Los errores `ERROR_CLOUD_FILE_*` solo aparecen en versiones con archivos a petición de OneDrive (Windows 10 1709 o posterior).
- El CI solo prueba la versión de `windows-latest`; el resto de la matriz es manual.

## Registro de cada prueba pendiente

Al ejecutar un caso, registrar la revisión y los cambios locales evaluados, versión de Windows, sistema de archivos, preparación y semilla de datos cuando corresponda, pasos, resultado real, tiempos o mediciones relevantes y logs. Una prueba no ejecutada permanece pendiente; una prueba fallida debe conservar su evidencia y repetirse después de corregir la causa.
