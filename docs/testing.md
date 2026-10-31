# Stability validation

Automated checks run on a real Windows GitHub Actions runner. They build the
application with warnings treated as errors and execute filesystem integration
tests against `Copier`. No application behavior is changed by this test suite.

## Automated coverage

- Nested and empty directories, empty files, Unicode names and byte-for-byte binary content.
- File and directory destination paths at the supported boundary; rejection above it.
- Directory rename propagation, case-insensitive collisions and invalid/reserved names.
- Subtree skipping without losing siblings, and clearing state on a fresh scan.
- Missing sources, impossible destinations, empty sources and files disappearing after scanning.
- Immediate deletion of both source and destination while `Copier` remains alive.

To reproduce in a Windows MSYS2 MINGW64 shell with GCC and make installed, run
`make CXX=g++ WINDRES=windres`, then:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -Iinclude tests/copier_tests.cpp src/Copier.cpp src/Common.cpp -o bin/copier_tests.exe -static -lshell32 -lole32 -luuid
./bin/copier_tests.exe
```

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
| UI and decisions | Use Enter, Escape and X; repeatedly rename or skip long-path directories. | Correct subtree handling, responsive UI, accurate summary and no stale dialogs. |
| Input validation | Same source/destination, destination nested in source, blank fields and mixed-case paths. | Invalid operations are rejected before copying. |
| Filesystem errors | Read-only destination, locked source file, denied permissions and exhausted disposable test volume. | Failures are reported without a crash, hang or false successful summary. Never fill the system volume. |
| Integrity and repetition | Copy a tree containing at least 10,000 files and a multi-GB file; compare relative paths and SHA-256 hashes. Repeat 20 times. | All expected files match, skipped items are absent, and the process remains usable. |
| Resource lifetime | Monitor private bytes, handles and GDI objects over 100 Browse/copy/rename cycles, after warming up. | No sustained cycle-by-cycle growth. Capture counts and investigate growth; a single snapshot is insufficient. |
| Long paths and links | Use source paths over 260 characters, a deeply nested tree, and a junction loop in a disposable fixture. | Long names can be shortened/skipped; no recursion loop or crash. Document supported depth. Junctions are copied as empty directories. |

For memory corruption investigation, repeat the UI and stress cases under Windows
Application Verifier and full page heap when available. Save diagnostic logs and
resolve verifier failures before release. These checks complement the automated
suite; they have not been automated here.

## Release gate and known gaps

Require a green Windows build/test job, passing manual checks above, and no
unresolved crash, data corruption, folder-lock or resource-growth reports for the
candidate commit. Passing tests provides evidence, not a proof of stability.

The suite does not drive `Window` or `Decision`, simulate allocation failures,
measure GDI lifetime, test network shares, or validate all Windows versions.
`FindHandle` is currently unused and the application-created font is still not
explicitly released; adding tests does not repair either issue. Record failures
as bugs instead of weakening acceptance criteria to obtain a green result.
