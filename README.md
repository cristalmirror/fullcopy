# FullCopy

**Version 0.0.1**

FullCopy is a Windows desktop application that copies a directory with all its subdirectories and files.
When a file or directory name is too long to be copied (the destination path goes over the Windows
path limit), a window appears asking for a new, shorter name, and the copy continues.

Written in C++17 with the Win32 API. It is cross-compiled from Linux with MinGW-w64 inside a Fedora container.

## Usage

1. Run `fullcopy.exe` on Windows (no installation or extra DLLs needed).
2. Choose the **Source directory** and the **Destination directory**, typing the paths or with the
   **Browse...** buttons.
3. Press **Start**. The progress bar shows how much of the copy is done.
4. If a name is too long, the **Name too long** window appears:
   - It shows the current name and the maximum number of characters allowed.
   - Write a new name and press **Accept** (or Enter). The copy continues with the new name.
     If the item is a directory, all its content is copied under the new name.
   - Closing the window (X or Escape) skips that item and all its content.
5. When the copy finishes, a summary shows how many items were skipped or failed.
6. Press **Accept** in the main window to close the application.

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
project inside it. The result is `bin/fullcopy.exe`.

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
| `make clean` | Removes the `build/` and `bin/` directories. |
| `make clean && make docker` | Builds everything from scratch. |

`make: Nothing to be done for 'all'.` means the executable is already up to date.

## Project structure

```
include/      Class definitions (.hpp)
src/          Class implementations (.cpp) and main.cpp
resources/    Windows manifest (modern look for the controls)
docs/         Class diagrams
Dockerfile    Build environment (Fedora + MinGW-w64)
Makefile      Build rules
```

The class design is described in [docs/class_diagram.md](docs/class_diagram.md).

## Changelog

See [CHANGELOG.md](CHANGELOG.md).
