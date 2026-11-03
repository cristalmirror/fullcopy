# Stability validation

Automated checks run on a real Windows GitHub Actions runner. They build the
application with warnings treated as errors and execute filesystem integration
tests against `Copier` and UI tests against the `Controller` and its windows. They
also build and run the tool that creates the manual test data. No application
behavior is changed by this test suite.

## Automated coverage

- Nested and empty directories, empty files, Unicode names and byte-for-byte binary content.
- File and directory destination paths at the supported boundary; rejection above it.
- Directory rename propagation, case-insensitive collisions and invalid/reserved names.
- Subtree skipping without losing siblings, and clearing state on a fresh scan.
- Missing sources, impossible destinations, empty sources and files disappearing after scanning.
- Immediate deletion of both source and destination while `Copier` remains alive.
- Directory checks before a copy: empty paths, a missing source, a file as source, the same
  directory with another case, a trailing backslash or forward slashes, a destination inside the
  source, and a sibling that starts with the same name (`source2`).
- Junctions: a junction to the same volume is copied as a real directory with its content; a
  junction to the source itself and two junctions that point to each other are reported as
  recursive and not copied; a junction whose target is gone is reported as inaccessible.

UI tests (`tests/ui_tests.cpp`) run the real `Controller`, `Window` and `Decision` windows on
the main thread, while a driver thread plays the user with window messages only:

- Every rejected start shows its error, keeps the controls enabled and creates nothing.
- A copy with quoted paths finishes with the right summary, status and controls, and the
  destination can be deleted as soon as the program closes.
- The **Name too long** window shows the path length, the maximum length and the current name;
  invalid, too long and already used names show their error and keep it open; a valid name lets
  the copy go on. Start and Enter are ignored during the copy.
- Closing the **Name too long** window skips a directory with all its content, and the summary
  counts it.
- Closing the program while the **Name too long** window is open shows *Cancelling...*, waits for
  the copy thread and closes without a summary.

The test data tool (see [Test data](#test-data)) is built with warnings treated as errors, creates
a small tree with long names, and must refuse a directory that already exists.

To reproduce in a Windows MSYS2 MINGW64 shell with GCC and make installed, run
`make CXX=g++ WINDRES=windres`, then:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -Iinclude tests/copier_tests.cpp src/Copier.cpp src/Common.cpp -o bin/copier_tests.exe -static -lshell32 -lole32 -luuid -lgdi32 -luser32
./bin/copier_tests.exe
g++ -std=c++17 -O2 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -Iinclude tests/ui_tests.cpp src/Controller.cpp src/Window.cpp src/Decision.cpp src/Warnings.cpp src/Copier.cpp src/Common.cpp -o bin/ui_tests.exe -static -lcomctl32 -lshell32 -lole32 -luuid -lgdi32 -luser32
./bin/ui_tests.exe
```

The UI tests open real windows and message boxes for a few seconds. Do not use the mouse or
the keyboard while they run.

Fixtures are isolated under the Windows temporary directory. Cleanup is attempted
after each case. The folder release test checks deletion explicitly; destructor
cleanup alone is not evidence that folders were unlocked.

## Required manual Windows checks

Record the commit, Windows version, filesystem, steps, result and any error for
all cases. Do not mark unexecuted checks as passed.

| Scenario | Procedure | Acceptance criterion |
|---|---|---|
| Original folder lock report | Create a destination through Browse, select it, copy, close FullCopy, then delete it. Repeat after cancelling Browse and without starting a copy. | The process exits and deletion succeeds immediately. If blocked, record the owning process with Resource Monitor. |
| Cancellation | Close during scanning and during a large-file copy; also close with a rename dialog open. | No crash or deadlock. Shutdown may wait for the current synchronous copy/scan; record the delay. Partial output is allowed. A product decision is needed if the delay is unacceptable. |
| UI and decisions | Use Enter, Escape and X; repeatedly rename or skip long-path directories (use the [test data](#test-data) tree). | Correct subtree handling, responsive UI, accurate summary and no stale dialogs. |
| Input validation | Same source/destination, destination nested in source, blank fields and mixed-case paths. | Invalid operations are rejected before copying. |
| Filesystem errors | Read-only destination, locked source file, denied permissions and exhausted disposable test volume. | Failures are reported without a crash, hang or false successful summary. Never fill the system volume. |
| Integrity and repetition | Copy a tree containing at least 10,000 files (the [test data](#test-data) tree) and a multi-GB file; compare relative paths and SHA-256 hashes. Repeat 20 times. | All expected files match, skipped items are absent, and the process remains usable. |
| Resource lifetime | Monitor private bytes, handles and GDI objects over 100 Browse/copy/rename cycles, after warming up. | No sustained cycle-by-cycle growth. Capture counts and investigate growth; a single snapshot is insufficient. |
| Long paths and links | Use source paths over 260 characters (the [test data](#test-data) tree), a deeply nested tree, and a junction loop in a disposable fixture. | Long names can be shortened/skipped; no recursion loop or crash. Document supported depth. A junction to the source volume is copied as a real directory; a junction loop shows the **Recursion** window during the scan; a junction to another volume or to a missing target is listed in the **Referencia Inaccesible** window after the copy and is not created in the destination. |

### Test data

`tests/generate_manual_tree.cpp` is a command line tool that creates a source tree for these
checks: 10 directories with 1000 files each. Some directories and files, chosen at random, have
names of 245 to 255 characters, so their destination paths go over the Windows limits and the
**Name too long** window appears. A single name can not be longer than 255 characters, so it is
the full path that goes over 260 characters. Every file contains a short message of 100
characters at most. The tree is written with the `\\?\` prefix.

Build it with `make docker-manual-tree` (or `make manual-tree` with MinGW-w64), copy
`bin/generate_manual_tree.exe` to the Windows test machine, and run it with a path that does not
exist yet:

```bat
generate_manual_tree.exe C:\fullcopy-source
```

It prints the seed, how many **Name too long** windows to expect, and the command to delete the
tree. Run `generate_manual_tree.exe --help` for the options: `--directories`, `--files`,
`--long-directories`, `--long-files` and `--seed` (the same seed creates the same tree). Write
the seed in the record of each check, so the same tree can be created again.

The tool creates one level of directories with small text files only. Prepare the deeply nested
tree, the junction loop and the multi-GB file by hand.

For memory corruption investigation, repeat the UI and stress cases under Windows
Application Verifier and full page heap when available. Save diagnostic logs and
resolve verifier failures before release. These checks complement the automated
suite; they have not been automated here.

## Release gate and known gaps

Require a green Windows build/test job, passing manual checks above, and no
unresolved crash, data corruption, folder-lock or resource-growth reports for the
candidate commit. Passing tests provides evidence, not a proof of stability.

The UI tests cover part of the *Input validation*, *UI and decisions* and *Cancellation*
manual checks, but not the Browse dialog, the real mouse and keyboard, the layout,
cancellation during a scan or a large file copy, or closing with the X button and Alt+F4.
The suite does not simulate allocation failures, measure GDI lifetime, test network
shares, or validate all Windows versions, so the manual checks above are still required
for a release. Record failures as bugs instead of weakening acceptance criteria to obtain
a green result.
