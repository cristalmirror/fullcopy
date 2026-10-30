# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

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
