# SumatraLister — instructions

Step-by-step guide to installing, configuring and troubleshooting the plugin. For how it
works, what was tested and what wasn't, see `README.md`. Every setting is documented in
detail in `SumatraLister.ini`.

---

## 1. What you need

- **Total Commander** — 64-bit (`TOTALCMD64.EXE`) or 32-bit (`TOTALCMD.EXE`). The plugin must
  match it: **`SumatraLister.wlx64` for 64-bit, `SumatraLister.wlx` for 32-bit.**
- **SumatraPDF** installed (installer or portable). Get it from https://www.sumatrapdfreader.org/.
  Any recent version works; `#search=` links need 3.4 or newer.
- **The plugin file.** No prebuilt binary is included — build it first (section 2).

## 2. Build the plugin (once)

With Visual Studio and CMake, from the folder containing `CMakeLists.txt`:

```
cmake -S . -B build64 -A x64
cmake --build build64 --config Release
```

The result is `build64\Release\SumatraLister.wlx64`. For 32-bit Total Commander use
`-A Win32` (and a separate folder such as `build32`); that produces `SumatraLister.wlx`.
MinGW users: see *Building* in `README.md`.

## 3. Install into Total Commander

1. Copy the `.wlx64` (or `.wlx`) file to a folder you'll keep, for example
   `C:\Tools\Totalcmd\Plugins\SumatraLister\`. **Optionally** copy `SumatraLister.ini` into the
   *same* folder — the plugin looks for its settings and writes its log next to itself.
2. In Total Commander: **Configuration → Options… → Plugins → Lister plugins (WLX) →
   Configure**, then **Add** and select the file.
3. Check that the plugin's detect string lists the formats (`EXT="PDF" | EXT="CHM" | …`).
4. If another plugin already claims some of these extensions, move SumatraLister above it in the
   list — Total Commander uses the first plugin whose detect string matches.
5. Select a PDF, EPUB, CBZ… and press **F3**. You should see "Opening in SumatraPDF…" for a
   moment, then the document. **Ctrl+Q** (Quick View) works too.

> Total Commander keeps plugin DLLs loaded while it runs, so **close it before replacing the
> file** with a newer build.

## 4. Everyday use

- Once the document has focus, Sumatra's own keys apply: scrolling, page keys, zoom, and so on.
- **Find:** Lister's Find (F7) opens Sumatra's own find bar. Ctrl+F inside the document works as well.
- **Copy / Select all** from Lister's menu are forwarded to Sumatra. They work by sending keystrokes,
  so the Lister window must be in the foreground.
- **Next / previous file** in Lister reuses the same pane.
- **Print:** use Lister's Print command (or Ctrl+P inside the document) to get Sumatra's print dialog.
- **File-list thumbnails** come from Windows' own thumbnail handlers, not from this plugin, so what
  you see depends on the handlers installed on your machine. Turn off with `ThumbnailsEnabled=0`.
- If Sumatra stops unexpectedly, the pane says so instead of staying blank. Re-open the file to retry.

## 5. Settings (`SumatraLister.ini`)

Put the file next to the plugin. **Settings are read when the plugin is first used in a Total
Commander session — restart Total Commander after editing.** Only change what you need; blank or
missing keys use the defaults. Some useful recipes (all under `[Settings]` unless shown):

| I want to… | Put this in the INI |
|---|---|
| Follow Total Commander's light/dark theme | `AutoNightMode=1` (needs a Total Commander version that sends the Lister dark-mode notification, TC 10 or newer) |
| Always read in inverted colours | `NightMode=1` |
| Use a portable or non-standard Sumatra | `SumatraPath=D:\Tools\SumatraPDF\SumatraPDF.exe` |
| Open untrusted files more safely | `RestrictMode=1` (Sumatra's restricted mode; it can limit features such as saving, and possibly printing — see Sumatra's documentation) |
| Different defaults for comics | add a section: `[CBZ]` then `DefaultView=continuous` and `DefaultZoom=fit width` (same for `[CBR]` etc.) |
| Handle only some formats | `EnabledExtensions=PDF,EPUB` (see the note below) |
| Give a slow machine more time to embed | `EmbedTimeoutMs=10000` (allowed range 500–30000) |
| Open the file externally if embedding fails | `FallbackToShellOpen=1` |
| Never show thumbnails from this plugin | `ThumbnailsEnabled=0` |
| Make fallback printing silent (scripts) | `InteractivePrintFallback=0` |

**Two rules that trip people up**

- **Quotes.** Windows' INI reader silently removes one pair of quotes wrapping a whole value. That's
  fine for a path, but it corrupts a command like `"C:\Editor\ed.exe" -n%l "%f"`. Wrap such a value in
  *single* quotes: `InverseSearchCommand='"C:\Editor\ed.exe" -n%l "%f"'`.
- **`EnabledExtensions` and Total Commander's stored list.** Total Commander may keep the detect
  string it saw when the plugin was first added. After changing `EnabledExtensions`, restart; if the
  old list still applies, remove and re-add the plugin or edit its detect string in the same
  Lister-plugins dialog. A non-blank value that matches nothing (a typo, or spaces instead of commas)
  enables *nothing*, by design.

## 6. Optional hotkeys (off by default)

Both are registered **system-wide** while a Lister pane is open, so they can take over a combination
another program uses. That's why they are opt-in and remappable.

| Feature | Enable | Default combo | Change the combo |
|---|---|---|---|
| Pop out: reopen the file in a normal, full SumatraPDF window (Save As, annotations, rotation…) | `EnablePopOut=1` | `Ctrl+Alt+O` | `PopOutHotkey=Ctrl+Shift+F9` |
| Diagnostic snapshot to the clipboard (for bug reports) | `EnableDiagnosticHotkey=1` | `Ctrl+Alt+D` | `DiagnosticHotkey=Ctrl+Alt+Shift+D` |

A combo is one or more of `Ctrl`, `Alt`, `Shift`, `Win` plus **one** key: a letter, a digit, or `F1`–`F24`.
At least one modifier is required. An invalid value falls back to the default (noted in the log).
If another program already owns the combination nothing happens — see Troubleshooting.

## 7. LaTeX users

- **Jump from the PDF to your source:** double-click a spot in the PDF. This needs SyncTeX data next
  to the PDF (compile with `-synctex=1`; without it Sumatra says "No synchronization info at this
  position") and an inverse-search command.
- **Best way to set the command: once, inside Sumatra** (press Ctrl+K in a running Sumatra window and
  choose "Set Inverse Search Command Line", or use its Settings/Options, depending on version). That's
  what Sumatra's documentation recommends.
- The plugin can also supply it via `InverseSearchCommand=`, but Sumatra stores that **permanently in
  its own settings file on every launch**, which also affects Sumatra outside this plugin — and
  Sumatra marks the switch as deprecated. Prefer the one-time setup unless you want the plugin to
  manage it.
- If double-clicking does nothing, check Sumatra's `EnableTeXEnhancements` setting.

## 8. Deep links (advanced)

A path handed to the plugin may carry parameters after `#`, combined with `&`:
`C:\book.pdf#page=5&search=hello%20world`, `…#dest=Chapter3`, `…#forwardsearch=C%3A%5Csrc%5Cmain.tex:123`,
`…#pwd=secret`. They only matter when *another tool* (a script, another plugin, a custom command) passes
such a path to the plugin; an ordinary F3 on a file never has one, and I haven't verified a built-in
Total Commander way to type one. Two cautions: `pwd=` places the password on Sumatra's command line,
where other programs of the same user can read it; and `search=` needs Sumatra 3.4+.

## 9. Troubleshooting

**First step: turn on the log.** Set `DebugLog=1`, restart Total Commander, reproduce the problem, then
open `SumatraLister.log` in the plugin's folder (it restarts itself at about 2 MB).

- **The plugin won't load / Total Commander reports it can't load the DLL** — wrong bitness (`.wlx64`
  needs 64-bit Total Commander, `.wlx` needs 32-bit), or a build that depends on a runtime that isn't
  installed. The CMake build is set up to avoid that (static runtime); a hand-built DLL may not be.
- **"SumatraPDF could not be found or started."** — Sumatra isn't installed, or isn't where the plugin
  looks. Set `SumatraPath=`. **The same message also appears when Sumatra *was* found but didn't embed
  within `EmbedTimeoutMs`** — the log tells the two cases apart (look for `Resolved SumatraPDF.exe:` and
  `did not reparent`). Try a larger `EmbedTimeoutMs`, and check that Sumatra starts normally by itself.
  The plugin looks, in order, at `SumatraPath`, the registry, standard install folders, its own folder,
  then folders listed in `PATH` — **never the current directory**, on purpose.
- **"SumatraPDF closed unexpectedly while viewing this file."** — usually a damaged file. Try it in
  Sumatra directly; re-open it to retry.
- **Another viewer still opens these files** — check the plugin order and detect string (section 3);
  another plugin may be matching first.
- **A hotkey does nothing** — another program probably owns that combination. With `DebugLog=1` look for
  `RegisterHotKey … failed`, then pick a different combo.
- **My settings aren't applied** — restart Total Commander; check the file is named exactly
  `SumatraLister.ini` and sits next to the plugin DLL.
- **Sumatra looks different when I use it on its own** — you probably set `BackgroundColor=` or
  `InverseSearchCommand=`; Sumatra saves those permanently (see the INI). Blank them here, then reset the
  value in Sumatra itself (for the colour, the `MainWindowBackground` value in its settings file).

**Reporting a problem:** set `EnableDiagnosticHotkey=1` and `DebugLog=1`, restart, reproduce the problem,
and with a Lister pane open press the diagnostic hotkey (default `Ctrl+Alt+D`). A message confirms the
copy; paste the result into your report. It contains the plugin and Sumatra versions, the detected path, your
effective settings and the recent log, and never a deep link's password or search text.

## 10. Updating and uninstalling

- **Update:** close Total Commander, replace the plugin file (keep your INI), start it again.
- **Uninstall:** remove it from *Lister plugins (WLX)* in Total Commander's options, close Total
  Commander, then delete the plugin folder (the INI and log are in it). If you used
  `BackgroundColor=` or `InverseSearchCommand=`, see the last Troubleshooting item to reset Sumatra.
