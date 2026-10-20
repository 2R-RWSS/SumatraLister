// ============================================================================
//  SumatraLister.wlx / SumatraLister.wlx64
//
//  Total Commander "Lister" (WLX) plugin that displays PDF, CHM, DjVu, EPUB,
//  FB2, MOBI, PRC, XPS/OXPS and comic archive (CB7/CBR/CBT/CBZ) files by
//  embedding SumatraPDF.exe directly into the Lister window.
//
//  How it works
//  ------------
//  SumatraPDF has a built-in "plugin" mode (the same mechanism it uses for
//  the Firefox/Chrome browser plugin and the IE ActiveX control):
//
//      SumatraPDF.exe -plugin <decimal HWND> "<path to file>"
//
//  When started this way, Sumatra creates its main window as a CHILD of the
//  HWND you pass in, hides its toolbar/menu, and tracks the size of that
//  parent window. We create a small "host" child window inside the HWND
//  that Total Commander gives us in ListLoad(), and pass *that* window's
//  handle to SumatraPDF. We then forward WM_SIZE so Sumatra's view always
//  fills the Lister pane, and we terminate the Sumatra process when the
//  Lister window is closed.
//
//  Features
//  --------
//   - Auto-detects SumatraPDF (registry / common paths / PATH), with an
//     optional manual override via SumatraLister.ini.
//   - INI-configurable launch options: default zoom, default view mode,
//     page-surround background colour, night/inverted-colour mode,
//     restricted (sandboxed) mode, embed timeout, an EnabledExtensions
//     allowlist, per-[EXT]-section overrides (e.g. different defaults for
//     comics vs. PDFs), a SyncTeX inverse-search command, and free-form
//     extra command-line arguments.
//   - Deep links: append parameters after a "#" in the path passed to
//     ListLoad/ListLoadNext, e.g. "C:\book.pdf#page=5&search=foo". Supports
//     page=N, dest=NAME (named destination / TOC entry), search=TERM,
//     forwardsearch=SOURCE:LINE (SyncTeX/PdfSync), and pwd=PASSWORD
//     (never persisted, only ever read from a link supplied this way). A
//     real file that happens to be named like a deep link on disk always
//     takes precedence over the heuristic. forwardsearch passes the PDF as
//     -forward-search's own third argument, matching Sumatra's documented
//     syntax exactly ("-forward-search <sourcepath> <line> <pdfpath>") --
//     confirmed against the official command-line reference -- rather than
//     relying on the ordinary trailing file argument the other deep links use.
//   - Implements the real ListSendCommand set (LC_COPY, LC_NEWPARAMS,
//     LC_SELECTALL, LC_SETPERCENT), ListSearchText[W] with its real
//     LCS_FINDFIRST/LCS_MATCHCASE/LCS_WHOLEWORDS/LCS_BACKWARDS flags, and
//     ListSearchDialog -- forwarding find/copy/select-all/scroll into
//     Sumatra's own UI. Page navigation, zoom, and general keyboard input
//     reach Sumatra directly once its window has focus and need no explicit
//     forwarding.
//   - File-list thumbnails (ListGetPreviewBitmap[W]) via the Windows Shell
//     thumbnail pipeline -- no extra Sumatra process spawned per thumbnail.
//   - Printing (ListPrint[W]): shows Sumatra's own native print dialog when
//     reusing a live embedded pane for the file being printed. The (less
//     common) fallback case defaults to also showing an interactive print
//     dialog (InteractivePrintFallback=1), returning immediately rather
//     than blocking on user-paced dialog interaction; can be switched to
//     the old silent, wait-for-completion CLI behavior for scripted/
//     automated printing (InteractivePrintFallback=0).
//   - Auto night mode: with AutoNightMode=1, follows Total Commander's own
//     Lister dark-mode state (via the real LC_NEWPARAMS/LCP_DARKMODE
//     mechanism) instead of a fixed setting. Verified that -invertcolors
//     itself (unlike -bg-color/-inverse-search below) does not persist to
//     Sumatra's own settings file, so repeated relaunches as TC's theme
//     changes don't have an unwanted global side effect.
//   - Quick View Panel (Ctrl+Q) focus notification (ITM_FOCUS), so its
//     header highlights correctly when the embedded pane gains focus.
//   - Crash detection: if an embedded Sumatra process exits unexpectedly
//     (crash, killed externally) rather than via this plugin's own
//     intentional close, the pane switches to an accurate message instead
//     of staying a dead, unresponsive blank window. Uses a thread-pool wait
//     on the process handle (RegisterWaitForSingleObject), not polling.
//   - A brief "Opening in SumatraPDF..." indicator while an embed is in
//     progress, instead of a blank pane, for the (occasionally several
//     seconds) wait on a slow machine.
//   - Sumatra version detection: reads the detected SumatraPDF.exe's own
//     file version resource and, where a minimum version is confirmed
//     (currently just -search, which Sumatra's own docs confirm needs
//     3.4+), logs a clear diagnostic warning rather than silently failing
//     when a configured feature won't work on the user's installed version.
//   - Configurable pop-out/diagnostic hotkeys (PopOutHotkey=,
//     DiagnosticHotkey=, e.g. "Ctrl+Alt+O") instead of a fixed combo, so a
//     conflict with another application can be worked around by remapping
//     rather than only by disabling the feature.
//   - Optional pop-out hotkey (EnablePopOut=1, off by default): reopens the
//     current file in a normal, full SumatraPDF window with its
//     menu/toolbar, for actions the embedded view doesn't expose (Save As,
//     annotate, rotate...).
//   - Optional diagnostic-snapshot hotkey (EnableDiagnosticHotkey=1, off by
//     default): copies plugin version, detected Sumatra path/version,
//     effective config, and a recent log tail to the clipboard for bug
//     reports. Never includes a deep link's pwd/search value.
//   - Optional fallback (FallbackToShellOpen=1): if Sumatra can't be found
//     or embedded, opens the file with its default Windows handler instead
//     of just showing an error message.
//   - Reuses one embedded Sumatra process across ListLoadNext (Lister's
//     "next file" navigation) instead of recreating the host window.
//   - Optional debug log (SumatraLister.log next to the DLL, auto-rotated
//     past ~2MB) for troubleshooting detection/launch problems.
//
//  API correctness
//  ----------------
//  The WLX constants and struct layouts used in this file (LC_*, LCP_*,
//  LCS_*, ITM_FOCUS, ListDefaultParamStruct, and every exported function's
//  exact signature including ListPrint's RECT* Margins) have been verified
//  against the official Total Commander Lister Plugin SDK
//  (https://github.com/ghisler/WLX-SDK). Likewise, every SumatraPDF
//  command-line flag used here (-plugin, -page, -named-dest, -search,
//  -forward-search, -inverse-search, -pwd, -bg-color, -invertcolors,
//  -restrict, -zoom, -view, -print-to[-default], -print-dialog,
//  -exit-when-done, -silent, -print-settings) has been checked against
//  Sumatra's own official command-line reference rather than assumed from
//  memory. Two of those (-bg-color, -inverse-search) are documented by
//  Sumatra itself as deprecated AND as permanently writing into the user's
//  SumatraPDF-settings.txt on every launch, not just affecting the current
//  session -- both are opt-in INI settings here, off by default, with that
//  behavior disclosed at the point of use. See README.md's "API
//  correctness" section for the fuller account of what an earlier,
//  unverified version of this file got wrong (a fabricated ListSendCommand
//  implementation, a real ABI mismatch in ListPrint, and an incorrect
//  -forward-search argument shape) and how each was found and corrected.
//
//  Thread safety
//  -------------
//  Total Commander can call into a Lister plugin from more than one thread
//  at once -- most notably, ListGetPreviewBitmap for background thumbnail
//  generation runs concurrently with the UI thread's ListLoad/ListSendCommand
//  calls for an open Lister window. Instances are held as shared_ptr (so an
//  object always outlives every thread still using it, even if
//  ListCloseWindow removes it from the map mid-operation on another thread)
//  and each instance has its own opLock, separate from the map's g_mutex, so
//  a slow relaunch on one Lister window never blocks operations on another.
//  Every opLock acquisition in this file has been individually audited to
//  confirm nothing with an unbounded, user-paced wait (e.g. a modal message
//  box) ever runs while that lock is held.
//
//  Build: see README.md in this package for MSVC / MinGW instructions.
//  Target: Win32 (SumatraLister.wlx) and x64 (SumatraLister.wlx64).
// ============================================================================

#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winver.h>      // VS_FIXEDFILEINFO / GetFileVersionInfoW (SumatraPDF version detection); explicit rather than
                         // relying on windows.h pulling it in under WIN32_LEAN_AND_MEAN
#include <shlwapi.h>
#include <shlobj.h>      // SHCreateItemFromParsingName / IShellItemImageFactory (thumbnails)
#include <shellapi.h>    // ShellExecuteW (FallbackToShellOpen)
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <algorithm>     // std::min / std::max / std::clamp
#include <cstdint>       // std::uint8_t
#include <utility>       // std::pair
#include <cwctype>       // iswxdigit, towupper
#include <cstdio>
#include <cstdarg>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "version.lib")

// ---------------------------------------------------------------------------
//  WLX API constants -- verified against the official Total Commander
//  Lister Plugin SDK (https://github.com/ghisler/WLX-SDK,
//  https://ghisler.github.io/WLX-SDK/listplug.h.htm).
// ---------------------------------------------------------------------------

// ListSendCommand commands (TC calls this "when the user changes some
// options in Lister's menu" -- these four are the complete, real set).
#define LC_COPY       1  // copy current selection to the clipboard
#define LC_NEWPARAMS  2  // display options changed; Parameter is a combination of LCP_* flags below
#define LC_SELECTALL  3  // select the whole contents
#define LC_SETPERCENT 4  // scroll to a new position in the document, as a percent (Parameter = 0-100)

// LC_NEWPARAMS flags (the Parameter passed alongside LC_NEWPARAMS).
// This plugin only acts on LCP_DARKMODE/LCP_DARKMODENATIVE (for
// AutoNightMode); the others describe TC's own built-in text/hex/image
// viewer options, which don't apply to an embedded SumatraPDF view, and
// per the SDK docs "you may ignore these parameters if they don't apply
// to your document type." Defined here for completeness/documentation,
// not because this plugin's own logic reads them.
#define LCP_WRAPTEXT       1
#define LCP_FITTOWINDOW    2
#define LCP_ANSI           4
#define LCP_ASCII          8
#define LCP_VARIABLE       12
#define LCP_FORCESHOW      16
#define LCP_FITLARGERONLY  32
#define LCP_CENTER         64
#define LCP_DARKMODE       128  // added in WLX SDK 2.12 / TC 10.0 (2022)
#define LCP_DARKMODENATIVE 256  // added in WLX SDK 2.12 / TC 10.0 (2022)

// ListSearchText / ListSearchTextW SearchParameter flags.
#define LCS_FINDFIRST  1  // fresh search from the top; unset means "find next" from current position
#define LCS_MATCHCASE  2  // case-sensitive search (not currently translated -- see README)
#define LCS_WHOLEWORDS 4  // whole-words-only search (not currently translated -- see README)
#define LCS_BACKWARDS  8  // search backwards (upward) through the document

// WM_COMMAND value the PLUGIN sends TO Total Commander's Lister/Quick-View
// parent window (not the reverse): WM_COMMAND(MAKEWPARAM(0, ITM_FOCUS),
// (LPARAM)pluginWindow), notifying the Quick View Panel (Ctrl+Q) that this
// pane gained focus, so its header highlights correctly.
#define ITM_FOCUS 0xFFF8

// Custom message (private to this plugin, not part of the WLX API): posted
// to a host window by ProcessExitCallback when its embedded Sumatra process
// exits. See RegisterCrashDetection.
#define WM_SUMATRA_PROCESS_EXITED (WM_APP + 1)

// GWLP_USERDATA values for ErrorWndProc, set alongside the WNDPROC swap at
// each call site that switches a pane into its error/fallback display.
#define ERRSTATE_NOT_FOUND        0  // Sumatra couldn't be found/started at all
#define ERRSTATE_OPENED_EXTERNAL  1  // FallbackToShellOpen kicked in instead
#define ERRSTATE_CRASHED          2  // was embedded and working, then the process exited unexpectedly

// return codes
#define LISTPLUGIN_OK       0
#define LISTPLUGIN_ERROR    1

// Struct TC passes to ListSetDefaultParams, verified against the official
// SDK. DefaultIniName is always narrow (char[]), even in the Unicode/
// W-suffixed build.
struct ListDefaultParamStruct {
    int   size;
    DWORD PluginInterfaceVersionLow;
    DWORD PluginInterfaceVersionHi;
    char  DefaultIniName[MAX_PATH];
};

// Plugin's own version, independent of SumatraPDF's version. Bump when
// making a user-visible change; surfaced in the diagnostic snapshot
// (Ctrl+Alt+D, if EnableDiagnosticHotkey=1) to make bug reports easier to
// place in context.
#define PLUGIN_VERSION L"1.1"

// Canonical list of extensions this plugin knows how to handle (matches
// what SumatraPDF itself can open). Used to build ListGetDetectString's
// detection string (filtered by EnabledExtensions if set) and to look for
// per-extension INI override sections ([PDF], [CBZ], etc.) in
// LoadConfigImpl -- kept as one shared list so those two things can't
// silently drift out of sync with each other.
static const wchar_t* const kSupportedExtensions[] = {
    L"PDF", L"CHM", L"DJVU", L"EPUB", L"FB2", L"FB2Z", L"MOBI", L"PRC",
    L"XPS", L"OXPS", L"CB7", L"CBR", L"CBT", L"CBZ"
};

// ---------------------------------------------------------------------------
//  Config (SumatraLister.ini, same folder as the DLL)
// ---------------------------------------------------------------------------
//
//  [Settings]
//  SumatraPath=C:\Tools\SumatraPDF\SumatraPDF.exe   ; manual override, optional
//  ExtraArgs=-esc-to-exit                            ; appended verbatim
//  RestrictMode=0                                    ; 1 = launch with -restrict
//  NightMode=0                                       ; 1 = launch with -invertcolors
//  AutoNightMode=0                                   ; 1 = follow TC's Lister light/dark theme instead
//  DefaultZoom=                                      ; e.g. "fit page", "100", "fit width"
//  DefaultView=                                      ; e.g. "continuous", "facing", "book view"
//  BackgroundColor=                                  ; hex RGB, e.g. "2B2B2B"; forwarded to -bg-color
//                                                     ;     (the page-surround color, distinct from
//                                                     ;     NightMode/-invertcolors' page/text inversion).
//                                                     ;     CAUTION: -bg-color is deprecated and, per
//                                                     ;     Sumatra's own docs, permanently writes to
//                                                     ;     SumatraPDF-settings.txt on every launch, not
//                                                     ;     just the current session -- see the fuller
//                                                     ;     explanation in SumatraLister.ini.
//  InverseSearchCommand=                             ; SyncTeX/PdfSync inverse-search command line,
//                                                     ;     forwarded to -inverse-search, e.g.
//                                                     ;     "code.exe -g \"%f\":%l" -- lets Ctrl+clicking
//                                                     ;     a location in the embedded PDF jump back to
//                                                     ;     the LaTeX source in your editor. CAUTION: same
//                                                     ;     persistent-settings-file caveat as
//                                                     ;     BackgroundColor= above applies here too.
//  EnabledExtensions=                                ; comma-separated allowlist, e.g. "PDF,EPUB,CBZ";
//                                                     ;     empty (default) = every supported extension
//                                                     ;     stays enabled. Trims what ListGetDetectString
//                                                     ;     advertises to TC without recompiling.
//  PrintSettings=                                    ; forwarded to -print-settings for ListPrint
//  InteractivePrintFallback=1                        ; see README/ini for the full explanation
//  ThumbnailsEnabled=1                               ; 1 = supply file-list thumbnails via Shell API
//  EmbedTimeoutMs=4000                               ; how long to wait for Sumatra to embed before giving up
//  FallbackToShellOpen=0                             ; 1 = open the file with its default Windows app if
//                                                     ;     Sumatra can't be found/embedded, instead of just
//                                                     ;     showing an error message in the Lister pane
//  EnablePopOut=0                                     ; 1 = register a system-wide hotkey (PopOutHotkey=,
//                                                     ;     default Ctrl+Alt+O), for as long as a Lister
//                                                     ;     pane using this plugin is open, to reopen the
//                                                     ;     current file in a normal, full SumatraPDF
//                                                     ;     window (menu/toolbar included). OFF by default
//                                                     ;     since the hotkey isn't scoped to just this pane
//                                                     ;     having focus -- see README for details.
//  PopOutHotkey=Ctrl+Alt+O                           ; combo for EnablePopOut=1, e.g. "Ctrl+Alt+Shift+P"
//  EnableDiagnosticHotkey=0                           ; 1 = register a system-wide hotkey (DiagnosticHotkey=,
//                                                     ;     default Ctrl+Alt+D; same scoping caveat as
//                                                     ;     EnablePopOut above) that copies a diagnostic
//                                                     ;     snapshot to the clipboard for bug reports.
//  DiagnosticHotkey=Ctrl+Alt+D                       ; combo for EnableDiagnosticHotkey=1
//  DebugLog=0                                        ; 1 = write SumatraLister.log next to DLL
//
//  Per-extension overrides: an optional [EXT] section (e.g. [CBZ], [EPUB])
//  overrides DefaultZoom/DefaultView/NightMode from [Settings] for files of
//  that extension only. Any key not present in the [EXT] section falls
//  back to the [Settings] value. Example:
//    [CBZ]
//    DefaultView=continuous
//    DefaultZoom=fit width
//
struct PerExtensionOverrides {
    std::wstring defaultZoom;    // empty = not overridden, use [Settings]
    std::wstring defaultView;    // empty = not overridden, use [Settings]
    int          nightMode = -1; // -1 = not overridden, 0 = off, 1 = on
};

struct PluginConfig {
    std::wstring sumatraPathOverride;
    std::wstring extraArgs;
    bool         restrictMode            = false;
    bool         nightMode               = false;
    bool         autoNightMode           = false;
    std::wstring defaultZoom;
    std::wstring defaultView;
    std::wstring backgroundColor;
    std::wstring inverseSearchCommand;
    std::wstring enabledExtensions;  // empty = all; else comma-separated allowlist
    std::wstring printSettings;
    bool         interactivePrintFallback = true;
    bool         thumbnailsEnabled       = true;
    DWORD        embedTimeoutMs          = 4000;
    bool         fallbackShellOpen       = false;
    bool         enablePopOut            = false;
    std::wstring popOutHotkeyStr         = L"Ctrl+Alt+O";
    bool         enableDiagnosticHotkey  = false;
    std::wstring diagnosticHotkeyStr     = L"Ctrl+Alt+D";
    bool         debugLog                = false;
    std::map<std::wstring, PerExtensionOverrides> perExtension; // key: uppercase extension, no dot
};

static PluginConfig   g_config;
static std::once_flag g_configOnceFlag; // guards lazy config load against concurrent threads
                                          // (Total Commander can call into this plugin from a
                                          // background thumbnail-generation thread at the same
                                          // time the UI thread is opening a Lister window)
static HINSTANCE      g_hInst = nullptr;

static std::wstring GetModuleDir()
{
    wchar_t modulePath[MAX_PATH] = {0};
    GetModuleFileNameW(g_hInst, modulePath, MAX_PATH);
    PathRemoveFileSpecW(modulePath);
    return modulePath;
}

static std::wstring GetIniPath()
{
    return GetModuleDir() + L"\\SumatraLister.ini";
}

static std::wstring GetLogPath()
{
    // Computed once (thread-safe static local init) rather than re-deriving
    // the module's own path via GetModuleFileNameW on every LogF call --
    // the DLL's location can't change during its own lifetime, so repeating
    // that lookup per log line was pure avoidable overhead on a path that
    // can be called many times during an active troubleshooting session.
    static const std::wstring path = GetModuleDir() + L"\\SumatraLister.log";
    return path;
}

// Lightweight debug logger; no-op unless DebugLog=1 in the INI. Guarded by
// its own mutex (separate from g_mutex/opLock) since log calls can now come
// from a background thumbnail thread interleaved with the UI thread; without
// this, concurrent appends could interleave mid-line in the file.
static std::mutex g_logMutex;

static void LogF(const wchar_t* fmt, ...)
{
    if (!g_config.debugLog) return;

    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(g_logMutex);

    std::wstring path = GetLogPath();

    // Simple rotation: once the log passes ~2MB, start fresh rather than
    // growing forever across a long-running Total Commander session.
    // Size is tracked in-memory after an initial seed rather than
    // re-stat'ing the file via GetFileAttributesExW on every call: every
    // byte ever written to this log goes through this same function under
    // this same mutex, so once we know the starting size (from one real
    // stat, lazily done on the first log call of this DLL's lifetime), we
    // can just keep a running total ourselves instead of asking the
    // filesystem again each time.
    static bool      sizeSeeded      = false;
    static ULONGLONG approxSizeBytes = 0;

    if (!sizeSeeded) {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
            approxSizeBytes = (((ULONGLONG)fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        sizeSeeded = true;
    }

    if (approxSizeBytes > 2ull * 1024 * 1024) {
        DeleteFileW(path.c_str());
        approxSizeBytes = 0;
    }

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"a, ccs=UTF-8") == 0 && f) {
        SYSTEMTIME st; GetLocalTime(&st);
        int written = fwprintf(f, L"[%02d:%02d:%02d] %ls\n", st.wHour, st.wMinute, st.wSecond, buf);
        if (written > 0)
            approxSizeBytes += (ULONGLONG)written * sizeof(wchar_t); // approximate; exact byte count isn't worth another stat
        (void)fclose(f); // best-effort log write; nothing actionable to do if the close itself fails here
    }
}

// Reads roughly the last maxLines lines from the debug log, for the
// diagnostic snapshot feature (Ctrl+Alt+D). Avoids loading the whole
// (possibly ~2MB) file into memory by seeking near the end and reading a
// bounded trailing chunk instead. This content is essentially always plain
// ASCII in practice, so a possible mid-character landing right at the seek
// point is treated as an acceptable, cosmetic-at-worst imperfection rather
// than something worth complex boundary-aware seeking to fully rule out.
static std::wstring ReadLogTail(int maxLines)
{
    std::wstring path = GetLogPath();
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") != 0 || !f)
        return L"(log file not found -- enable DebugLog=1, reopen the file, and try again)";

    (void)fseek(f, 0, SEEK_END); // best-effort seek; a failure here just means we read from wherever we already were
    long size = ftell(f);
    const long kMaxTailBytes = 16 * 1024;
    long start = (size > kMaxTailBytes) ? size - kMaxTailBytes : 0;
    (void)fseek(f, start, SEEK_SET); // same: best-effort, falls back to reading from the current position on failure

    std::wstring text;
    wint_t ch;
    while ((ch = fgetwc(f)) != WEOF)
        text += (wchar_t)ch;
    (void)fclose(f); // best-effort diagnostic read; nothing actionable to do if the close itself fails here

    std::vector<std::wstring> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find(L'\n', pos);
        lines.push_back(text.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos));
        if (nl == std::wstring::npos) break;
        pos = nl + 1;
    }

    size_t firstToKeep = (lines.size() > (size_t)maxLines) ? lines.size() - (size_t)maxLines : 0;
    std::wstring result;
    for (size_t i = firstToKeep; i < lines.size(); i++) {
        if (lines[i].empty()) continue;
        result += lines[i];
        result += L"\r\n";
    }
    return result.empty() ? L"(log is empty)" : result;
}

static void LoadConfigImpl()
{
    std::wstring ini = GetIniPath();
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES)
        return; // no INI present -> defaults apply, that's fine

    wchar_t buf[1024];

    GetPrivateProfileStringW(L"Settings", L"SumatraPath", L"", buf, _countof(buf), ini.c_str());
    g_config.sumatraPathOverride = buf;

    GetPrivateProfileStringW(L"Settings", L"ExtraArgs", L"", buf, _countof(buf), ini.c_str());
    g_config.extraArgs = buf;

    g_config.restrictMode      = GetPrivateProfileIntW(L"Settings", L"RestrictMode", 0, ini.c_str()) != 0;
    g_config.nightMode         = GetPrivateProfileIntW(L"Settings", L"NightMode", 0, ini.c_str()) != 0;
    g_config.autoNightMode     = GetPrivateProfileIntW(L"Settings", L"AutoNightMode", 0, ini.c_str()) != 0;
    g_config.thumbnailsEnabled = GetPrivateProfileIntW(L"Settings", L"ThumbnailsEnabled", 1, ini.c_str()) != 0;
    g_config.fallbackShellOpen = GetPrivateProfileIntW(L"Settings", L"FallbackToShellOpen", 0, ini.c_str()) != 0;
    g_config.enablePopOut      = GetPrivateProfileIntW(L"Settings", L"EnablePopOut", 0, ini.c_str()) != 0;
    g_config.enableDiagnosticHotkey = GetPrivateProfileIntW(L"Settings", L"EnableDiagnosticHotkey", 0, ini.c_str()) != 0;
    g_config.debugLog          = GetPrivateProfileIntW(L"Settings", L"DebugLog", 0, ini.c_str()) != 0;
    g_config.interactivePrintFallback = GetPrivateProfileIntW(L"Settings", L"InteractivePrintFallback", 1, ini.c_str()) != 0;

    UINT timeoutRaw = GetPrivateProfileIntW(L"Settings", L"EmbedTimeoutMs", 4000, ini.c_str());
    // Clamped entirely in unsigned space (matching GetPrivateProfileIntW's
    // actual UINT return type) rather than round-tripping through a signed
    // int first: a malformed/negative INI value can make GetPrivateProfileInt
    // return a huge UINT (e.g. "-1" -> UINT_MAX), and converting that to int
    // before clamping would rely on implementation-defined signed/unsigned
    // conversion behavior instead of just clamping the unsigned value directly.
    g_config.embedTimeoutMs = std::clamp(timeoutRaw, 500u, 30000u);

    GetPrivateProfileStringW(L"Settings", L"DefaultZoom", L"", buf, _countof(buf), ini.c_str());
    g_config.defaultZoom = buf;

    GetPrivateProfileStringW(L"Settings", L"DefaultView", L"", buf, _countof(buf), ini.c_str());
    g_config.defaultView = buf;

    GetPrivateProfileStringW(L"Settings", L"BackgroundColor", L"", buf, _countof(buf), ini.c_str());
    g_config.backgroundColor = buf;

    GetPrivateProfileStringW(L"Settings", L"InverseSearchCommand", L"", buf, _countof(buf), ini.c_str());
    g_config.inverseSearchCommand = buf;

    GetPrivateProfileStringW(L"Settings", L"EnabledExtensions", L"", buf, _countof(buf), ini.c_str());
    g_config.enabledExtensions = buf;

    GetPrivateProfileStringW(L"Settings", L"PrintSettings", L"", buf, _countof(buf), ini.c_str());
    g_config.printSettings = buf;

    GetPrivateProfileStringW(L"Settings", L"PopOutHotkey", L"Ctrl+Alt+O", buf, _countof(buf), ini.c_str());
    g_config.popOutHotkeyStr = buf;

    GetPrivateProfileStringW(L"Settings", L"DiagnosticHotkey", L"Ctrl+Alt+D", buf, _countof(buf), ini.c_str());
    g_config.diagnosticHotkeyStr = buf;

    // Per-extension [EXT] override sections. A nonexistent section reads
    // back identically to an empty key, so there's no need to separately
    // check for the section's existence -- just try each expected key.
    g_config.perExtension.clear();
    for (const wchar_t* ext : kSupportedExtensions) {
        PerExtensionOverrides ov;
        bool any = false;

        GetPrivateProfileStringW(ext, L"DefaultZoom", L"", buf, _countof(buf), ini.c_str());
        if (buf[0]) { ov.defaultZoom = buf; any = true; }

        GetPrivateProfileStringW(ext, L"DefaultView", L"", buf, _countof(buf), ini.c_str());
        if (buf[0]) { ov.defaultView = buf; any = true; }

        GetPrivateProfileStringW(ext, L"NightMode", L"", buf, _countof(buf), ini.c_str());
        if (buf[0]) { ov.nightMode = (_wtoi(buf) != 0) ? 1 : 0; any = true; }

        if (any)
            g_config.perExtension[ext] = ov;
    }

    LogF(L"Config loaded: override='%ls' extra='%ls' restrict=%d night=%d auto-night=%d "
         L"zoom='%ls' view='%ls' bgcolor='%ls' inversesearch-set=%d enabled-ext='%ls' "
         L"timeout=%lu fallback=%d per-ext-overrides=%lu",
         g_config.sumatraPathOverride.c_str(), g_config.extraArgs.c_str(),
         g_config.restrictMode, g_config.nightMode, g_config.autoNightMode,
         g_config.defaultZoom.c_str(), g_config.defaultView.c_str(),
         g_config.backgroundColor.c_str(), !g_config.inverseSearchCommand.empty(),
         g_config.enabledExtensions.c_str(),
         g_config.embedTimeoutMs, g_config.fallbackShellOpen, (unsigned long)g_config.perExtension.size());
}

// Thread-safe lazy config load: exactly one caller (whichever thread gets
// here first -- UI thread on first ListLoad, or a thumbnail worker thread)
// actually runs LoadConfigImpl(); everyone else blocks briefly until it's
// done, then proceeds with a fully-populated g_config.
static void LoadConfig()
{
    std::call_once(g_configOnceFlag, LoadConfigImpl);
}

// Returns path's extension with no leading dot, uppercased (e.g. "PDF"), or
// an empty string if it has none. Used for per-extension INI overrides and
// EnabledExtensions filtering.
static std::wstring GetExtensionUpper(const std::wstring& path)
{
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    if (!ext || !*ext)
        return std::wstring();
    std::wstring result = ext + 1; // skip the leading '.'
    for (wchar_t& c : result)
        c = (wchar_t)towupper(c);
    return result;
}

static const PerExtensionOverrides* FindExtensionOverrides(const std::wstring& filePath)
{
    std::wstring ext = GetExtensionUpper(filePath);
    if (ext.empty())
        return nullptr;
    auto it = g_config.perExtension.find(ext);
    return (it != g_config.perExtension.end()) ? &it->second : nullptr;
}

// Effective zoom/view given an already-looked-up override (or nullptr if
// none applies): the override's value if it set one, else the [Settings]
// default.
static std::wstring GetEffectiveZoom(const PerExtensionOverrides* ov)
{
    return (ov && !ov->defaultZoom.empty()) ? ov->defaultZoom : g_config.defaultZoom;
}
static std::wstring GetEffectiveView(const PerExtensionOverrides* ov)
{
    return (ov && !ov->defaultView.empty()) ? ov->defaultView : g_config.defaultView;
}

// Effective static night-mode (-invertcolors) for filePath, honoring the
// documented precedence: AutoNightMode, when on, overrides every static
// setting (global or per-extension). When AutoNightMode is off, a
// per-extension override wins over the global NightMode= default.
static bool GetEffectiveStaticNightMode(const std::wstring& filePath)
{
    if (g_config.autoNightMode)
        return false; // AutoNightMode decides later via LC_NEWPARAMS, not here
    const PerExtensionOverrides* ov = FindExtensionOverrides(filePath);
    if (ov && ov->nightMode >= 0)
        return ov->nightMode != 0;
    return g_config.nightMode;
}

// Checks `ext` (no dot, e.g. L"PDF") against the comma-separated
// EnabledExtensions allowlist. An empty allowlist (the default) means
// everything is enabled. Comparison is case-insensitive; entries may have
// surrounding whitespace.
static bool IsExtensionEnabled(const wchar_t* ext)
{
    if (g_config.enabledExtensions.empty())
        return true;

    const std::wstring& list = g_config.enabledExtensions;
    size_t start = 0;
    while (start <= list.size()) {
        size_t comma = list.find(L',', start);
        std::wstring token = list.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);

        size_t first = token.find_first_not_of(L" \t");
        size_t last  = token.find_last_not_of(L" \t");
        if (first != std::wstring::npos) {
            token = token.substr(first, last - first + 1);
            if (_wcsicmp(token.c_str(), ext) == 0)
                return true;
        }

        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  Internal state: one record per open Lister instance (per host window)
// ---------------------------------------------------------------------------

struct ListerInstance {
    HWND               hostWnd     = nullptr;  // child window we create inside TC's ParentWin
    HWND               parentWin   = nullptr;  // window handle TC gave us
    HWND               sumatraWnd  = nullptr;  // top-level Sumatra window (child of hostWnd)
    PROCESS_INFORMATION pi         = {};
    std::wstring       filePath;
    bool               currentInvert = false;  // effective -invertcolors state of the running process
    bool               closed        = false;  // true once TeardownInstance has run; guards reuse-after-close
    int                popOutHotkeyId = 0;     // RegisterHotKey id for pop-out, 0 = not registered
    int                diagHotkeyId   = 0;     // RegisterHotKey id for diagnostic snapshot, 0 = not registered
    HANDLE             processWaitHandle = nullptr; // RegisterWaitForSingleObject handle; see ProcessExitCallback
    std::mutex         opLock;                 // serializes launch/relaunch/close for *this* instance,
                                                // held independently of g_mutex (which only protects the
                                                // map itself) so a slow relaunch on one Lister window never
                                                // blocks operations on other windows or thumbnail requests
};

// Instances are kept alive via shared_ptr, not raw pointer. TC can call into
// this plugin from more than one thread (the documented case is background
// thumbnail generation for ListGetPreviewBitmap, but ListSendCommand /
// ListCloseWindow could in principle race a relaunch too) -- with a raw
// pointer, a thread that looked up `inst` from the map could end up calling
// inst->opLock.lock() on memory that ListCloseWindow already deleted on
// another thread. Handing out shared_ptr copies under g_mutex means the
// object stays alive for as long as *any* thread is still using it, even
// after ListCloseWindow erases the map entry; opLock still serializes the
// actual teardown vs. in-flight operations so nothing operates on a
// half-closed instance.
using ListerInstancePtr = std::shared_ptr<ListerInstance>;

static std::mutex                            g_mutex;
static std::map<HWND, ListerInstancePtr>     g_instances; // keyed by hostWnd (== "ListWin" we return)
static const wchar_t* HOST_CLASS_NAME        = L"SumatraListerHostWnd";

// Thread-safe lookup helper: copies the shared_ptr out under g_mutex and
// returns immediately, so callers never hold g_mutex while doing slow work
// (launching/closing a process, sleeping between synthesized keystrokes).
static ListerInstancePtr FindInstance(HWND listWin)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_instances.find(listWin);
    return it != g_instances.end() ? it->second : nullptr;
}

// ---------------------------------------------------------------------------
//  ANSI/Unicode interop for the legacy ANSI-suffixed entry points
// ---------------------------------------------------------------------------

// Converts an ANSI (CP_ACP) C-string to UTF-16. Used by the ANSI-suffixed
// entry points (ListLoad, ListLoadNext, ListSearchText, ListPrint) that
// pre-Unicode builds of Total Commander call instead of the *W versions.
// Safe against null/empty input AND conversion failure: MultiByteToWideChar
// returns 0 on a malformed byte sequence for the given code page, and
// naively constructing a std::wstring from a then-possibly-null buffer
// pointer would be undefined behavior rather than just an empty result.
static std::wstring AnsiToWide(const char* s)
{
    if (!s || !*s)
        return std::wstring();

    int len = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (len <= 0)
        return std::wstring(); // malformed for this code page; fail safe rather than crash

    std::vector<wchar_t> buf(static_cast<size_t>(len));
    MultiByteToWideChar(CP_ACP, 0, s, -1, buf.data(), len);
    return std::wstring(buf.data());
}

// ---------------------------------------------------------------------------
//  Locate SumatraPDF.exe on this machine
// ---------------------------------------------------------------------------

static bool FileExistsW(const std::wstring& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool ReadRegString(HKEY root, const std::wstring& subKey, const std::wstring& valueName,
                           std::wstring& out)
{
    // Only two views are ever meaningfully distinct for a given build: the
    // default (0, native to this process) and whichever WOW64 flag isn't a
    // no-op for that bitness. A 64-bit process's default view already IS
    // the 64-bit view (KEY_WOW64_64KEY would just repeat it); a 32-bit
    // process is already WOW64-redirected to the 32-bit view by default
    // (KEY_WOW64_32KEY would just repeat it). Trying all three unconditionally
    // meant one RegOpenKeyExW call per subkey was always a wasted duplicate.
#ifdef _WIN64
    const REGSAM views[] = { 0, KEY_WOW64_32KEY };
#else
    const REGSAM views[] = { 0, KEY_WOW64_64KEY };
#endif
    for (REGSAM extra : views) {
        HKEY hKey;
        if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ | extra, &hKey) == ERROR_SUCCESS) {
            wchar_t buf[MAX_PATH * 2] = {0};
            DWORD   cb   = sizeof(buf);
            DWORD   type = 0;
            LONG    rc   = RegQueryValueExW(hKey, valueName.empty() ? nullptr : valueName.c_str(),
                                             nullptr, &type, (LPBYTE)buf, &cb);
            RegCloseKey(hKey);
            if (rc == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
                wchar_t expanded[MAX_PATH * 2] = {0};
                if (type == REG_EXPAND_SZ) {
                    ExpandEnvironmentStringsW(buf, expanded, MAX_PATH * 2);
                    out = expanded;
                } else {
                    out = buf;
                }
                if (!out.empty())
                    return true;
            }
        }
    }
    return false;
}

static std::wstring StripQuotesAndArgs(const std::wstring& s)
{
    if (!s.empty() && s.front() == L'"') {
        size_t end = s.find(L'"', 1);
        if (end != std::wstring::npos)
            return s.substr(1, end - 1);
    }
    return s;
}

static std::wstring FindSumatraPDF()
{
    // 0) Manual override from SumatraLister.ini wins over everything.
    if (!g_config.sumatraPathOverride.empty()) {
        if (FileExistsW(g_config.sumatraPathOverride)) {
            LogF(L"Using SumatraPath override: %ls", g_config.sumatraPathOverride.c_str());
            return g_config.sumatraPathOverride;
        }
        LogF(L"SumatraPath override set but file not found: %ls", g_config.sumatraPathOverride.c_str());
    }

    std::wstring path;

    // 1) App Paths (set by the Sumatra installer for both per-user / per-machine installs)
    if (ReadRegString(HKEY_CURRENT_USER,
                       L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\SumatraPDF.exe",
                       L"", path)) {
        path = StripQuotesAndArgs(path);
        if (FileExistsW(path))
            return path;
    }

    if (ReadRegString(HKEY_LOCAL_MACHINE,
                       L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\SumatraPDF.exe",
                       L"", path)) {
        path = StripQuotesAndArgs(path);
        if (FileExistsW(path))
            return path;
    }

    // 2) Sumatra's own uninstall / install-dir registry keys
    const wchar_t* uninstallKeys[] = {
        L"Software\\SumatraPDF",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SumatraPDF",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{0A5EBB13-4D6F-4674-A8C6-13E1107E0BE2}_is1", // legacy installer GUID
    };
    for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
        for (auto key : uninstallKeys) {
            if (ReadRegString(root, key, L"InstallLocation", path)) {
                std::wstring exe = path;
                if (!exe.empty() && exe.back() != L'\\') exe += L'\\';
                exe += L"SumatraPDF.exe";
                if (FileExistsW(exe)) return exe;
            }
            if (ReadRegString(root, key, L"DisplayIcon", path)) {
                path = StripQuotesAndArgs(path);
                if (FileExistsW(path)) return path;
            }
        }
    }

    // 3) Common install locations (covers portable-installed-to-Program-Files setups too)
    wchar_t programFiles[MAX_PATH]    = {0};
    wchar_t programFilesX86[MAX_PATH] = {0};
    wchar_t localAppData[MAX_PATH]    = {0};
    SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES,    nullptr, 0, programFiles);
    SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILESX86, nullptr, 0, programFilesX86);
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA,    nullptr, 0, localAppData);

    std::vector<std::wstring> candidates = {
        std::wstring(programFiles)    + L"\\SumatraPDF\\SumatraPDF.exe",
        std::wstring(programFilesX86) + L"\\SumatraPDF\\SumatraPDF.exe",
        std::wstring(localAppData)    + L"\\SumatraPDF\\SumatraPDF.exe",
        std::wstring(localAppData)    + L"\\Programs\\SumatraPDF\\SumatraPDF.exe",
    };
    for (auto& c : candidates)
        if (!c.empty() && FileExistsW(c))
            return c;

    // 4) Same directory as this plugin DLL (user may have dropped a portable copy there)
    {
        std::wstring local = GetModuleDir() + L"\\SumatraPDF.exe";
        if (FileExistsW(local)) return local;
        local = GetModuleDir() + L"\\SumatraPDF\\SumatraPDF.exe";
        if (FileExistsW(local)) return local;
    }

    // 5) Finally, look through the directories listed in PATH. Done by hand
    //    rather than with SearchPathW(nullptr, ...): that ALSO searches the
    //    application directory, the system directories and the CURRENT
    //    directory, and Total Commander's current directory is often simply
    //    whatever folder the user is browsing -- so a SumatraPDF.exe sitting in
    //    a downloaded or shared folder could otherwise be picked up and
    //    launched just because the user previewed a PDF (classic binary
    //    planting). Only absolute PATH entries count; empty or relative ones
    //    (which mean "the current directory") are skipped.
    {
        DWORD need = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        if (need > 0) {
            std::vector<wchar_t> buf(need);
            DWORD got = GetEnvironmentVariableW(L"PATH", buf.data(), need);
            if (got > 0 && got < need) {
                std::wstring pathVar(buf.data(), got);
                size_t start = 0;
                while (start <= pathVar.size()) {
                    size_t semi = pathVar.find(L';', start);
                    std::wstring dir = pathVar.substr(start, semi == std::wstring::npos ? std::wstring::npos : semi - start);
                    if (dir.size() >= 2 && dir.front() == L'"' && dir.back() == L'"')
                        dir = dir.substr(1, dir.size() - 2); // PATH entries are sometimes quoted
                    if (!dir.empty() && !PathIsRelativeW(dir.c_str())) {
                        if (dir.back() != L'\\') dir += L'\\';
                        std::wstring candidate = dir + L"SumatraPDF.exe";
                        if (FileExistsW(candidate))
                            return candidate;
                    }
                    if (semi == std::wstring::npos) break;
                    start = semi + 1;
                }
            }
        }
    }

    return L""; // not found
}

static std::wstring   g_sumatraPath;
static std::once_flag g_sumatraPathOnceFlag; // see g_configOnceFlag: same concurrent-caller concern

static void FindSumatraPDFImpl()
{
    g_sumatraPath = FindSumatraPDF();
    LogF(L"Resolved SumatraPDF.exe: %ls", g_sumatraPath.empty() ? L"<not found>" : g_sumatraPath.c_str());
}

static const std::wstring& GetSumatraPath()
{
    std::call_once(g_sumatraPathOnceFlag, FindSumatraPDFImpl);
    return g_sumatraPath;
}

// ---------------------------------------------------------------------------
//  Sumatra version detection
//
//  Reads the detected SumatraPDF.exe's own file version resource (standard
//  Win32 version-info APIs, not anything Sumatra-specific) so the plugin can
//  warn clearly, in the debug log and diagnostic snapshot, when a configured
//  feature needs a newer Sumatra than the one installed -- instead of the
//  feature silently doing nothing.
//
//  Deliberately conservative about WHICH features it checks: only minimum
//  versions confirmed against Sumatra's own official documentation are
//  encoded (currently just -search, documented as "ver 3.4+"). Flags whose
//  minimum version isn't confirmed (e.g. -named-dest's base flag predates
//  3.1, with no exact earlier version stated) are intentionally NOT checked
//  rather than guessed at, since a wrong guess here would mean falsely
//  telling users their perfectly fine Sumatra is "too old." An unreadable
//  or missing version resource is treated as "unknown" and given the
//  benefit of the doubt, never as "too old."
// ---------------------------------------------------------------------------

struct SumatraVersion {
    WORD major    = 0;
    WORD minor    = 0;
    WORD build    = 0;
    WORD revision = 0;
    bool known    = false;
};

static SumatraVersion g_sumatraVersion;
static std::once_flag g_sumatraVersionOnceFlag;

static void GetSumatraVersionImpl()
{
    const std::wstring& exe = GetSumatraPath();
    if (exe.empty())
        return;

    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &handle);
    if (size == 0) {
        LogF(L"GetFileVersionInfoSizeW failed for %ls, error %lu (Sumatra version unknown)",
             exe.c_str(), GetLastError());
        return;
    }

    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(exe.c_str(), 0, size, data.data())) {
        LogF(L"GetFileVersionInfoW failed for %ls, error %lu (Sumatra version unknown)",
             exe.c_str(), GetLastError());
        return;
    }

    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&ffi), &len) ||
        !ffi || len < sizeof(VS_FIXEDFILEINFO)) {
        LogF(L"VerQueryValueW found no fixed file info in %ls (Sumatra version unknown)", exe.c_str());
        return;
    }

    g_sumatraVersion.major    = HIWORD(ffi->dwFileVersionMS);
    g_sumatraVersion.minor    = LOWORD(ffi->dwFileVersionMS);
    g_sumatraVersion.build    = HIWORD(ffi->dwFileVersionLS);
    g_sumatraVersion.revision = LOWORD(ffi->dwFileVersionLS);
    g_sumatraVersion.known    = true;

    LogF(L"Detected SumatraPDF version %u.%u.%u.%u",
         (unsigned)g_sumatraVersion.major, (unsigned)g_sumatraVersion.minor,
         (unsigned)g_sumatraVersion.build, (unsigned)g_sumatraVersion.revision);
}

static const SumatraVersion& GetSumatraVersion()
{
    std::call_once(g_sumatraVersionOnceFlag, GetSumatraVersionImpl);
    return g_sumatraVersion;
}

// True if the detected Sumatra is at least major.minor -- OR if its version
// couldn't be determined at all (unknown versions get the benefit of the
// doubt rather than producing a false "too old" warning).
static bool SumatraVersionAtLeast(WORD major, WORD minor)
{
    const SumatraVersion& v = GetSumatraVersion();
    if (!v.known)
        return true;
    if (v.major != major)
        return v.major > major;
    return v.minor >= minor;
}

// Formats the detected version for display ("3.5.2.0", or "unknown").
static std::wstring FormatSumatraVersion()
{
    const SumatraVersion& v = GetSumatraVersion();
    if (!v.known)
        return L"unknown";
    return std::to_wstring(v.major) + L"." + std::to_wstring(v.minor) + L"." +
           std::to_wstring(v.build) + L"." + std::to_wstring(v.revision);
}

// ---------------------------------------------------------------------------
//  Deep links: "C:\book.pdf#page=42" -> path="C:\book.pdf", page=42, etc.
//
//  Syntax: one or more "&"-separated "key=value" pairs after the first '#'.
//  Values are percent-decoded (e.g. "%20" -> space; see UrlDecode).
//    page=N              -- open at page N
//    dest=NAME           -- open at a named destination / TOC entry (-named-dest)
//    search=TERM         -- open with TERM already searched/highlighted (-search)
//    forwardsearch=SRC:LINE -- SyncTeX/PdfSync jump from a LaTeX source
//                           location (-forward-search); SRC is matched via
//                           the LAST ':' with an all-digit tail, so a Windows
//                           drive letter ("C:") is never misread as the
//                           separator (it only ever appears at index 1).
//    pwd=PASSWORD        -- open a password-protected file (-pwd). Only ever
//                           read from a caller-supplied link like this, not
//                           persisted anywhere by this plugin.
//  Examples: "C:\book.pdf#page=5", "C:\book.pdf#dest=Chapter3",
//  "C:\book.pdf#search=hello%20world", "C:\book.pdf#page=5&search=foo",
//  "C:\report.pdf#forwardsearch=C%3A%5Csrc%5Cmain.tex:123", "...#pwd=secret"
//
//  page/dest/forwardsearch are mutually exclusive "where to open" commands;
//  search and pwd can combine with any of them. If more than one
//  positioning command is given, forwardsearch wins, then dest, then page
//  (logged if DebugLog=1) rather than silently picking one with no record
//  of the conflict.
// ---------------------------------------------------------------------------

struct DeepLinkParams {
    int          page             = -1;  // -page N
    std::wstring dest;                   // -named-dest <name>
    std::wstring search;                 // -search <term>
    std::wstring forwardSearchFile;      // -forward-search <sourcepath> <line> <pdfpath>
    int          forwardSearchLine  = -1;
    std::wstring pwd;                    // -pwd <password>

    bool HasPositionCommand() const { return page > 0 || !dest.empty() || forwardSearchLine > 0; }
};

// Minimal percent-decoding for deep-link values (e.g. "%20" -> ' '),
// following the standard URL query-string convention ('+' -> ' ' too).
// Malformed sequences (a '%' not followed by exactly two hex digits) are
// left as-is rather than treated as an error, since a value with a literal
// '%' in it (e.g. a search term) should still work on a best-effort basis.
static std::wstring UrlDecode(const std::wstring& in)
{
    std::wstring out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == L'%' && i + 2 < in.size() && iswxdigit(in[i + 1]) && iswxdigit(in[i + 2])) {
            wchar_t hex[3] = { in[i + 1], in[i + 2], 0 };
            out += (wchar_t)wcstol(hex, nullptr, 16);
            i += 2;
        } else if (in[i] == L'+') {
            out += L' ';
        } else {
            out += in[i];
        }
    }
    return out;
}

static DeepLinkParams ExtractDeepLinkParams(std::wstring& path)
{
    DeepLinkParams result;

    // If a literal file already exists at this exact path -- including any
    // "#..."-looking tail -- trust the filesystem over the heuristic. This
    // is what keeps a real file legitimately named e.g. "Report#draft.pdf"
    // from being silently misread as a deep link and opened under the
    // wrong (non-existent) trimmed path.
    if (FileExistsW(path))
        return result;

    size_t hashPos = path.find(L'#');
    if (hashPos == std::wstring::npos)
        return result;

    std::wstring suffix = path.substr(hashPos + 1);
    std::wstring base   = path.substr(0, hashPos);

    bool sawRecognizedKey = false;
    bool haveForwardSearch = false, haveDest = false, havePage = false;

    size_t start = 0;
    while (start <= suffix.size()) {
        size_t amp = suffix.find(L'&', start);
        std::wstring token = suffix.substr(start, amp == std::wstring::npos ? std::wstring::npos : amp - start);
        size_t eq = token.find(L'=');

        if (eq != std::wstring::npos) {
            std::wstring key = token.substr(0, eq);
            std::wstring val = UrlDecode(token.substr(eq + 1));

            if (key == L"page") {
                if (!val.empty() && val.find_first_not_of(L"0123456789") == std::wstring::npos) {
                    int p = _wtoi(val.c_str());
                    if (p > 0) { result.page = p; havePage = true; sawRecognizedKey = true; }
                }
            } else if (key == L"dest") {
                if (!val.empty()) { result.dest = val; haveDest = true; sawRecognizedKey = true; }
            } else if (key == L"search") {
                if (!val.empty()) { result.search = val; sawRecognizedKey = true; }
            } else if (key == L"forwardsearch") {
                // "<sourcepath>:<line>" -- split at the LAST ':' with an
                // all-digit tail; a drive letter's colon ("C:") only ever
                // appears at index 1, never at the end, so this can't
                // collide with a normal Windows path.
                size_t colon = val.rfind(L':');
                if (colon != std::wstring::npos && colon > 0 && colon + 1 < val.size()) {
                    std::wstring linePart = val.substr(colon + 1);
                    if (linePart.find_first_not_of(L"0123456789") == std::wstring::npos) {
                        int line = _wtoi(linePart.c_str());
                        if (line > 0) {
                            result.forwardSearchFile = val.substr(0, colon);
                            result.forwardSearchLine = line;
                            haveForwardSearch = true;
                            sawRecognizedKey = true;
                        }
                    }
                }
            } else if (key == L"pwd") {
                if (!val.empty()) { result.pwd = val; sawRecognizedKey = true; }
            }
            // unrecognized keys are silently ignored (forward-compatible)
        }

        if (amp == std::wstring::npos) break;
        start = amp + 1;
    }

    if (!sawRecognizedKey)
        return DeepLinkParams(); // nothing usable found; path is left untouched below

    int posCount = (haveForwardSearch ? 1 : 0) + (haveDest ? 1 : 0) + (havePage ? 1 : 0);
    if (posCount > 1) {
        // Priority order (forwardsearch > dest > page) decided once here,
        // rather than being expressed separately in a log-message ternary
        // and a clearing if/else-if that happened to need to agree with it.
        const wchar_t* winner = L"page";
        if (haveForwardSearch) {
            winner = L"forwardsearch";
            result.page = -1;
            result.dest.clear();
        } else if (haveDest) {
            winner = L"dest";
            result.page = -1;
        }
        LogF(L"Deep link for %ls specifies more than one position command; using %ls",
             base.c_str(), winner);
    }

    path = base;
    return result;
}

// ---------------------------------------------------------------------------
//  Hotkey strings: "Ctrl+Alt+O" -> RegisterHotKey modifier flags + VK code
//
//  Accepts any combination of the modifiers Ctrl (or Control), Alt, Shift,
//  and Win, followed by exactly one key: a letter A-Z, a digit 0-9, or a
//  function key F1-F24. Case-insensitive; whitespace around each "+"-
//  separated part is tolerated. At least one modifier is REQUIRED: these
//  are registered system-wide (see the pop-out/diagnostic hotkey notes
//  elsewhere), and a bare letter or function key would swallow that key in
//  every other application for as long as the hotkey is active. Anything
//  that doesn't parse cleanly is rejected as a whole (valid=false) rather
//  than guessed at -- the caller falls back to the documented default combo
//  and logs why, instead of silently registering something unintended.
// ---------------------------------------------------------------------------

struct ParsedHotkey {
    UINT mod   = 0;
    UINT vk    = 0;
    bool valid = false;
};

static ParsedHotkey ParseHotkeyString(const std::wstring& str)
{
    ParsedHotkey result;
    UINT mod = 0;
    UINT vk  = 0;
    bool haveKey = false;

    size_t start = 0;
    while (start <= str.size()) {
        size_t plus = str.find(L'+', start);
        std::wstring token = str.substr(start, plus == std::wstring::npos ? std::wstring::npos : plus - start);

        size_t first = token.find_first_not_of(L" \t");
        size_t last  = token.find_last_not_of(L" \t");
        if (first == std::wstring::npos)
            return result; // empty part, e.g. "Ctrl++O" or a trailing "+"
        token = token.substr(first, last - first + 1);

        if (haveKey)
            return result; // something after the key part: the key must be last

        if (_wcsicmp(token.c_str(), L"Ctrl") == 0 || _wcsicmp(token.c_str(), L"Control") == 0) {
            mod |= MOD_CONTROL;
        } else if (_wcsicmp(token.c_str(), L"Alt") == 0) {
            mod |= MOD_ALT;
        } else if (_wcsicmp(token.c_str(), L"Shift") == 0) {
            mod |= MOD_SHIFT;
        } else if (_wcsicmp(token.c_str(), L"Win") == 0) {
            mod |= MOD_WIN;
        } else if (token.size() == 1) {
            wchar_t c = (wchar_t)towupper(token[0]);
            if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) {
                vk = (UINT)c; // VK codes for A-Z / 0-9 equal their ASCII values
                haveKey = true;
            } else {
                return result;
            }
        } else if ((token[0] == L'F' || token[0] == L'f') && token.size() <= 3 &&
                   token.find_first_not_of(L"0123456789", 1) == std::wstring::npos) {
            int n = _wtoi(token.c_str() + 1);
            if (n < 1 || n > 24)
                return result;
            vk = (UINT)(VK_F1 + (n - 1));
            haveKey = true;
        } else {
            return result; // unknown token
        }

        if (plus == std::wstring::npos) break;
        start = plus + 1;
    }

    if (!haveKey || mod == 0)
        return result;

    result.mod   = mod | MOD_NOREPEAT;
    result.vk    = vk;
    result.valid = true;
    return result;
}

// ---------------------------------------------------------------------------
//  Host child window: a plain window that lives inside TC's ParentWin and
//  hosts Sumatra's reparented window. Resizing it resizes Sumatra to match.
// ---------------------------------------------------------------------------

// Forward declarations: defined later in the file, called from HostWndProc.
static void LaunchSumatraStandalone(const ListerInstance* inst);
static bool TryFallbackShellOpen(const std::wstring& filePath);
static LRESULT CALLBACK ErrorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void UnregisterCrashDetection(ListerInstance* inst);
static bool CopyDiagnosticSnapshotToClipboard(const ListerInstance* inst);
static void ShowDiagnosticSnapshot(HWND parentForBox, bool clipboardOk);

static LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_SIZE: {
            ListerInstancePtr inst = FindInstance(hwnd);
            if (inst && inst->sumatraWnd && IsWindow(inst->sumatraWnd)) {
                RECT rc; GetClientRect(hwnd, &rc);
                MoveWindow(inst->sumatraWnd, 0, 0, rc.right - rc.left, rc.bottom - rc.top, TRUE);
            }
            return 0;
        }
        case WM_SETFOCUS: {
            ListerInstancePtr inst = FindInstance(hwnd);
            if (inst) {
                if (inst->sumatraWnd && IsWindow(inst->sumatraWnd))
                    SetFocus(inst->sumatraWnd);
                // Notify TC's Quick View Panel (Ctrl+Q) that this pane
                // gained focus, so its header highlights correctly -- see
                // ITM_FOCUS's definition near the top of this file.
                // Harmless no-op when this Lister window isn't inside a
                // Quick View Panel.
                if (inst->parentWin && IsWindow(inst->parentWin))
                    PostMessageW(inst->parentWin, WM_COMMAND, MAKEWPARAM(0, ITM_FOCUS), (LPARAM)hwnd);
            }
            return 0;
        }
        case WM_HOTKEY: {
            // wParam is the id passed to RegisterHotKey, letting us tell
            // which of this instance's (up to two) hotkeys fired.
            // opLock here matches every other reader of filePath/currentInvert
            // (ListLoadNextW, ListSendCommand) so a hotkey press can't read
            // a half-updated file path mid-relaunch; `closed` skips a stale
            // instance that's mid-teardown.
            ListerInstancePtr inst = FindInstance(hwnd);
            bool showDiagBox = false;
            bool diagClipboardOk = false;
            HWND diagBoxParent = nullptr;

            if (inst) {
                std::lock_guard<std::mutex> opLock(inst->opLock);
                if (!inst->closed) {
                    if ((int)wParam == inst->popOutHotkeyId) {
                        LaunchSumatraStandalone(inst.get());
                    } else if ((int)wParam == inst->diagHotkeyId) {
                        // Only the data-gathering half runs under the lock;
                        // the message box itself must not (see
                        // ShowDiagnosticSnapshot's comment for why).
                        diagClipboardOk = CopyDiagnosticSnapshotToClipboard(inst.get());
                        diagBoxParent = inst->hostWnd;
                        showDiagBox = true;
                    }
                }
            } // opLock released here -- everything below runs without it held

            if (showDiagBox)
                ShowDiagnosticSnapshot(diagBoxParent, diagClipboardOk);

            return 0;
        }
        case WM_SUMATRA_PROCESS_EXITED: {
            // Posted by ProcessExitCallback when an embedded Sumatra
            // process exits on its own. processWaitHandle being still set
            // is the staleness guard described at RegisterCrashDetection/
            // UnregisterCrashDetection below -- if it's already null, our
            // own intentional close got there first and this notification
            // is obsolete.
            ListerInstancePtr inst = FindInstance(hwnd);
            if (inst) {
                std::lock_guard<std::mutex> opLock(inst->opLock);
                if (!inst->closed && inst->processWaitHandle && inst->pi.hProcess) {
                    LogF(L"Detected unexpected Sumatra process exit for %ls", inst->filePath.c_str());
                    inst->processWaitHandle = nullptr; // already fired; nothing left to unregister
                    CloseHandle(inst->pi.hThread);
                    CloseHandle(inst->pi.hProcess);
                    inst->pi = {};
                    inst->sumatraWnd = nullptr;

                    bool opened = TryFallbackShellOpen(inst->filePath);
                    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)ErrorWndProc);
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, opened ? ERRSTATE_OPENED_EXTERNAL : ERRSTATE_CRASHED);
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
            }
            return 0;
        }
        case WM_PAINT: {
            // "Opening in SumatraPDF..." indicator, shown for as long as the
            // host window has no embedded child covering it. Once Sumatra's
            // window is embedded it fills this window's entire client area
            // (and WS_CLIPCHILDREN excludes that area from this window's own
            // painting), so this stops being visible on its own without any
            // explicit "hide the indicator" state to track. Callers that are
            // about to block waiting for an embed force an immediate,
            // synchronous paint via InvalidateRect+UpdateWindow first --
            // that's what makes this actually visible during the wait, since
            // the UI thread isn't pumping messages while it blocks.
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);

            ListerInstancePtr inst = FindInstance(hwnd);
            bool dark = inst && inst->currentInvert; // cosmetic-only read; a stale value is harmless
            if (dark) {
                HBRUSH bg = CreateSolidBrush(RGB(32, 32, 32));
                FillRect(dc, &rc, bg);
                DeleteObject(bg); // real GDI object, unlike the COLOR_* stock-brush sentinel below
                SetTextColor(dc, RGB(190, 190, 190));
            } else {
                FillRect(dc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
                SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
            }
            SetBkMode(dc, TRANSPARENT);
            InflateRect(&rc, -10, -10);
            DrawTextW(dc, L"Opening in SumatraPDF...", -1, &rc, DT_TOP | DT_LEFT | DT_SINGLELINE);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1; // avoid flicker; WM_PAINT above fills the background itself, and Sumatra's child covers the area once embedded
        case WM_DESTROY:
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static std::once_flag g_hostClassOnceFlag;

static void EnsureHostClassRegistered()
{
    std::call_once(g_hostClassOnceFlag, []() {
        WNDCLASSW wc = {};
        wc.lpfnWndProc   = HostWndProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = HOST_CLASS_NAME;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
    });
}

static HWND WaitForSumatraChild(HWND hostWnd, HANDLE hProcess, DWORD processId, DWORD timeoutMs)
{
    DWORD start = GetTickCount();

    // Most of the wait is normally "process hasn't created its message
    // queue / main window yet". WaitForInputIdle blocks efficiently (no
    // busy polling) until Sumatra's main thread is ready to pump messages,
    // which is almost always already true by the time it reparents itself.
    DWORD idleBudget = timeoutMs > 1000 ? timeoutMs - 500 : timeoutMs / 2;
    WaitForInputIdle(hProcess, idleBudget);

    HWND  found = nullptr;
    DWORD pollIntervalMs = 15; // start fast; the window usually appears right after input-idle
    while (GetTickCount() - start < timeoutMs) {
        HWND child = GetWindow(hostWnd, GW_CHILD);
        while (child) {
            DWORD pid = 0;
            GetWindowThreadProcessId(child, &pid);
            if (pid == processId) { found = child; break; }
            child = GetWindow(child, GW_HWNDNEXT);
        }
        if (found) break;
        Sleep(pollIntervalMs);
        if (pollIntervalMs < 50) pollIntervalMs = std::min(pollIntervalMs + 10, 50ul); // hard-cap at 50ms
    }
    return found;
}

// ---------------------------------------------------------------------------
//  Crash detection: notice when an embedded Sumatra process exits on its
//  own (crash, killed externally, internal error) rather than via our own
//  intentional close, and show an accurate message instead of leaving a
//  dead, unresponsive blank pane.
//
//  Uses RegisterWaitForSingleObject (a thread-pool wait, not polling) on
//  the process handle. The callback runs on an arbitrary thread-pool thread
//  and must do minimal, thread-safe work only -- it just posts a message
//  back to the host window, which does the real handling on the UI thread.
//
//  Race safety: CloseRunningSumatraProcess (our own intentional-close path)
//  always calls UnregisterWaitEx BEFORE taking any action that could cause
//  the process to actually exit (WM_CLOSE / TerminateProcess). Since
//  UnregisterWaitEx cancels the registration and blocks until any
//  in-progress callback finishes, and the process handle cannot yet be
//  signaled at that point (we haven't asked it to close yet), an
//  intentional close can never itself trigger this notification. The
//  WM_SUMATRA_PROCESS_EXITED handler additionally checks that
//  processWaitHandle is still set before acting, as a defense-in-depth
//  guard against any notification that outlives its instance.
// ---------------------------------------------------------------------------

static void CALLBACK ProcessExitCallback(void* lpParameter, BOOLEAN /*timedOut*/)
{
    // Minimal work only: this runs on an OS thread-pool thread, not ours.
    // PostMessageW is documented safe to call from any thread.
    HWND hostWnd = (HWND)lpParameter;
    PostMessageW(hostWnd, WM_SUMATRA_PROCESS_EXITED, 0, 0);
}

// Registers crash detection for inst's currently-running process. Must be
// called with inst->opLock held (or before the instance is visible to other
// threads), right after a successful embed.
static void RegisterCrashDetection(ListerInstance* inst)
{
    if (!inst->pi.hProcess || inst->processWaitHandle)
        return; // nothing to watch, or already watching

    if (!RegisterWaitForSingleObject(&inst->processWaitHandle, inst->pi.hProcess,
                                      ProcessExitCallback, (void*)inst->hostWnd,
                                      INFINITE, WT_EXECUTEONLYONCE)) {
        LogF(L"RegisterWaitForSingleObject failed, error %lu (crash detection unavailable for this instance)",
             GetLastError());
        inst->processWaitHandle = nullptr;
    }
}

// Cancels crash detection. Must be called with inst->opLock held, and
// BEFORE any action that could cause inst->pi.hProcess to actually exit --
// see the race-safety note above.
static void UnregisterCrashDetection(ListerInstance* inst)
{
    if (inst->processWaitHandle) {
        // INVALID_HANDLE_VALUE here means "block until any in-progress
        // callback completes," not "pass an invalid handle" -- this is the
        // documented Win32 idiom for a synchronous, race-free unregister.
        UnregisterWaitEx(inst->processWaitHandle, INVALID_HANDLE_VALUE);
        inst->processWaitHandle = nullptr;
    }
}

// ---------------------------------------------------------------------------
//  Command-line construction helpers
// ---------------------------------------------------------------------------

static void AppendArg(std::wstring& cmd, const std::wstring& arg)
{
    cmd += L' ';
    cmd += arg;
}

// Appends `arg` to `cmd` as a properly quoted, escaped command-line
// argument, following the same backslash/quote escaping rules
// CommandLineToArgvW (and therefore every normal C/C++ argv-parsing child
// process, including SumatraPDF) expects. Produces identical output to a
// naive '"'+arg+'"' wrap for any argument with no embedded quotes and no
// backslash run immediately before the end, but correctly escapes the
// cases that would otherwise let a crafted value (a deep-link search term,
// destination name, LaTeX path or password; an InverseSearchCommand with
// its own inner quotes) break out of its quoting and inject additional
// command-line arguments. Verified with a round-trip test against the real
// CommandLineToArgvW (embedded quotes, trailing backslashes, and an
// explicit injection attempt), not just reasoned through.
static void AppendQuotedArg(std::wstring& cmd, const std::wstring& arg)
{
    cmd += L' ';
    cmd += L'"';
    for (size_t i = 0; i < arg.size(); ) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') { backslashes++; i++; }

        if (i == arg.size()) {
            // Backslashes at the very end of the argument: double them, since
            // our own closing quote immediately follows and would otherwise
            // be escaped by an odd trailing backslash instead of closing the
            // argument.
            cmd.append(backslashes * 2, L'\\');
            break;
        } else if (arg[i] == L'"') {
            // Backslashes immediately before a literal quote: double them,
            // then escape the quote itself.
            cmd.append(backslashes * 2 + 1, L'\\');
            cmd += L'"';
            i++;
        } else {
            // Backslashes not followed by a quote are literal; no escaping needed.
            cmd.append(backslashes, L'\\');
            cmd += arg[i];
            i++;
        }
    }
    cmd += L'"';
}

// Converts a pointer-sized handle (HWND, HINSTANCE, ...) to a long long, for
// formatting with std::to_wstring: the decimal "-plugin <hwnd>" argument and
// log messages. Deliberately NOT formatted via a printf-style "%lld"/"%I64d":
// printf length modifiers differ between the C runtimes this file can be built
// against (legacy msvcrt.dll vs UCRT vs MinGW's ISO-style implementation), and
// passing a 32-bit handle to a 64-bit-reading specifier on a 32-bit build would
// be a vararg width mismatch. std::to_wstring(long long) has neither problem.
// (The implicit widening below is a genuine conversion on 32-bit builds and a
// no-op on 64-bit ones, so no explicit cast is needed -- or flagged.)
static long long HandleToInt64(void* handle)
{
    return reinterpret_cast<intptr_t>(handle);
}

// ---------------------------------------------------------------------------
//  Hotkeys (pop-out, diagnostic snapshot)
//
//  Both are registered with RegisterHotKey rather than by subclassing the
//  embedded Sumatra window. Subclassing can't work here: the Sumatra window
//  belongs to SumatraPDF.exe's own process, and a WNDPROC is just a code
//  pointer -- one from our DLL is meaningless (and would crash the other
//  process) when that process's message loop tries to call through it in
//  its own address space. RegisterHotKey is the documented mechanism for
//  "deliver a message to my own window on a specific key combo, regardless
//  of which window currently has keyboard focus", and the resulting
//  WM_HOTKEY is delivered to hostWnd, which we DO own, in our own process.
//
//  The real tradeoff, and why both default to OFF, is that the claim is
//  genuinely system-wide for as long as any Lister pane using this plugin
//  is open -- not scoped to just that pane having focus -- so it can
//  shadow the same combo in other applications. The combos themselves are
//  configurable (PopOutHotkey= / DiagnosticHotkey=) so a conflict can be
//  worked around by remapping instead of only by disabling the feature.
// ---------------------------------------------------------------------------

static int g_nextHotkeyId = 1; // protected by g_mutex

// Registers a system-wide hotkey for this instance's hostWnd, storing the
// resulting id in *idField. Idempotent (skips re-registering if *idField is
// already set) and tolerant of failure (e.g. another application already
// owns the combo) -- the corresponding feature simply stays unavailable for
// that instance rather than erroring out. An unparseable combo string falls
// back to the documented default (logged) rather than registering
// something unintended. Must be called on the same thread that created
// inst->hostWnd, which is always true here: every caller of
// LaunchSumatraEmbedded runs on Total Commander's UI thread, same as
// DoListLoad's CreateWindowExW for hostWnd.
static void RegisterInstanceHotkey(ListerInstance* inst, int* idField, const std::wstring& comboStr,
                                    const wchar_t* purpose, const wchar_t* defaultCombo)
{
    if (*idField != 0 || !inst->hostWnd)
        return;

    std::wstring effective = comboStr;
    ParsedHotkey hk = ParseHotkeyString(comboStr);
    if (!hk.valid) {
        LogF(L"%ls hotkey '%ls' isn't a valid combination (expected e.g. \"Ctrl+Alt+O\": at least one of "
             L"Ctrl/Alt/Shift/Win plus one key A-Z, 0-9 or F1-F24); falling back to the default %ls",
             purpose, comboStr.c_str(), defaultCombo);
        effective = defaultCombo;
        hk = ParseHotkeyString(effective);
        if (!hk.valid)
            return; // can't happen for the built-in defaults, but never register garbage
    }

    int id;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        id = g_nextHotkeyId++;
    }

    if (RegisterHotKey(inst->hostWnd, id, hk.mod, hk.vk)) {
        *idField = id;
    } else {
        LogF(L"RegisterHotKey for %ls (%ls) failed, error %lu (another application may already use that "
             L"combination -- try a different one via the matching *Hotkey= setting)",
             purpose, effective.c_str(), GetLastError());
    }
}

static void UnregisterInstanceHotkey(ListerInstance* inst, int* idField)
{
    if (*idField != 0 && inst->hostWnd) {
        UnregisterHotKey(inst->hostWnd, *idField);
        *idField = 0;
    }
}

static void RegisterPopOutHotkey(ListerInstance* inst)
{
    if (g_config.enablePopOut)
        RegisterInstanceHotkey(inst, &inst->popOutHotkeyId, g_config.popOutHotkeyStr, L"pop-out", L"Ctrl+Alt+O");
}
static void UnregisterPopOutHotkey(ListerInstance* inst)
{
    UnregisterInstanceHotkey(inst, &inst->popOutHotkeyId);
}

static void RegisterDiagnosticHotkey(ListerInstance* inst)
{
    if (g_config.enableDiagnosticHotkey)
        RegisterInstanceHotkey(inst, &inst->diagHotkeyId, g_config.diagnosticHotkeyStr, L"diagnostics", L"Ctrl+Alt+D");
}
static void UnregisterDiagnosticHotkey(ListerInstance* inst)
{
    UnregisterInstanceHotkey(inst, &inst->diagHotkeyId);
}

// ---------------------------------------------------------------------------
//  Launching SumatraPDF.exe
// ---------------------------------------------------------------------------

// Appends the CLI flags corresponding to a parsed deep link, given the
// already-stripped path of the file being opened. Returns true if the file
// path was already included as part of what was appended here (this
// happens only for -forward-search, whose third argument per Sumatra's own
// documented syntax -- "-forward-search <sourcepath> <line> <pdfpath>",
// confirmed against the official command-line reference -- IS the file to
// open, unlike -page/-named-dest/-search which apply to whatever file is
// given separately via the ordinary trailing argument). Callers must NOT
// also append filePath as a separate trailing argument when this returns
// true, or the file would appear twice on the command line. Shared by
// LaunchSumatraStandalone and LaunchSumatraEmbedded so the two launch
// paths can't drift out of sync with each other.
static bool AppendDeepLinkArgs(std::wstring& cmdLine, const DeepLinkParams& dl, const std::wstring& filePath)
{
    bool filePathAlreadyIncluded = false;

    if (dl.forwardSearchLine > 0 && !dl.forwardSearchFile.empty()) {
        AppendArg(cmdLine, L"-forward-search");
        AppendQuotedArg(cmdLine, dl.forwardSearchFile);
        AppendArg(cmdLine, std::to_wstring(dl.forwardSearchLine));
        AppendQuotedArg(cmdLine, filePath);
        filePathAlreadyIncluded = true;
    } else if (!dl.dest.empty()) {
        AppendArg(cmdLine, L"-named-dest");
        AppendQuotedArg(cmdLine, dl.dest);
    } else if (dl.page > 0) {
        AppendArg(cmdLine, L"-page");
        AppendArg(cmdLine, std::to_wstring(dl.page));
    }

    if (!dl.search.empty()) {
        AppendArg(cmdLine, L"-search");
        AppendQuotedArg(cmdLine, dl.search);
    }
    if (!dl.pwd.empty()) {
        AppendArg(cmdLine, L"-pwd");
        AppendQuotedArg(cmdLine, dl.pwd);
    }

    return filePathAlreadyIncluded;
}

// "Pop out" to a normal, full SumatraPDF window: reopens the same file
// (preserving any deep-link position) in an ordinary, separate Sumatra
// window with its full UI, without disturbing the embedded preview.
// Deliberately minimal -- it doesn't re-apply RestrictMode/BackgroundColor/
// InverseSearchCommand: the latter two are persisted by Sumatra itself
// (see their INI notes), and pop-out is meant to give a "normal" window.
static void LaunchSumatraStandalone(const ListerInstance* inst)
{
    const std::wstring& exe = GetSumatraPath();
    if (exe.empty() || inst->filePath.empty())
        return;

    std::wstring filePath = inst->filePath;
    DeepLinkParams dl = ExtractDeepLinkParams(filePath);

    std::wstring cmdLine = L"\"" + exe + L"\"";
    bool filePathIncluded = AppendDeepLinkArgs(cmdLine, dl, filePath);
    if (inst->currentInvert)
        AppendArg(cmdLine, L"-invertcolors");
    if (!filePathIncluded)
        AppendQuotedArg(cmdLine, filePath);

    LogF(L"Pop-out: launching standalone Sumatra: %ls", cmdLine.c_str());

    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    // Fire-and-forget: this standalone window's lifetime is intentionally
    // independent of the embedded instance, so we don't track its handles.
    if (CreateProcessW(exe.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE,
                        0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        LogF(L"Pop-out CreateProcess failed, error %lu", GetLastError());
    }
}

static bool LaunchSumatraEmbedded(ListerInstance* inst)
{
    const std::wstring& exe = GetSumatraPath();
    if (exe.empty()) {
        LogF(L"Cannot launch: SumatraPDF.exe not found");
        return false;
    }

    std::wstring filePath = inst->filePath;
    DeepLinkParams dl = ExtractDeepLinkParams(filePath); // strips any deep-link suffix if present

    // -search needs Sumatra 3.4+ (confirmed against Sumatra's own official
    // command-line reference). Still attempted either way -- the version
    // check is only advisory, so a wrong/odd version reading can never make
    // this plugin MORE restrictive than Sumatra itself would be -- but a
    // clear log line beats the feature silently doing nothing.
    if (!dl.search.empty() && !SumatraVersionAtLeast(3, 4)) {
        LogF(L"Deep link for %ls uses search=, which needs SumatraPDF 3.4 or newer; the installed "
             L"version is %ls, so this may be ignored", filePath.c_str(), FormatSumatraVersion().c_str());
    }

    std::wstring cmdLine = L"\"" + exe + L"\"";
    AppendArg(cmdLine, L"-plugin");
    AppendArg(cmdLine, std::to_wstring(HandleToInt64(inst->hostWnd)));

    bool filePathIncluded = AppendDeepLinkArgs(cmdLine, dl, filePath);

    const PerExtensionOverrides* ov = FindExtensionOverrides(filePath); // computed once, used by both lookups below
    std::wstring effectiveZoom = GetEffectiveZoom(ov);
    std::wstring effectiveView = GetEffectiveView(ov);
    if (!effectiveZoom.empty()) {
        AppendArg(cmdLine, L"-zoom");
        AppendQuotedArg(cmdLine, effectiveZoom);
    }
    if (!effectiveView.empty()) {
        AppendArg(cmdLine, L"-view");
        AppendQuotedArg(cmdLine, effectiveView);
    }
    if (!g_config.backgroundColor.empty()) {
        AppendArg(cmdLine, L"-bg-color");
        AppendQuotedArg(cmdLine, g_config.backgroundColor);
    }
    if (!g_config.inverseSearchCommand.empty()) {
        AppendArg(cmdLine, L"-inverse-search");
        AppendQuotedArg(cmdLine, g_config.inverseSearchCommand);
    }
    if (inst->currentInvert)
        AppendArg(cmdLine, L"-invertcolors");
    if (g_config.restrictMode)
        AppendArg(cmdLine, L"-restrict");
    if (!g_config.extraArgs.empty())
        AppendArg(cmdLine, g_config.extraArgs);

    if (!filePathIncluded)
        AppendQuotedArg(cmdLine, filePath);

    LogF(L"Launching: %ls", cmdLine.c_str());

    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si = {};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(
        exe.c_str(),
        cmdBuf.data(),
        nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi);

    if (!ok) {
        LogF(L"CreateProcess failed, error %lu", GetLastError());
        return false;
    }

    inst->pi = pi;

    HWND child = WaitForSumatraChild(inst->hostWnd, pi.hProcess, pi.dwProcessId, g_config.embedTimeoutMs);
    if (!child) {
        LogF(L"Sumatra did not reparent within %lu ms (no -plugin support / crashed?)", g_config.embedTimeoutMs);
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        inst->pi = {};
        return false;
    }

    inst->sumatraWnd = child;
    RegisterPopOutHotkey(inst);
    RegisterDiagnosticHotkey(inst);
    RegisterCrashDetection(inst);

    RECT rc; GetClientRect(inst->hostWnd, &rc);
    MoveWindow(child, 0, 0, rc.right - rc.left, rc.bottom - rc.top, TRUE);
    ShowWindow(child, SW_SHOW);

    return true;
}

// Shared by TeardownInstance, RelaunchSumatra, and ListLoadNextW: closes the
// currently-running embedded Sumatra process gracefully (WM_CLOSE), falling
// back to TerminateProcess if it doesn't exit promptly. Caller must hold
// inst->opLock. Leaves inst->pi / inst->sumatraWnd zeroed on return.
static void CloseRunningSumatraProcess(ListerInstance* inst, DWORD graceMs = 1200)
{
    if (!inst->pi.hProcess)
        return;

    // Must happen before WM_CLOSE/TerminateProcess below -- see the
    // race-safety note at RegisterCrashDetection/UnregisterCrashDetection.
    UnregisterCrashDetection(inst);

    if (inst->sumatraWnd && IsWindow(inst->sumatraWnd))
        PostMessageW(inst->sumatraWnd, WM_CLOSE, 0, 0);

    if (WaitForSingleObject(inst->pi.hProcess, graceMs) == WAIT_TIMEOUT)
        TerminateProcess(inst->pi.hProcess, 0);

    CloseHandle(inst->pi.hThread);
    CloseHandle(inst->pi.hProcess);
    inst->pi = {};
    inst->sumatraWnd = nullptr;
}

// Tears the instance all the way down: closes the Sumatra process and
// destroys the host window. MUST be called with inst->opLock held, and is
// idempotent (the `closed` flag makes a second call a no-op) so it's safe
// even if some other thread is mid-operation and only gets opLock afterward.
static void TeardownInstance(ListerInstance* inst)
{
    if (!inst || inst->closed)
        return;
    inst->closed = true;

    CloseRunningSumatraProcess(inst, 1500);
    UnregisterPopOutHotkey(inst);
    UnregisterDiagnosticHotkey(inst);

    if (inst->hostWnd && IsWindow(inst->hostWnd))
        DestroyWindow(inst->hostWnd);
    inst->hostWnd = nullptr;
}

// ---------------------------------------------------------------------------
//  Fallback / error display
// ---------------------------------------------------------------------------

// Last-resort fallback when Sumatra can't be found/embedded: open the file
// with whatever the user's default Windows handler for it is. This can't be
// embedded into the Lister pane (a foreign app's top-level window can't be
// safely reparented the way Sumatra's -plugin mode supports), so it opens
// as a separate window; the Lister pane still shows an explanatory message.
static bool TryFallbackShellOpen(const std::wstring& filePath)
{
    if (!g_config.fallbackShellOpen || filePath.empty())
        return false;

    LogF(L"FallbackToShellOpen: launching default handler for %ls", filePath.c_str());
    HINSTANCE result = ShellExecuteW(nullptr, L"open", filePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    bool ok = (intptr_t)result > 32; // per ShellExecute convention
    if (!ok) {
        LogF(L"FallbackToShellOpen failed, ShellExecute returned %ls", std::to_wstring(HandleToInt64(result)).c_str());
    }
    return ok;
}

// The error/fallback pane. Installed over a host window's WNDPROC (see
// ShowErrorPane) rather than being a separate window class: the host
// window already exists and already fills the Lister pane, so swapping what
// handles its messages is simpler than creating and managing a second
// window on top of it. hwnd's GWLP_USERDATA holds an ERRSTATE_* value
// selecting which message to show.
static LRESULT CALLBACK ErrorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
        SetBkMode(dc, TRANSPARENT);

        LONG_PTR state = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        const wchar_t* msg1 = L"SumatraPDF could not be found or started.";
        const wchar_t* msg2 = L"Install it from https://www.sumatrapdfreader.org/ "
                               L"or place SumatraPDF.exe next to this plugin. "
                               L"Enable DebugLog=1 in SumatraLister.ini for details.";
        if (state == ERRSTATE_OPENED_EXTERNAL) {
            msg1 = L"SumatraPDF could not be embedded here, so the file was opened in its default application instead.";
            msg2 = L"Install SumatraPDF from https://www.sumatrapdfreader.org/ for an embedded preview here.";
        } else if (state == ERRSTATE_CRASHED) {
            msg1 = L"SumatraPDF closed unexpectedly while viewing this file.";
            msg2 = L"This usually means the file is corrupted or triggered a rendering "
                   L"issue in SumatraPDF itself. Try a different file, or re-open this "
                   L"one to try again. Enable DebugLog=1 in SumatraLister.ini for details.";
        }

        InflateRect(&rc, -10, -10);
        DrawTextW(dc, msg1, -1, &rc, DT_TOP | DT_LEFT | DT_WORDBREAK);
        rc.top += 36;
        DrawTextW(dc, msg2, -1, &rc, DT_TOP | DT_LEFT | DT_WORDBREAK);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Switches a host window to the error pane showing the given ERRSTATE_*.
static void ShowErrorPane(HWND hwnd, int state)
{
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)ErrorWndProc);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, state);
    InvalidateRect(hwnd, nullptr, TRUE);
}

// Puts a host window back on its normal HostWndProc. A host window reused
// for another attempt (ListLoadNext, a night-mode relaunch) may have been
// left showing the error pane by an earlier failed one; without this,
// resize/focus/hotkey handling would stay broken for the rest of that
// pane's life even after a later attempt succeeded, and the "Opening in
// SumatraPDF..." indicator couldn't be shown on a retry.
static void RestoreHostWndProc(HWND hwnd)
{
    if ((WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC) != HostWndProc) {
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)HostWndProc);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
}

// Forces an immediate, synchronous paint of the host window's "Opening in
// SumatraPDF..." indicator. Needed right before any blocking embed wait:
// the UI thread isn't pumping messages while it blocks, so an ordinary
// InvalidateRect alone would leave the indicator queued but never painted
// until the wait was already over. UpdateWindow dispatches WM_PAINT
// directly to the window procedure, bypassing the message queue.
static void ShowLoadingIndicator(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return;
    RestoreHostWndProc(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
    UpdateWindow(hwnd);
}

// ---------------------------------------------------------------------------
//  Best-effort UI forwarding into Sumatra (find / copy / select-all / ...)
//  Sumatra has no public message-based remote-control API, so these use
//  synthetic input (SendInput) targeted at its focused window, the same way
//  a user's own keypresses would reach it. This requires the embedding
//  window (i.e. Total Commander's Lister) to be the foreground window.
// ---------------------------------------------------------------------------

// Keyboard modifier for SendKeyCombo. A plain enum (not enum class) so the
// call sites read naturally, e.g. SendKeyCombo(ModCtrl, 'F'); fixed to a
// 1-byte underlying type since it only ever needs 3 values.
enum KeyModifier : std::uint8_t { ModNone, ModCtrl, ModShift };

// Synthesizes a key-down sequence (optionally with Ctrl or Shift held)
// followed by a matching key-up sequence, e.g. SendKeyCombo(ModCtrl, 'F')
// == Ctrl+F, SendKeyCombo(ModShift, VK_F3) == Shift+F3.
static void SendKeyCombo(KeyModifier mod, WORD vk)
{
    WORD modVk = 0;
    switch (mod) {
        case ModCtrl:  modVk = VK_CONTROL; break;
        case ModShift: modVk = VK_SHIFT;   break;
        case ModNone:  break;
    }

    INPUT down[2] = {};
    int   n = 0;
    if (modVk) { down[n].type = INPUT_KEYBOARD; down[n].ki.wVk = modVk; n++; }
    down[n].type = INPUT_KEYBOARD; down[n].ki.wVk = vk; n++;
    SendInput(static_cast<UINT>(n), down, sizeof(INPUT)); // n is 1-2, bounded by the array size above

    INPUT up[2] = {};
    n = 0;
    up[n].type = INPUT_KEYBOARD; up[n].ki.wVk = vk; up[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
    if (modVk) { up[n].type = INPUT_KEYBOARD; up[n].ki.wVk = modVk; up[n].ki.dwFlags = KEYEVENTF_KEYUP; n++; }
    SendInput(static_cast<UINT>(n), up, sizeof(INPUT)); // n is 1-2, bounded by the array size above
}

static bool SetClipboardTextW(const std::wstring& text)
{
    if (!OpenClipboard(nullptr))
        return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* dst = GlobalLock(hMem);
        memcpy(dst, text.c_str(), bytes);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
    }
    CloseClipboard();
    return hMem != nullptr;
}

// Reads the clipboard's current CF_UNICODETEXT content, if any. Used to
// save/restore the user's clipboard around ForwardFindToSumatra's
// paste-into-find-box trick, which would otherwise silently overwrite
// whatever the user last copied (potentially something they still meant to
// paste elsewhere) every time they used Lister's Find dialog.
static bool GetClipboardTextW(std::wstring& out)
{
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
        return false;
    if (!OpenClipboard(nullptr))
        return false;

    bool ok = false;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        const wchar_t* src = (const wchar_t*)GlobalLock(hData);
        if (src) {
            out = src;
            ok = true;
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return ok;
}

// Opens Sumatra's find bar and executes a search for `text`, honoring the
// real LCS_BACKWARDS flag from SearchParameter for direction (F3/Shift+F3).
// Always (re)pastes `text` into the find box before searching, even on a
// "find next" call (LCS_FINDFIRST unset) -- this keeps Sumatra's find box
// guaranteed in sync with whatever Total Commander currently considers the
// search text to be, rather than assuming it's still there unchanged from
// an earlier call, at the cost of a harmless redundant paste on repeats.
static void ForwardFindToSumatra(ListerInstance* inst, const std::wstring& text, int searchFlags)
{
    if (!inst->sumatraWnd || !IsWindow(inst->sumatraWnd) || text.empty())
        return;

    bool backwards = (searchFlags & LCS_BACKWARDS) != 0;
    // LCS_MATCHCASE / LCS_WHOLEWORDS are intentionally not translated here:
    // this plugin doesn't have confirmed knowledge of Sumatra's exact find-
    // bar hotkeys for toggling those specific options, and guessing risks
    // silently toggling the wrong control instead. See README.

    std::wstring savedClipboard;
    bool hadClipboardText = GetClipboardTextW(savedClipboard);

    SetFocus(inst->sumatraWnd);
    SendKeyCombo(ModCtrl, 'F'); // Ctrl+F -> open/focus Sumatra's find toolbar
    Sleep(120);

    SendKeyCombo(ModCtrl, 'A'); // select any existing text in the find box
    Sleep(30);
    if (SetClipboardTextW(text)) {
        SendKeyCombo(ModCtrl, 'V'); // paste search text
        Sleep(30);
        SendKeyCombo(backwards ? ModShift : ModNone, VK_F3); // execute search in the requested direction

        // Give Sumatra a moment to actually process the paste and the
        // search before we touch the clipboard again -- restoring too
        // early could overwrite our search text before Sumatra's own
        // thread gets around to reading it, pasting stale content instead.
        Sleep(50);
        if (hadClipboardText)
            SetClipboardTextW(savedClipboard);
        // If there was nothing on the clipboard before (or it held a
        // non-text format we don't track), we leave the search text in
        // place rather than actively clearing it -- we only restore
        // content we know we clobbered.
    }
}

static void ForwardCopyToSumatra(ListerInstance* inst)
{
    if (!inst->sumatraWnd || !IsWindow(inst->sumatraWnd))
        return;
    SetFocus(inst->sumatraWnd);
    SendKeyCombo(ModCtrl, 'C'); // Ctrl+C -> copies current selection to clipboard
}

// Real LC_SELECTALL command: select the whole document contents.
static void ForwardSelectAllToSumatra(ListerInstance* inst)
{
    if (!inst->sumatraWnd || !IsWindow(inst->sumatraWnd))
        return;
    SetFocus(inst->sumatraWnd);
    SendKeyCombo(ModCtrl, 'A');
}

// Real LC_SETPERCENT command: scroll to a position in the document, given
// as a percent (0-100). Sumatra has no message-based API to query total
// page count, so translating an arbitrary percentage into an exact target
// page isn't possible without risking a silently wrong jump. The two
// unambiguous boundary cases are handled precisely via Sumatra's own
// Ctrl+Home/Ctrl+End; everything in between is a documented no-op rather
// than a guess.
static void ForwardSetPercentToSumatra(ListerInstance* inst, int percent)
{
    if (!inst->sumatraWnd || !IsWindow(inst->sumatraWnd))
        return;
    SetFocus(inst->sumatraWnd);
    if (percent <= 1)
        SendKeyCombo(ModCtrl, VK_HOME); // beginning of document
    else if (percent >= 99)
        SendKeyCombo(ModCtrl, VK_END);  // end of document
}

// Opens Sumatra's own native print dialog (full printer/page-range/copies
// control) in an already-embedded instance, used by ListPrint when reusing
// a live pane for the exact file being printed -- see ListPrintW.
static void ForwardPrintDialogToSumatra(ListerInstance* inst)
{
    if (!inst->sumatraWnd || !IsWindow(inst->sumatraWnd))
        return;
    SetFocus(inst->sumatraWnd);
    SendKeyCombo(ModCtrl, 'P');
}

// ---------------------------------------------------------------------------
//  Diagnostic snapshot (hotkey-triggered; see EnableDiagnosticHotkey)
// ---------------------------------------------------------------------------

// Builds a plain-text diagnostic snapshot for bug reports: plugin version,
// build architecture, detected Sumatra path and version, this instance's
// file and effective config, and a short tail of the debug log if enabled.
//
// Deliberately never includes a deep link's pwd or search values, even
// though they're known to this instance -- a snapshot meant to be pasted
// into a public bug report shouldn't carry along a password or search term.
// Likewise InverseSearchCommand is reported only as set/unset, not its text.
static std::wstring BuildDiagnosticSnapshot(const ListerInstance* inst)
{
    std::wstring s = L"SumatraLister diagnostic snapshot\r\n"
                      L"==================================\r\n";
    s += L"Plugin version: " PLUGIN_VERSION L"\r\n";
#ifdef _WIN64
    s += L"Build: 64-bit (SumatraLister.wlx64)\r\n";
#else
    s += L"Build: 32-bit (SumatraLister.wlx)\r\n";
#endif
    const std::wstring& sumatraPath = GetSumatraPath();
    s += L"Detected SumatraPDF: " + (sumatraPath.empty() ? std::wstring(L"(not found)") : sumatraPath) + L"\r\n";
    s += L"SumatraPDF version: " + FormatSumatraVersion() + L"\r\n";

    if (inst) {
        std::wstring displayPath = inst->filePath;
        ExtractDeepLinkParams(displayPath); // strip any deep-link suffix, including any pwd/search it carried
        s += L"Current file: " + displayPath + L"\r\n";
        s += L"Embedded process running: "; s += (inst->pi.hProcess ? L"yes" : L"no"); s += L"\r\n";
        s += L"Invert colors active: "; s += (inst->currentInvert ? L"yes" : L"no"); s += L"\r\n";
    }

    s += L"\r\nEffective configuration:\r\n";
    s += L"  SumatraPath override: " + (g_config.sumatraPathOverride.empty() ? std::wstring(L"(auto-detect)") : g_config.sumatraPathOverride) + L"\r\n";
    s += L"  RestrictMode: "; s += (g_config.restrictMode ? L"1" : L"0"); s += L"\r\n";
    s += L"  NightMode: "; s += (g_config.nightMode ? L"1" : L"0"); s += L"\r\n";
    s += L"  AutoNightMode: "; s += (g_config.autoNightMode ? L"1" : L"0"); s += L"\r\n";
    s += L"  DefaultZoom: " + (g_config.defaultZoom.empty() ? std::wstring(L"(none)") : g_config.defaultZoom) + L"\r\n";
    s += L"  DefaultView: " + (g_config.defaultView.empty() ? std::wstring(L"(none)") : g_config.defaultView) + L"\r\n";
    s += L"  BackgroundColor: " + (g_config.backgroundColor.empty() ? std::wstring(L"(none)") : g_config.backgroundColor) + L"\r\n";
    s += L"  InverseSearchCommand: "; s += (g_config.inverseSearchCommand.empty() ? L"(not set)" : L"(set)"); s += L"\r\n";
    s += L"  EnabledExtensions: " + (g_config.enabledExtensions.empty() ? std::wstring(L"(all)") : g_config.enabledExtensions) + L"\r\n";
    s += L"  ThumbnailsEnabled: "; s += (g_config.thumbnailsEnabled ? L"1" : L"0"); s += L"\r\n";
    s += L"  EmbedTimeoutMs: " + std::to_wstring(g_config.embedTimeoutMs) + L"\r\n";
    s += L"  FallbackToShellOpen: "; s += (g_config.fallbackShellOpen ? L"1" : L"0"); s += L"\r\n";
    s += L"  InteractivePrintFallback: "; s += (g_config.interactivePrintFallback ? L"1" : L"0"); s += L"\r\n";
    s += L"  EnablePopOut: "; s += (g_config.enablePopOut ? L"1" : L"0");
    s += L" (" + g_config.popOutHotkeyStr + L")\r\n";
    s += L"  EnableDiagnosticHotkey: "; s += (g_config.enableDiagnosticHotkey ? L"1" : L"0");
    s += L" (" + g_config.diagnosticHotkeyStr + L")\r\n";
    s += L"  Per-extension overrides configured: " + std::to_wstring(g_config.perExtension.size()) + L"\r\n";
    s += L"  DebugLog: "; s += (g_config.debugLog ? L"1" : L"0"); s += L"\r\n";

    s += L"\r\n";
    if (g_config.debugLog)
        s += L"Recent log lines:\r\n" + ReadLogTail(30);
    else
        s += L"(DebugLog=0 -- enable it in SumatraLister.ini, reopen the file, and try\r\n"
             L"again for more detail here.)\r\n";

    return s;
}

// The data-gathering half of the diagnostic hotkey: builds the snapshot and
// copies it to the clipboard. Deliberately does NOT show the confirmation
// message box itself -- this must be safe to call while inst->opLock is
// held (it reads inst's fields), whereas the message box must NOT be shown
// under that lock. See ShowDiagnosticSnapshot below for why.
static bool CopyDiagnosticSnapshotToClipboard(const ListerInstance* inst)
{
    std::wstring snapshot = BuildDiagnosticSnapshot(inst);
    return SetClipboardTextW(snapshot);
}

// The confirmation half: a brief message box (this is a deliberate,
// user-initiated action where "did it work" feedback matters, unlike the
// other hotkey-forwarded actions in this file which stay silent).
//
// Deliberately takes a plain HWND, not a ListerInstance*: MessageBoxW is
// modal and blocks for as long as the user leaves it open, which is fine
// on its own but must NOT happen while inst->opLock is held, since that
// would mean any OTHER operation on this same pane -- closing it,
// navigating to the next file, a find/copy command, even pressing this
// same hotkey again -- would block on that same lock for however long the
// user leaves the dialog open. Callers must copy the snapshot (which does
// need the lock) via CopyDiagnosticSnapshotToClipboard first, release the
// lock, and only then call this.
static void ShowDiagnosticSnapshot(HWND parentForBox, bool clipboardOk)
{
    MessageBoxW(parentForBox,
                clipboardOk ? L"Diagnostic snapshot copied to clipboard. Paste it into your bug report."
                            : L"Could not access the clipboard to copy the diagnostic snapshot.",
                L"SumatraLister", static_cast<UINT>(MB_OK | (clipboardOk ? MB_ICONINFORMATION : MB_ICONWARNING)));
}

// ---------------------------------------------------------------------------
//  File-list thumbnails (Total Commander's Thumbnail view / Quick View use
//  ListGetPreviewBitmap, separately from the Lister window above). Rather
//  than spawning Sumatra per thumbnail, this uses the Windows Shell
//  thumbnail pipeline (IShellItemImageFactory) -- the SumatraPDF installer
//  registers a shell thumbnail handler for the formats it supports, and
//  modern Windows also has its own built-in PDF thumbnail handler.
// ---------------------------------------------------------------------------

static HBITMAP GetShellThumbnail(const std::wstring& path, int width, int height)
{
    if (!g_config.thumbnailsEnabled || path.empty())
        return nullptr;

    HBITMAP hbmp = nullptr;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comInitHere = SUCCEEDED(hr);

    IShellItem* item = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
        IShellItemImageFactory* factory = nullptr;
        if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&factory)))) {
            SIZE sz = { width, height };
            // SIIGBF_RESIZETOFIT keeps aspect ratio within the requested box;
            // SIIGBF_BIGGERSIZEOK lets the cache return a larger cached image
            // if a smaller exact match isn't available (faster, still scales
            // down fine for a file-list thumbnail).
            if (FAILED(factory->GetImage(sz, SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK, &hbmp)))
                hbmp = nullptr;
            factory->Release();
        }
        item->Release();
    }

    if (comInitHere) CoUninitialize();
    return hbmp;
}

// ---------------------------------------------------------------------------
//  Printing fallback (ListPrint). Used when there's no live embedded pane
//  for the file being printed -- the common case instead reuses that
//  pane's own Ctrl+P dialog (see ListPrintW).
// ---------------------------------------------------------------------------

static int DoListPrint(const std::wstring& fileToPrint, const std::wstring& printerName)
{
    const std::wstring& exe = GetSumatraPath();
    if (exe.empty() || fileToPrint.empty())
        return LISTPLUGIN_ERROR;

    std::wstring cmdLine = L"\"" + exe + L"\"";

    if (g_config.interactivePrintFallback) {
        // Interactive (default): show Sumatra's own print dialog
        // (printer/page-range/copies choice), matching the SDK's documented
        // expectation for ListPrint far more faithfully than silent
        // printing does. -exit-when-done closes Sumatra automatically once
        // the user finishes, rather than leaving a now-pointless window open.
        AppendArg(cmdLine, L"-print-dialog");
        AppendArg(cmdLine, L"-exit-when-done");
    } else {
        // Legacy silent mode: for scripted/automated printing that can't
        // have a dialog popping up unattended. -silent suppresses Sumatra's
        // own error UI, which would otherwise have nobody there to dismiss it.
        if (printerName.empty()) {
            AppendArg(cmdLine, L"-print-to-default");
        } else {
            AppendArg(cmdLine, L"-print-to");
            AppendQuotedArg(cmdLine, printerName);
        }
        AppendArg(cmdLine, L"-silent");
    }

    if (!g_config.printSettings.empty()) {
        AppendArg(cmdLine, L"-print-settings");
        AppendQuotedArg(cmdLine, g_config.printSettings);
    }
    if (g_config.restrictMode)
        AppendArg(cmdLine, L"-restrict");

    AppendQuotedArg(cmdLine, fileToPrint);

    LogF(L"Printing (%ls mode): %ls", g_config.interactivePrintFallback ? L"interactive" : L"silent", cmdLine.c_str());

    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = g_config.interactivePrintFallback ? SW_SHOWNORMAL : SW_HIDE;

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(exe.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE,
                              g_config.interactivePrintFallback ? 0 : CREATE_NO_WINDOW,
                              nullptr, nullptr, &si, &pi);
    if (!ok) {
        LogF(L"Print CreateProcess failed, error %lu", GetLastError());
        return LISTPLUGIN_ERROR;
    }

    if (g_config.interactivePrintFallback) {
        // Fire-and-forget, matching LaunchSumatraStandalone's pattern: this
        // must NOT block waiting for the user to interact with and dismiss
        // the dialog, since ListPrint is called synchronously from Total
        // Commander -- waiting for arbitrary, user-paced interaction would
        // make TC's own UI appear frozen for however long that takes.
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return LISTPLUGIN_OK;
    }

    // Silent mode: -print-to[-default] makes Sumatra print and exit on its
    // own; wait (bounded) for that exit so TC's print status reflects
    // completion. No human is involved in this path, so a short, fixed
    // timeout is meaningful here in a way it wouldn't be for the
    // interactive dialog above.
    DWORD waitResult = WaitForSingleObject(pi.hProcess, 60000);

    if (waitResult == WAIT_TIMEOUT) {
        // Sumatra didn't exit in time -- don't silently report success for
        // a print job we never actually confirmed finished. Terminate the
        // runaway process rather than just closing our handle to it, which
        // would leave it running indefinitely with nothing tracking it.
        LogF(L"Print job did not finish within 60s, terminating");
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return LISTPLUGIN_ERROR;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (waitResult != WAIT_OBJECT_0) {
        LogF(L"WaitForSingleObject on print process failed, error %lu", GetLastError());
        return LISTPLUGIN_ERROR;
    }

    return LISTPLUGIN_OK;
}

// Tears down the currently-running embedded Sumatra process (if any) and
// starts a fresh one for inst->filePath / inst->currentInvert. Currently
// used only by AutoNightMode's LC_NEWPARAMS handling in ListSendCommand,
// since there's no live way to toggle -invertcolors on an already-running
// embedded instance without restarting it. Caller must hold inst->opLock.
// Returns false (after switching the pane to the same error/fallback UI
// every other launch path uses) if the relaunch didn't succeed -- e.g.
// Sumatra was closed/crashed externally between the original embed and
// this relaunch.
static bool RelaunchSumatra(ListerInstance* inst)
{
    CloseRunningSumatraProcess(inst);
    ShowLoadingIndicator(inst->hostWnd);

    if (LaunchSumatraEmbedded(inst))
        return true;

    bool opened = TryFallbackShellOpen(inst->filePath);
    ShowErrorPane(inst->hostWnd, opened ? ERRSTATE_OPENED_EXTERNAL : ERRSTATE_NOT_FOUND);
    return false;
}

// ===========================================================================
//  Exported WLX API
// ===========================================================================

extern "C" {

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);
        // Config and Sumatra detection load lazily (std::call_once) from the
        // entry points that need them, rather than here: DllMain runs under
        // the loader lock, where file/registry I/O is best avoided.
    } else if (reason == DLL_PROCESS_DETACH) {
        if (lpReserved != nullptr) {
            // The whole process is terminating. Other threads (including any
            // thread-pool waiters) are already gone -- possibly killed while
            // holding g_mutex or an opLock, so taking either here could
            // deadlock -- and waiting on anything under the loader lock would
            // be both pointless and risky. Just make a best effort not to
            // leave embedded Sumatra processes running; nothing else can be
            // touching g_instances at this point.
            for (auto& kv : g_instances) {
                ListerInstance* inst = kv.second.get();
                if (inst->pi.hProcess)
                    TerminateProcess(inst->pi.hProcess, 0);
            }
            return TRUE;
        }

        // The DLL is being explicitly unloaded (FreeLibrary) in a still-running
        // process: do the full, orderly teardown.
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& kv : g_instances) {
            std::lock_guard<std::mutex> opLock(kv.second->opLock);
            TeardownInstance(kv.second.get());
        }
        g_instances.clear(); // drops our shared_ptr refs; frees each ListerInstance once unreferenced
    }
    return TRUE;
}

// --- ListGetDetectString --------------------------------------------------
__declspec(dllexport) void __stdcall ListGetDetectString(char* DetectString, int maxlen)
{
    LoadConfig(); // EnabledExtensions must be loaded before building this

    // Built once from kSupportedExtensions (all pure-ASCII, so the direct
    // wchar_t->char narrowing below is safe) filtered by EnabledExtensions;
    // computed on first call and reused after, since EnabledExtensions
    // can't change during the plugin's lifetime (no live INI reload).
    static const std::string detect = [] {
        std::string s;
        bool first = true;
        for (const wchar_t* ext : kSupportedExtensions) {
            if (!IsExtensionEnabled(ext))
                continue;
            if (!first) s += " | ";
            first = false;
            s += "EXT=\"";
            for (const wchar_t* p = ext; *p; p++) s += (char)*p;
            s += "\"";
        }
        return s;
    }();

    lstrcpynA(DetectString, detect.c_str(), maxlen);
}

// --- core loader, shared by ANSI and Unicode entry points ----------------
static HWND DoListLoad(HWND ParentWin, const std::wstring& fileName)
{
    LoadConfig();
    EnsureHostClassRegistered();

    RECT rc; GetClientRect(ParentWin, &rc);

    HWND host = CreateWindowExW(
        0, HOST_CLASS_NAME, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, rc.right - rc.left, rc.bottom - rc.top,
        ParentWin, nullptr, g_hInst, nullptr);

    if (!host)
        return nullptr;

    auto inst = std::make_shared<ListerInstance>();
    inst->hostWnd       = host;
    inst->parentWin     = ParentWin;
    inst->filePath      = fileName;
    // Extension detection for GetEffectiveStaticNightMode needs the actual
    // file path, not the raw fileName -- which might still carry a
    // deep-link suffix that could itself contain a dot (e.g. a
    // forwardsearch value referencing some_file.tex) or, on a real file
    // with a literal '#' in its name, might not need stripping at all.
    // Reusing ExtractDeepLinkParams' own stripping logic on a throwaway
    // copy keeps this consistent with how the suffix is actually
    // interpreted later in LaunchSumatraEmbedded.
    std::wstring extCheckPath = fileName;
    ExtractDeepLinkParams(extCheckPath); // discards the parsed params; only the stripped path is needed here
    inst->currentInvert = GetEffectiveStaticNightMode(extCheckPath); // AutoNightMode decides later via LC_NEWPARAMS

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_instances[host] = inst;
    }

    // Paint the "Opening in SumatraPDF..." indicator right now, before the
    // (potentially multi-second) embed wait below -- see ShowLoadingIndicator.
    // Registered in g_instances first so the paint handler can find the
    // instance (for dark-mode colours).
    ShowLoadingIndicator(host);

    if (!LaunchSumatraEmbedded(inst.get())) {
        bool opened = TryFallbackShellOpen(fileName);
        ShowErrorPane(host, opened ? ERRSTATE_OPENED_EXTERNAL : ERRSTATE_NOT_FOUND);
    }

    return host;
}

// --- ListLoadW (Unicode, preferred by TC) ---------------------------------
// ShowFlags (e.g. lcs_alt_open, set when the file was opened via Alt+F3 /
// Ctrl+PgDn rather than F3) is accepted per the real signature but
// intentionally unused: this plugin embeds Sumatra the same way regardless
// of how the file was opened, so there's nothing to branch on.
__declspec(dllexport) HWND __stdcall ListLoadW(HWND ParentWin, WCHAR* FileToLoad, int /*ShowFlags*/)
{
    return DoListLoad(ParentWin, FileToLoad ? FileToLoad : L"");
}

// --- ListLoad (ANSI fallback for older Total Commander builds) -----------
__declspec(dllexport) HWND __stdcall ListLoad(HWND ParentWin, char* FileToLoad, int /*ShowFlags*/)
{
    return DoListLoad(ParentWin, AnsiToWide(FileToLoad));
}

// --- ListLoadNextW / ListLoadNext: reuse same window for next/prev file --
__declspec(dllexport) int __stdcall ListLoadNextW(HWND /*ParentWin*/, HWND ListWin, WCHAR* FileToLoad, int /*ShowFlags*/)
{
    LoadConfig();

    ListerInstancePtr inst = FindInstance(ListWin);
    if (!inst)
        return LISTPLUGIN_ERROR;

    // inst->opLock (not g_mutex) covers the actual relaunch, so a slow
    // embed here doesn't block ListSendCommand/thumbnail calls for other
    // open Lister windows. Holding a shared_ptr keeps the object alive even
    // if ListCloseWindow erases it from the map on another thread right
    // now; the `closed` check below then catches that race cleanly instead
    // of operating on a half-torn-down instance.
    std::lock_guard<std::mutex> opLock(inst->opLock);
    if (inst->closed)
        return LISTPLUGIN_ERROR;

    CloseRunningSumatraProcess(inst.get());

    inst->filePath = FileToLoad ? FileToLoad : L"";

    // The new file may have a different extension than the previous one, and
    // so a different per-extension NightMode override. Recompute the static
    // night-mode state for it -- but only when AutoNightMode is off: with it
    // on, the current state was set by TC's own dark-mode notification, which
    // is still true of the new file, and must not be reset here.
    if (!g_config.autoNightMode) {
        std::wstring extCheckPath = inst->filePath;
        ExtractDeepLinkParams(extCheckPath);
        inst->currentInvert = GetEffectiveStaticNightMode(extCheckPath);
    }

    ShowLoadingIndicator(inst->hostWnd); // also restores HostWndProc if a previous attempt left the error pane up

    if (!LaunchSumatraEmbedded(inst.get())) {
        bool opened = TryFallbackShellOpen(inst->filePath);
        ShowErrorPane(inst->hostWnd, opened ? ERRSTATE_OPENED_EXTERNAL : ERRSTATE_NOT_FOUND);
        return LISTPLUGIN_ERROR;
    }
    return LISTPLUGIN_OK;
}

__declspec(dllexport) int __stdcall ListLoadNext(HWND ParentWin, HWND ListWin, char* FileToLoad, int ShowFlags)
{
    std::wstring wpath = AnsiToWide(FileToLoad);
    return ListLoadNextW(ParentWin, ListWin, wpath.empty() ? nullptr : &wpath[0], ShowFlags);
}

// --- ListCloseWindow -------------------------------------------------------
__declspec(dllexport) void __stdcall ListCloseWindow(HWND ListWin)
{
    ListerInstancePtr inst;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_instances.find(ListWin);
        if (it == g_instances.end())
            return;
        inst = it->second;       // take our own ref before erasing
        g_instances.erase(it);   // new lookups (FindInstance) won't see this instance from here on
    }

    // Acquire opLock before tearing down: if some other thread is mid-way
    // through a relaunch (ListLoadNextW / ListSendCommand) it's holding
    // this lock right now, so we simply wait our turn. Either way,
    // TeardownInstance's `closed` guard makes this safe even if we somehow
    // raced ahead of it.
    std::lock_guard<std::mutex> opLock(inst->opLock);
    TeardownInstance(inst.get());
    // `inst` (and the ListerInstance it points to) is freed automatically
    // once this shared_ptr -- and any other thread's copy still in scope --
    // goes out of scope; no explicit delete needed.
}

// --- ListSendCommand --------------------------------------------------------
// Real command set verified against the official WLX SDK (see the
// constants block near the top of this file). TC calls this "when the
// user changes some options in Lister's menu" -- resizing, page
// navigation, zoom, and find/print all reach this plugin through other,
// more direct paths (WM_SIZE on hostWnd; normal keyboard focus once
// Sumatra's window has it; ListSearchDialog/ListSearchTextW; ListPrint).
__declspec(dllexport) int __stdcall ListSendCommand(HWND ListWin, int Command, int Parameter)
{
    LoadConfig();

    ListerInstancePtr instPtr = FindInstance(ListWin);
    if (!instPtr)
        return LISTPLUGIN_ERROR;

    std::lock_guard<std::mutex> opLock(instPtr->opLock);
    if (instPtr->closed)
        return LISTPLUGIN_ERROR;

    ListerInstance* inst = instPtr.get();

    switch (Command) {
        case LC_COPY:
            ForwardCopyToSumatra(inst);
            return LISTPLUGIN_OK;

        case LC_SELECTALL:
            ForwardSelectAllToSumatra(inst);
            return LISTPLUGIN_OK;

        case LC_SETPERCENT:
            ForwardSetPercentToSumatra(inst, Parameter);
            return LISTPLUGIN_OK;

        case LC_NEWPARAMS: {
            // Parameter is a combination of LCP_* flags. This plugin only
            // acts on the dark-mode ones (for AutoNightMode); the rest
            // (wrap/fit/ansi-ascii/center/...) describe TC's own built-in
            // text/hex/image viewer options and don't apply to an embedded
            // PDF/ebook view -- "you may ignore these parameters if they
            // don't apply to your document type" per the SDK docs.
            if (!g_config.autoNightMode)
                return LISTPLUGIN_OK;

            bool wantInvert = (Parameter & (LCP_DARKMODE | LCP_DARKMODENATIVE)) != 0;
            if (wantInvert != inst->currentInvert) {
                inst->currentInvert = wantInvert;
                LogF(L"AutoNightMode: LC_NEWPARAMS dark-mode flag %ls -> invert=%d, relaunching",
                     wantInvert ? L"set" : L"unset", wantInvert);
                // This fires from TC's own theme-change notifications rather
                // than a direct user action, so a failed relaunch is
                // surfaced via the pane's error UI (RelaunchSumatra always
                // applies that) rather than through this command's return code.
                RelaunchSumatra(inst);
            }
            return LISTPLUGIN_OK;
        }

        default:
            return LISTPLUGIN_OK; // unhandled/unrecognized commands are simply ignored
    }
}

// --- ListSearchText / ListSearchTextW: Lister's own "find" dialog ---------
__declspec(dllexport) int __stdcall ListSearchTextW(HWND ListWin, WCHAR* SearchString, int SearchParameter)
{
    if (!SearchString)
        return LISTPLUGIN_ERROR;

    ListerInstancePtr inst = FindInstance(ListWin);
    if (!inst)
        return LISTPLUGIN_ERROR;

    std::lock_guard<std::mutex> opLock(inst->opLock);
    if (inst->closed)
        return LISTPLUGIN_ERROR;

    std::wstring text = SearchString;
    ForwardFindToSumatra(inst.get(), text, SearchParameter);
    return LISTPLUGIN_OK;
}

__declspec(dllexport) int __stdcall ListSearchText(HWND ListWin, char* SearchString, int SearchParameter)
{
    // &wtext[0] is well-defined even when wtext is empty (guaranteed to
    // reference the null terminator per the standard), so this safely
    // forwards through to ListSearchTextW's own null check either way.
    std::wstring wtext = AnsiToWide(SearchString);
    return ListSearchTextW(ListWin, &wtext[0], SearchParameter);
}

// --- ListSearchDialog: plugin-native find UI, preferred over ListSearchTextW ---
// Real, optional WLX export (SDK-verified): called instead of TC showing its
// own generic search dialog. Forwarding straight to Sumatra's own find bar
// gives the user its full find UI (result highlighting, its own match-case/
// whole-word controls) rather than the clipboard-paste bridge ListSearchTextW
// has to use for TC's generic dialog. Both are implemented and coexist
// fine per the SDK docs; TC prefers this one when both are exported.
__declspec(dllexport) int __stdcall ListSearchDialog(HWND ListWin, int FindNext)
{
    ListerInstancePtr inst = FindInstance(ListWin);
    if (!inst)
        return LISTPLUGIN_ERROR; // no live pane for this window; let TC fall back to its own dialog

    std::lock_guard<std::mutex> opLock(inst->opLock);
    if (inst->closed || !inst->sumatraWnd || !IsWindow(inst->sumatraWnd))
        return LISTPLUGIN_ERROR;

    SetFocus(inst->sumatraWnd);
    if (FindNext)
        SendKeyCombo(ModNone, VK_F3);  // continue the previous search forward
    else
        SendKeyCombo(ModCtrl, 'F');    // open Sumatra's own find bar fresh

    // Per the SDK docs: never return LISTPLUGIN_ERROR here just because we
    // can't confirm whether a match was found -- ERROR specifically means
    // "please show your own dialog instead," which isn't what happened;
    // we successfully showed Sumatra's native one.
    return LISTPLUGIN_OK;
}

// --- ListGetPreviewBitmapW / ListGetPreviewBitmap: file-list thumbnails ---
__declspec(dllexport) HBITMAP __stdcall ListGetPreviewBitmapW(
    WCHAR* FileToLoad, int width, int height, char* /*contentbuf*/, int /*contentbuflen*/)
{
    LoadConfig();
    if (!FileToLoad) return nullptr;
    return GetShellThumbnail(FileToLoad, width, height);
}

__declspec(dllexport) HBITMAP __stdcall ListGetPreviewBitmap(
    char* FileToLoad, int width, int height, char* contentbuf, int contentbuflen)
{
    if (!FileToLoad) return nullptr;
    std::wstring wpath = AnsiToWide(FileToLoad);
    if (wpath.empty()) return nullptr;
    return ListGetPreviewBitmapW(&wpath[0], width, height, contentbuf, contentbuflen);
}

// --- ListPrintW / ListPrint --------------------------------------------
// Note: `Margins` (custom print margins, in MM_LOMETRIC units -- i.e.
// tenths of a millimeter, per the SDK docs) is intentionally not forwarded
// to Sumatra. Sumatra manages its own print margins via -print-settings
// rather than accepting page-margin hints from the caller, and translating
// an arbitrary RECT into Sumatra's own margin syntax without live testing
// across versions risks silently WRONG margins (clipped content) rather
// than the current, honest no-op. Set PrintSettings= in the INI if you
// need specific margins. Margins may legitimately be null; the SDK docs
// say it "may be ignored," which this plugin does either way.
__declspec(dllexport) int __stdcall ListPrintW(
    HWND ListWin, WCHAR* FileToPrint, WCHAR* DefPrinter, int /*PrintFlags*/, RECT* /*Margins*/)
{
    LoadConfig();
    if (!FileToPrint) return LISTPLUGIN_ERROR;

    // Prefer Sumatra's own native print dialog (full printer/page-range/
    // copies control) by reusing a live embedded instance for the exact
    // file being printed, if one is open in this Lister window -- this
    // matches the SDK's documented expectation that ListPrint "show a
    // print dialog, in which the user can choose what to print, and
    // select a different printer" far more faithfully than silent CLI
    // printing does. Falls back to DoListPrint when there's no live
    // matching instance to reuse.
    ListerInstancePtr inst = FindInstance(ListWin);
    if (inst) {
        std::lock_guard<std::mutex> opLock(inst->opLock);
        if (!inst->closed && inst->sumatraWnd && IsWindow(inst->sumatraWnd)) {
            std::wstring currentFile = inst->filePath;
            ExtractDeepLinkParams(currentFile); // strip any deep-link suffix so the comparison matches the real path
            if (_wcsicmp(currentFile.c_str(), FileToPrint) == 0) {
                ForwardPrintDialogToSumatra(inst.get());
                return LISTPLUGIN_OK;
            }
        }
    }

    std::wstring printer = DefPrinter ? DefPrinter : L"";
    return DoListPrint(FileToPrint, printer);
}

__declspec(dllexport) int __stdcall ListPrint(
    HWND ListWin, char* FileToPrint, char* DefPrinter, int PrintFlags, RECT* Margins)
{
    if (!FileToPrint) return LISTPLUGIN_ERROR;

    std::wstring wFile    = AnsiToWide(FileToPrint);
    std::wstring wPrinter = AnsiToWide(DefPrinter);
    if (wFile.empty()) return LISTPLUGIN_ERROR;
    return ListPrintW(ListWin, &wFile[0], wPrinter.empty() ? nullptr : &wPrinter[0], PrintFlags, Margins);
}

// --- Optional: TC calls this once at startup with persisted plugin params --
__declspec(dllexport) void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps)
{
    LoadConfig();
    // Settings live in SumatraLister.ini (next to the DLL) rather than the
    // location dps->DefaultIniName suggests -- the SDK docs explicitly
    // sanction storing plugin-specific settings "directly in that file, or
    // in that directory under a different name," so this is a supported
    // alternative, not a deviation. Logged only for diagnostics.
    if (dps)
        LogF(L"ListSetDefaultParams: interface version %lu/%lu, TC-suggested ini: %ls",
             dps->PluginInterfaceVersionHi, dps->PluginInterfaceVersionLow,
             AnsiToWide(dps->DefaultIniName).c_str());
}

} // extern "C"
