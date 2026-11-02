# FullCopy 1.0.1 — Wine debugging and memory validation

Date: 2026-11-01. Repository: `/home/crisal/fullcopy`.

Result: the existing functional suites and the real executable's GDB scenario pass. Two additional input-validation defects were reproduced. The measured C++ allocation stress test showed no retained-allocation growth or guard corruption. These results do not establish the absence of every memory error or resource leak.

## Build and environment

- Commit: `d99004ab14cb33c72a172e3feedb91085f21b6e5`.
- Executable: `bin/debug/fullcopy-1.0.1.exe`, Windows x64 PE with DWARF debug information.
- Executable SHA-256: `e61b379dc6aae2656c56488494135ea1206a87453e2a22efc9e8183b8f052ccc`.
- Fedora 44 container, Wine 11.20 Staging, Xvfb virtual display, GDB 18.1, Valgrind 3.27.1, Cppcheck 2.22.0.
- Test locale: `C.UTF-8`. The project source was mounted read-only during execution. Temporary harnesses, scripts and logs are in this directory.
- Test builds used debug information, C++17, assertions, stack protection and warnings treated as errors.

## Functional results

| Check | Result | Evidence |
|---|---|---|
| Copier integration suite | 5/5 passed | [copier-utf8.log](logs/copier-utf8.log) |
| Copier suite with Wine heap checks | 5/5 passed | [copier-heap.log](logs/copier-heap.log) |
| UI suite with Wine heap checks | 5/5 passed | [ui-heap.log](logs/ui-heap.log) |
| Real executable under GDB | Passed; application, GDB and driver exit 0 | [gdb-functional.log](logs/gdb-functional.log), [driver](logs/gdb-driver.log) |
| Manual-tree generator smoke test | Created 3 directories / 150 files; refused an existing destination | [creation](logs/generator-create.log), [refusal](logs/generator-existing.log) |

The initial Unicode fixture failure was isolated to the Wine Unix locale: the same Unicode CreateFileW/ofstream probe fails under `C` and succeeds under `C.UTF-8`. FullCopy was not called before that fixture failure. See [C locale control](logs/unicode-probe-C.log), [UTF-8 control](logs/unicode-probe.log) and [locale](logs/locale.log).

## GDB: real executable and repeated actions

WineDbg's GDB proxy ran the original debug executable. GDB loaded its PE/DWARF symbols explicitly. The available Linux GDB has no Windows-specific OS ABI; the session used generic ABI `none`, inspected symbols/arguments and breakpoints, and made no inferior function calls. DLL symbol loading was disabled; application symbols remained available.

The external driver copied a normal file and a file requiring shortening, sent extra Start and Enter commands while the rename dialog was open, submitted one invalid name and one valid name, checked the summary and both file contents, required exactly two destination files, closed the application, and deleted the destination immediately.

| Function / condition | Observed | Expected |
|---|---:|---:|
| `wWinMain` | 1 | 1 |
| `Controller::onStart` | 1 | 1 |
| `Controller::runCopy` | 1 | 1 |
| `Copier::copy` | 2 | 2 |
| `Decision::onAccept` | 2 | 2 (invalid and valid attempts) |
| `__stack_chk_fail` | 0 | 0 |
| `abort` | 0 | 0 |

The target exited normally with code 0. No unintended repeated start was observed in this scenario. This is not a claim that every method in every possible path executes only once. Local variables printed at the entry breakpoint can be uninitialized because their initializing instructions have not run yet.

## Memory measurements and detector controls

A temporary build linked the unchanged application classes and existing UI tests with a replacement C++ `new/delete` tracker. The tracker uses an independent allocation registry and 16-byte guards before and after each tracked allocation, validates guards before deletion and after each cycle, and covers the C++17 allocation/deallocation overloads.

- Warmup: 5 cycles of all 5 UI cases (25 cases).
- Measurement: 20 further cycles (100 cases) in the same process.
- Baseline retained C++ allocations: 2 blocks / 126 bytes.
- Final retained C++ allocations: 2 blocks / 126 bytes.
- Total observed allocations / frees across warmup and measurement: 44,292 / 44,290.
- Allocations / frees during the 20 measured cycles: 35,459 / 35,459.
- Samples above the warmed-up allocation baseline: 0.
- Changed guards: 0.
- All measured functional cases passed.

The two baseline blocks were not traced to their allocation sites. This test evaluates retained-allocation growth after warmup; it does not establish that every baseline allocation is intentional or perform an exit-time allocation census.

Sensitivity controls succeeded: a deliberate one-byte guard overwrite was detected, and a deliberately retained 64-byte allocation increased the live allocation count by one. The controls returned the expected diagnostic code 3. See [stress log](logs/memory-stress.log), [overflow control](logs/positive-overflow.log), [leak control](logs/positive-leak.log), and [result codes](logs/memory-results.tsv).

Wine's own heap checker also detected a deliberate one-byte overwrite after a 16-byte HeapAlloc block: HeapValidate returned FALSE and Wine reported an invalid block tail. The clean control passed. See [native heap overflow control](logs/heap-control-overflow.log) and [clean control](logs/heap-control-clean.log). The functional suites ran with `WINEDEBUG=-all,err+all,warn+heap` and did not report heap corruption.

Limits:

- The C++ tracker does not intercept C `malloc`, all Win32/COM allocations, or internal Wine allocations.
- Guard bytes detect writes into their adjacent region; they do not instrument every read/write or every use-after-free, stack or global access.
- No stack-protector failure at a breakpoint is narrower evidence than complete buffer-overflow instrumentation.
- Wine returned zero for the handle, GDI, USER and PrivateUsage samples. These resource measurements are unavailable/unusable here; zero is not evidence of zero resources or no resource leaks.
- Valgrind failed twice during Wine startup with SIGSEGV in Wine's Unix `ntdll.so`, before executing the PE tests. The second attempt used the direct ELF loader and `WINELOADERNOEXEC=1`. Its leak summaries describe an aborted Wine startup and cannot be attributed to FullCopy. See [first attempt](logs/valgrind-copier.51.log), [second attempt](logs/valgrind-no-preloader-copier.39.log), and [result codes](logs/valgrind-fallback-results.tsv).
- Native Windows Application Verifier / full PageHeap, meaningful handle/GDI lifetime measurements and the project's remaining manual checks were not executed.

## Reproduced defects

1. **High priority: extended path spelling bypasses source/destination safety checks.** `src/Copier.cpp:111` compares path text, and line 117 compares its prefix. For an existing source, using `\\?\` on only the source or destination makes both the same-directory and destination-inside-source checks return `Ok`. Four alias combinations failed while both ordinary-path controls passed. A destination inside the source can therefore pass initial validation. The probe only checked validation and did not copy files for these cases.
2. **Medium priority: incomplete reserved-device name validation.** `src/Copier.cpp:64` omits superscript digits in device names. `rename()` accepted `COM¹` and `LPT²` (`Ok`) instead of `InvalidName`; ASCII `COM1` and `NUL` controls were rejected correctly. Microsoft documents the superscript variants as [reserved Windows names](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file).

Both defects are recorded in [behavior-probe.log](logs/behavior-probe.log). The probe returned 1 for six mismatches, cleaned its fixture successfully, and made no `Copier::copy` calls. Its source is [behavior_probe.cpp](behavior_probe.cpp).

## Read-only review and remaining risks

No confirmed unbalanced C++ allocation, search handle, worker handle, font or COM release was found in the normal paths reviewed. The shared font owner, search guard, Decision ownership and Browse cleanup have corresponding release paths. Clearing the entry vector retains reusable capacity; that alone is not a leak.

The recursive scan at `src/Copier.cpp:348` / line 395 has no explicit depth bound. A sufficiently deep tree can exhaust the stack; this was not dynamically reproduced and is distinct from a buffer overflow. Final PostMessageW results are unchecked (`src/Controller.cpp:238`), leaving an untested failure path if message posting fails. These are review risks, not reproduced failures in this run.

Cppcheck completed and produced warnings requiring interpretation, not a clean analyzer report. Current call sites initialize the aggregate fields before use; its claim that the modal loop condition is always true does not model changes through window-message callbacks. The analyzer output is [cppcheck.log](logs/cppcheck.log).

## Reproduction artifacts

- Functional/heap probes: [phase-one.sh](phase-one.sh).
- GDB session: [phase-gdb.sh](phase-gdb.sh), [commands](gdb-functional.cmd), [external driver](gdb_driver.cpp).
- Memory stress: [phase-memory.sh](phase-memory.sh), [harness](memory_stress.cpp), [tracker](memory_tracker.cpp).
- Container setup: [Dockerfile.diagnostics](Dockerfile.diagnostics), [environment runner](env-run.sh).

An existing diagnostics image and prefix can run a phase with this command (replace the final phase filename):

```sh
docker run --rm --network none \
  -v /home/crisal/fullcopy:/project:ro \
  -v /tmp/fullcopy-wine.1eLXov:/validation:Z \
  -v /tmp/fullcopy-wine.1eLXov/home:/home/fullcopy:Z \
  fullcopy-wine-diagnostics bash /validation/env-run.sh bash /validation/phase-gdb.sh
```

Run phases sequentially when using the same prefix. Artifacts live in `/tmp` and may be removed by the system later. The diagnostics do not certify the application as defect-free; the two reproduced validation failures remain to be corrected.
