# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed

- Junctions and directory symbolic links were copied as empty directories without any warning.
  A reference to the same volume as the source is now copied as a real directory or file.
- OneDrive directories were copied as empty directories, because they are reparse points too.
  They are now scanned and copied like normal directories.

### Added

- `Warnings` window (`include/Warnings.hpp`, `src/Warnings.cpp`) with a text, a list of paths and an
  *Aceptar* button.
- **Referencia Inaccesible** warning after the copy: references to another volume, references that can
  not be followed and OneDrive items that can not be read are listed and not copied.
- **Recursion** warning during the scan: a reference to a directory that contains it is ignored
  instead of being scanned forever.
- Integration tests for junctions: same-volume copy, recursion, a missing target and two junctions
  that point to each other.

### Changed

- `Copier::copy()` returns a `CopyResult` (`Copied`, `Failed`, `Inaccessible`) instead of `bool`.
- `Copier::load()` accepts an optional `RecursionCallback`, and `Copier::getInaccessiblePaths()` lists
  what the scan could not read.
- The copy summary also counts the inaccessible references and the ignored recursive references.

## [1.0.2] - 2026-11-02

### Fixed

- Resolve source and destination directory aliases before checking equality or containment,
  and repeat validation before loading entries. Unresolvable destinations are rejected.
- Reject reserved COM/LPT device names containing superscript digits, including extensions.
- Scan directory trees iteratively while keeping only one search handle open at a time.
- Preserve worker results and poll thread termination so a lost completion notification
  does not leave the interface busy or prevent a pending close.

### Added

- Regression tests for extended path aliases, superscript device names, deep scans,
  cancellation, and completion without the worker notification.

## [1.0.1] - 2026-11-01

### Changed

- The code follows the Model-View-Controller pattern. A new `Controller` class connects the views
  (`Window`, `Decision`) with the model (`Copier`) and runs the copy thread. `main.cpp` creates the
  `Controller`. The behaviour of the program does not change.
- The checks of the source and destination directories moved from the main window to
  `Copier::checkDirectories()`, which returns a `DirectoryResult`.
- `Decision` no longer uses `Copier` directly: it receives a `RenameRequest` and renames through
  `Controller::rename()`.

### Added

- Comments in English for every function, and step by step inside the long ones.
- UML activity diagrams in `docs/flow_diagram.md`.
- Tests for `Copier::checkDirectories()`, and UI tests (`tests/ui_tests.cpp`) that drive the
  `Controller`, the main window and the **Name too long** window. Both run on the Windows CI job.
- `generate_manual_tree`, a command line tool (`make manual-tree`) that creates a source tree of
  10000 files with random long names for the manual checks.

## [1.0.0] - 2026-10-31

### Changed

- First stable release. The executable is now named with its version (`fullcopy-1.0.0.exe`), set by `VERSION` in the `Makefile`.

### Fixed

- Reject rename targets that already exist in the destination and prevent file overwrites during copying.
- Stop copying when a directory cannot be scanned or enumeration fails, discarding partial scan results.
- Reject files used as destination directories, including existing paths encountered during copying.
- Close directory search handles on cancellation, scan errors and exceptions, and release the owned UI font on shutdown.
- Use an atomic cancellation flag during scanning and file copying, removing the current partial file when a copy is cancelled.
- Catch worker exceptions and report an incomplete copy instead of letting them escape the thread entry point.

### Added

- Show the full source path of the item being copied under the progress bar.
- Separate debug builds with symbols, standard library assertions, stack protection and additional compiler warnings (`make debug` and `make docker-debug`).

## [0.0.1] - 2026-10-28

### Added

- Main window (`Window`) with source and destination directory fields, **Browse...** buttons,
  **Start** and **Accept** buttons, progress bar and status text.
- Recursive copy of a directory with all its subdirectories and files (`Copier`), running on a
  separate thread so the window keeps responding.
- Path length check against the Windows limits (259 characters for files, 247 for directories,
  255 for a single name).
- **Name too long** window (`Decision`) to rename an item whose destination path is too long.
  Renaming a directory moves all its content under the new name.
- Validation of new names: empty, invalid characters, reserved Windows names, too long, or
  already used.
- Option to skip an item (and its content) by closing the **Name too long** window.
- Summary at the end of the copy with the skipped and failed items.
- Reading of long source paths with the `\\?\` prefix.
- Windows manifest for the modern look of the controls.
- Build with MinGW-w64 from the latest Fedora (`Dockerfile`) and `Makefile`
  (`make`, `make docker`, `make clean`).
- Class diagram in `docs/class_diagram.md`.
