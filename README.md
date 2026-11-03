# FullCopy

**Version 1.0.2**

FullCopy is a Windows desktop application that copies a directory with all its subdirectories and files.
When a file or directory name is too long to be copied (the destination path goes over the Windows
path limit), a window appears asking for a new, shorter name, and the copy continues.

Written in C++17 with the Win32 API. It is cross-compiled from Linux with MinGW-w64 inside a Fedora container.

## Usage

1. Run `fullcopy-1.0.2.exe` on Windows (no installation or extra DLLs needed).
2. Choose the **Source directory** and the **Destination directory**, typing the paths or with the
   **Browse...** buttons.
3. Press **Start**. The progress bar shows how much of the copy is done.
4. If a name is too long, the **Name too long** window appears:
   - It shows the current name and the maximum number of characters allowed.
   - Write a new name and press **Accept** (or Enter). The copy continues with the new name.
     If the item is a directory, all its content is copied under the new name.
   - Closing the window (X or Escape) skips that item and all its content.
5. If the source contains a junction that points to a directory that contains it, the **Recursion**
   window appears during the scan. Press **Aceptar** to ignore it, and the scan continues.
6. When the copy finishes, the **Referencia Inaccesible** window lists the files and directories that
   could not be read (if there are any), and then a summary shows how many items were skipped,
   failed, inaccessible or ignored.
7. Press **Accept** in the main window to close the application.

Existing destination files are never overwritten. A rename that would use an existing destination
name is rejected; other existing files are reported as copy failures. If any source directory cannot
be scanned, the operation stops before copying files, except for the references and OneDrive
directories described below. Destination directories created while preparing
the operation may remain. Closing the main window requests cancellation during scanning or copying;
the current partially copied file is removed when cancellation is processed.

### Junctions, symbolic links and OneDrive

- A junction or symbolic link whose target is on the **same volume** as the source directory is
  copied as a real directory or file, with all its content.
- A junction or symbolic link whose target is on **another volume**, is missing or can not be read is
  not copied (nothing is created in the destination). It is listed in the **Referencia Inaccesible**
  window when the copy finishes. With a network source, no reference is followed.
- A junction that points to a directory that contains it (for example, `Application Data` inside
  `AppData\Local`) would be scanned forever. The scan shows the **Recursion** window and ignores it.
- OneDrive files and directories are copied like normal ones. Files that are only in the cloud are
  downloaded by OneDrive; the ones it can not provide (no connection, OneDrive not running, ...) are
  listed in the **Referencia Inaccesible** window.

### Limits checked

| Item | Maximum length |
|---|---|
| Full file path | 259 characters |
| Full directory path | 247 characters |
| A single file or directory name | 255 characters |

A new name can not be empty, can not contain `\ / : * ? " < > |`, can not end with a space or a dot,
can not be a name reserved by Windows (`CON`, `NUL`, `COM1`, ...) and can not repeat the name of
another item in the same directory.

The source directory and the destination directory must be different, and the destination can not be
inside the source.

## Building

### With Docker (recommended)

Requirements: `docker` (or `podman`) and `make`.

```sh
make docker
```

This builds the `fullcopy-builder` image (latest Fedora with the MinGW-w64 compiler) and compiles the
project inside it. The result is `bin/fullcopy-1.0.2.exe`.

The version is defined by the `VERSION` variable at the top of the `Makefile`.

To use podman instead of docker:

```sh
make docker CONTAINER=podman
```

### Without Docker

If the MinGW-w64 cross compiler (`x86_64-w64-mingw32-g++`) is installed on the system:

```sh
make
```

### Other targets

| Command | Description |
|---|---|
| `make debug` | Builds `bin/debug/fullcopy-1.0.2.exe` with debug symbols, assertions and stack protection. |
| `make docker-debug` | Builds the debug executable inside Docker. |
| `make manual-tree` | Builds `bin/generate_manual_tree.exe`, which creates the data for the manual tests ([docs/testing.md](docs/testing.md)). |
| `make docker-manual-tree` | Builds that tool inside Docker. |
| `make clean` | Removes the `build/` and `bin/` directories. |
| `make clean && make docker` | Builds everything from scratch. |

`make: Nothing to be done for 'all'.` means the executable is already up to date.

## Testing

The Windows CI job (`.github/workflows/tests.yml`) builds everything with warnings treated as
errors and runs the integration tests of `Copier`, the UI tests of the windows and the manual test
data tool, and checks the junctions it creates. It runs on every push. For `main`, the pull
requests to `main` and the version tags, `.github/workflows/build.yml` runs it first and only
builds and releases the executable when it passes. To run the tests in a Windows MSYS2 shell,
follow [docs/testing.md](docs/testing.md).

For the manual checks, build the test data tool and run it on Windows with a new directory:

```sh
make docker-manual-tree
```

```bat
generate_manual_tree.exe C:\fullcopy-source
```

It creates 10 directories with 1000 files each, where some names are long enough to open the
**Name too long** window, and a `junctions` directory that opens the **Recursion** and
**Referencia Inaccesible** windows. [docs/testing.md](docs/testing.md) lists the manual checks and its options.

## Project structure

```
include/      Class definitions (.hpp)
src/          Class implementations (.cpp) and main.cpp
tests/        Integration and UI tests, and a generator of manual test data
resources/    Windows manifest (modern look for the controls)
docs/         Class diagram, flow diagrams and test plan
Dockerfile    Build environment (Fedora + MinGW-w64)
Makefile      Build rules
```

## Architecture

The code follows the Model-View-Controller (MVC) pattern:

| Layer | Files | Job |
|---|---|---|
| Model | `Copier.hpp`, `Copier.cpp` | Checks the directories, scans the source, checks the path lengths, renames and copies. It does not know about windows. |
| View | `Window.hpp`, `Window.cpp`, `Decision.hpp`, `Decision.cpp`, `Warnings.hpp`, `Warnings.cpp` | Controls, texts and messages for the user. Every user action goes to the `Controller`; the views never call the `Copier`. |
| Controller | `Controller.hpp`, `Controller.cpp` | Reads the input from the views, uses the `Copier` on a separate copy thread, handles cancellation and tells the views what to show. |

`main.cpp` creates only the `Controller`, which creates the `Copier` and the main `Window`.
`Common.hpp` and `Common.cpp` hold the control IDs, the messages between the copy thread and the main
window, and helper functions shared by every class.

- [docs/class_diagram.md](docs/class_diagram.md): the classes and how they relate.
- [docs/flow_diagram.md](docs/flow_diagram.md): UML activity diagrams of a copy, a rename and closing the program.
- [docs/testing.md](docs/testing.md): automated tests and manual checks.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).

## License

FullCopy is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, version 3 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See [LICENSE.md](LICENSE.md) for the full text.
