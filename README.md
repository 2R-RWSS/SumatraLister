# SumatraLister — a Total Commander Lister plugin powered by SumatraPDF

SumatraLister (`SumatraLister.wlx` for 32-bit, `SumatraLister.wlx64` for 64-bit
Total Commander) previews **PDF, CHM, DjVu, EPUB, FB2, MOBI/PRC, XPS/OXPS and comic
archives (CBZ/CBR/CB7/CBT)** inside the Lister (F3) pane by embedding SumatraPDF
itself, using SumatraPDF's own built-in `-plugin <HWND>` embedding mode. You get
Sumatra's real renderer, search, selection and keyboard handling — not a re-implementation.

Package contents: `SumatraLister.cpp` (the plugin, single file), `SumatraLister.def`,
`SumatraLister.ini` (fully commented settings template), `CMakeLists.txt`, this
`README.md`, and `tests/` (the end-to-end verification harness, see *Tests*).

---

## API correctness

An earlier version of this plugin was written largely from memory of the Total
Commander plugin API and of SumatraPDF's command line, and parts of it were simply
wrong. Everything below was found and fixed by checking against the primary sources
(the official [WLX SDK](https://github.com/ghisler/WLX-SDK) and SumatraPDF's official
command-line reference) and, where possible, by actually running the code:

- **`ListSendCommand` was built on constants that don't exist** (`LCS_RESIZE`,
  `LCS_NEXT`, `LCS_BACKGND`, …). The real command set is only `LC_COPY`,
  `LC_NEWPARAMS`, `LC_SELECTALL`, `LC_SETPERCENT`. "Auto night mode" listened for a
  nonexistent constant and was silently inert; it now follows the real
  `LC_NEWPARAMS` + `LCP_DARKMODE`/`LCP_DARKMODENATIVE` flags.
- **`ListPrint[W]`'s `Margins` parameter is `RECT*`, not `RECT` by value** — a genuine
  ABI mismatch (stack corruption under `__stdcall`).
- **`-forward-search` takes the PDF as its own third argument**
  (`-forward-search "<source>" <line> "<pdf>"`), not via the trailing file argument
  that `-page`/`-named-dest` use. An earlier version got this wrong.
- **Two Sumatra options persist globally.** Sumatra's own documentation lists
  `-bg-color` and `-inverse-search` as writing into the user's
  `SumatraPDF-settings.txt` on every launch (and as deprecated). Both are opt-in here
  (off by default) and the INI says so at the point of use. `-invertcolors`, which
  night mode uses, is *not* in that list.

## Features

- **Auto-detection** of SumatraPDF: manual `SumatraPath`, registry App Paths and
  install keys, common install folders, the plugin's own folder, then the directories
  listed in `PATH` (absolute entries only — never the current directory, see *Notes*).
- **`SumatraLister.ini`** (optional, next to the plugin): extra args, `-restrict`
  mode, night mode, default zoom/view, background colour, an inverse-search command,
  an extension allowlist, per-extension overrides (`[CBZ]`, `[PDF]`, …), print
  behaviour, embed timeout, shell-open fallback, hotkeys, debug log. See the file.
- **Deep links** — append parameters after `#` to the path handed to the plugin
  (combine with `&`; values are percent-decoded):
  `page=N`, `dest=NAME` (named destination / TOC entry), `search=TERM`,
  `forwardsearch=SOURCE:LINE` (SyncTeX), `pwd=PASSWORD` (never stored; only read
  from a link). `page`/`dest`/`forwardsearch` are mutually exclusive (precedence:
  forwardsearch > dest > page, logged). A real file whose literal name looks like a
  deep link always wins over the heuristic.
  Example: `C:\book.pdf#page=5&search=hello%20world`
- **The real WLX surface**: `ListLoad[W]`, `ListLoadNext[W]` (reuses the pane),
  `ListCloseWindow`, `ListGetDetectString` (honours `EnabledExtensions`),
  `ListSendCommand` (copy / select-all / scroll-to-0%/100% / dark-mode),
  `ListSearchText[W]` (with `LCS_BACKWARDS`), `ListSearchDialog`, `ListPrint[W]`,
  `ListGetPreviewBitmap[W]`, `ListSetDefaultParams`. (`ListNotificationReceived` is
  deliberately not implemented — the SDK says only owner-drawn plugins need it.)
- **Thumbnails** through the Windows Shell thumbnail pipeline — no Sumatra process per file.
- **Printing**: if the file is open in a live pane, Sumatra's own Ctrl+P dialog is
  used. Otherwise `InteractivePrintFallback=1` (default) shows Sumatra's print dialog
  and returns immediately; `0` prints silently and waits (for scripted use).
- **Auto night mode** (`AutoNightMode=1`) follows Total Commander's Lister theme.
- **Quick View Panel** (Ctrl+Q) focus notification (`ITM_FOCUS`).
- **Crash detection**: if the embedded Sumatra process dies, the pane says so (and,
  with `FallbackToShellOpen=1`, opens the file in its default app) instead of staying
  a dead blank window. Implemented with a thread-pool wait on the process handle.
- **"Opening in SumatraPDF…" indicator** during the (sometimes multi-second) embed
  wait, painted synchronously before the UI thread blocks; dark-aware.
- **Sumatra version detection**: reads `SumatraPDF.exe`'s version resource and logs a
  clear warning when a configured feature needs a newer Sumatra. Currently only
  `-search` (3.4+, confirmed in Sumatra's docs) is checked — other flags' minimum
  versions aren't confirmed, so they're deliberately not guessed. An unreadable version
  is treated as "unknown, give it the benefit of the doubt", never as "too old".
- **Configurable hotkeys** (opt-in, off by default): `EnablePopOut`/`PopOutHotkey`
  (reopen the file in a full SumatraPDF window; default `Ctrl+Alt+O`) and
  `EnableDiagnosticHotkey`/`DiagnosticHotkey` (copy a diagnostic snapshot to the
  clipboard; default `Ctrl+Alt+D`). Format: modifiers (`Ctrl`/`Alt`/`Shift`/`Win`)
  plus one key (`A–Z`, `0–9`, `F1–F24`); at least one modifier is required. An invalid
  combination falls back to the default (logged). The snapshot never contains a deep
  link's `pwd`/`search`, and shows `InverseSearchCommand` only as set/not set.
- **Debug log** (`DebugLog=1`): `SumatraLister.log` next to the plugin, auto-rotating at ~2 MB.

## Verification

What was actually done, and what was not.

**Static analysis (final source):** GCC `-Wall -Wextra -Wshadow -Wconversion
-Wsign-conversion -Wcast-align -Wpedantic -Wnull-dereference -Wformat=2
-Wuseless-cast -Wswitch` — 0 errors, 0 warnings on both x64 and x86. cppcheck
(warning/performance/portability) — no findings. clang-tidy
(bugprone/performance/cert/clang-analyzer/concurrency) — only accepted categories:
`easily-swappable-parameters` (externally mandated WLX signatures and single-call-site
helpers), four standard Win32 `(HBRUSH)(COLOR_WINDOW+1)`/`(WNDPROC)` int-to-pointer
idioms, `LogF`'s deliberate C-variadic, and the `_UNICODE` macro Windows requires.

**Unit-level (179 assertions)** on code extracted *verbatim* from the shipped source
and run under Wine: the hotkey parser (15 valid forms, 20 rejected forms), URL
decoding edge cases, the deep-link parser (including a real file with a literal `#`
in its name), command-line quoting round-tripped through the real
`CommandLineToArgvW` (embedded quotes, trailing backslashes, an injection attempt,
editor commands with their own inner quotes), the forward-search argument shape,
the `EnabledExtensions` parser, and the Sumatra version comparison.

**End-to-end (209 checks, 11 scenarios)** by loading the real built plugin DLL under
Wine on a virtual display, against a fake `SumatraPDF.exe` that mimics `-plugin`
embedding (it creates a cross-process child window of the given HWND and records its
exact argv): embedding and window ownership, resize following, `ITM_FOCUS`
delivery, every `ListSendCommand`/search/print entry point, error returns for
unknown windows, `ListLoadNext` swapping processes in the same pane, clean close
with no orphaned processes, **crash detection (including re-arming after a retry)**,
**embed timeout and cleanup**, **the loading indicator being visibly painted during
the blocking wait** (measured with a positive control), system-wide hotkey
registration/release/fallback, both print modes' exact command lines, the
`EnabledExtensions` filter through the real INI reader, the Sumatra-not-found path,
and `PATH`-only discovery. The exact arguments Sumatra receives were checked
byte-for-byte, including an `InverseSearchCommand` with its own quoting.

**Builds:** real CMake builds with MinGW-w64 for both architectures; the produced
DLLs import only Windows system DLLs (no MinGW runtime), and all 15 functions are exported.

**Not verified — please treat as untested:** behaviour against a *real* SumatraPDF
(the fake follows Sumatra's documented `-plugin`/argument contract, nothing more) and a
*real* Total Commander; anything on a real Windows desktop (the harness runs under
Wine + Xvfb, which has no window manager — e.g. forwarded keystrokes for find/copy
reach the fake but their effect inside real Sumatra is untested); and an **MSVC** build
of this version (none was available; the `/MT` runtime setting in `CMakeLists.txt` is
standard CMake but unexercised here).

**Bugs found by this process, beyond the API errors above:** command-line quoting
that could be broken out of by a crafted value (now verified safe); the diagnostic
hotkey showing a modal message box while holding the pane's lock (it could freeze
every other operation on that pane); a pane left on the error display after a later
successful load; clipboard contents clobbered by find forwarding; silent-print
timeouts reported as success; a cross-process `WNDPROC` subclass that could never
have worked (replaced with `RegisterHotKey`); `CMakeLists.txt` missing libraries and
producing DLLs that depend on `libstdc++-6.dll`; `LNK4070` from a hard-coded
`LIBRARY` name; **every debug-log line truncated to one character under MinGW**
(`%s` for a wide string is a Microsoft-CRT convention — found only by reading the log
a real run produced; fixed by `%ls` everywhere and by avoiding printf for handles);
and a **binary-planting weakness** in the `PATH` fallback (`SearchPathW(nullptr,…)`
also searches the current directory, which in Total Commander is often a folder the
user is browsing — found when a test double in the working directory got launched).

## How it works

SumatraPDF's `-plugin` mode makes it create its window as a *child* of a window you
name. The plugin creates a small host window inside Total Commander's Lister window,
launches `SumatraPDF.exe -plugin <host HWND> …`, waits (`WaitForInputIdle`, then a
short backoff poll) for a child of the host owned by that process, and keeps it sized
to the host. Closing the pane closes Sumatra gracefully (`WM_CLOSE`, then
`TerminateProcess` as a last resort).

## Thread safety

Total Commander can call a Lister plugin from several threads (notably thumbnail
generation alongside the UI thread). Instances are `shared_ptr`s in a map guarded by
one mutex; each instance has its own `opLock` serialising launch/relaunch/close, so a
slow relaunch never blocks other panes. Config, Sumatra detection and version
detection are `std::call_once`. Every `opLock` acquisition was audited to ensure
nothing with an unbounded, user-paced wait (a modal box) runs under it. `DllMain`
does no file I/O, and on process termination it takes no locks and waits on nothing
(other threads may have died holding them).

## Requirements

- Total Commander with Lister plugin support (use the `.wlx64` for 64-bit TC, `.wlx` for 32-bit).
- SumatraPDF installed (any recent version; `-search` deep links need 3.4+).
- Windows 7 or later (`MOD_NOREPEAT`, Shell thumbnail APIs).

## Building

**CMake (MSVC or MinGW)** — produces `SumatraLister.wlx64` / `.wlx` by pointer size:

```
cmake -S . -B build64 -A x64        # MSVC 64-bit  (use -A Win32 for 32-bit)
cmake --build build64 --config Release
```

With MinGW, `-static` is applied so the DLL doesn't need `libstdc++-6.dll`/`libgcc_s_*.dll`
(a plugin that depends on them fails to load unless they sit next to Total Commander).
With MSVC the C runtime is linked statically (`/MT`) so no Visual C++ Redistributable
is needed.

**MinGW-w64 by hand:**

```
x86_64-w64-mingw32-g++ -shared -O2 -std=c++17 -static -static-libgcc -static-libstdc++ \
    SumatraLister.cpp SumatraLister.def \
    -lshlwapi -luser32 -ladvapi32 -lshell32 -lole32 -lgdi32 -lversion \
    -o SumatraLister.wlx64
# 32-bit: i686-w64-mingw32-g++ … -o SumatraLister.wlx
```

`-lversion` is required (no default library list includes it — leaving it out fails the
link). `ole32`/`gdi32` happen to be added by MinGW's driver anyway, but are listed so the
build doesn't depend on that.

**About `warning LNK4070`:** `SumatraLister.def` deliberately has no `LIBRARY`
statement. This one file serves both the `.wlx` and `.wlx64` builds, and a hard-coded
name always mismatches one of them. Total Commander loads the plugin with
`LoadLibrary` + `GetProcAddress` by name, so that metadata never mattered.

## Installing into Total Commander

1. Copy `SumatraLister.wlx64` (and/or `.wlx`) — and optionally `SumatraLister.ini` — to
   a folder of your choice.
2. Total Commander → *Configuration → Options → Plugins → Lister plugins (WLX) →
   Configure* → *Add*, and pick the file. Move it above other handlers for these types.
3. Press F3 on a PDF/EPUB/CBZ/… file.

## Supported extensions

`PDF CHM DJVU EPUB FB2 FB2Z MOBI PRC XPS OXPS CB7 CBR CBT CBZ` — trim with
`EnabledExtensions=` (it can't add formats Sumatra can't open).

## Notes / limitations

- **Quotes in the INI.** Windows' INI reader silently drops one pair of matching quotes
  around a whole value — harmless for a path, but it corrupts a command like
  `"C:\Program Files\Editor\ed.exe" -n%l "%f"`. Wrap such values in *single* quotes
  (`'…'`); the INI file explains and gives examples. Verified against the real reader.
- **`BackgroundColor=` and `InverseSearchCommand=` are persistent and deprecated** in
  Sumatra itself (see *API correctness*). Leave them blank unless you accept that they
  also change Sumatra when you use it outside this plugin.
- **Find / copy / select-all** are forwarded as synthesized keystrokes (Sumatra has no
  message-based remote-control API), so Lister must be the foreground window.
  `LCS_MATCHCASE`/`LCS_WHOLEWORDS` aren't translated (no confirmed Sumatra hotkeys), and
  `LC_SETPERCENT` handles only 0 % and 100 % (no page-count query exists).
- **`ListPrint` ignores custom margins** (Sumatra manages its own; use `PrintSettings=`).
  The interactive print fallback is deliberately fire-and-forget: blocking Total
  Commander's UI thread on a person using a dialog would look like a hang.
- **Hotkeys are system-wide while a pane is open**, not scoped to the pane having focus,
  so they can shadow another program's combination — that's why both are opt-in and
  remappable. If a combination is already taken, registration fails quietly (see the log).
- **Executable discovery never uses the current directory** (see *Bugs found*): only
  registry, standard install folders, the plugin's own folder, and absolute `PATH` entries.
- **Per-extension night mode vs `AutoNightMode`:** `AutoNightMode=1` overrides every
  static night-mode setting, including `[EXT] NightMode=`.

## Tests

`tests/` holds the harness described under *Verification*:

- `fake.cpp` — the fake `SumatraPDF.exe`; `driver.cpp` — loads the plugin DLL and runs a
  named scenario; `run.sh` — builds the scenario INIs and runs all scenarios under
  Wine + Xvfb (Linux); `unit/` — the generator that extracts functions verbatim from
  `SumatraLister.cpp` plus the unit test body.
- See `tests/README.md` for how to build and run them.
