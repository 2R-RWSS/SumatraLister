cmake_minimum_required(VERSION 3.15)
project(SumatraLister CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This plugin only builds for Windows (it is a Total Commander WLX plugin).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC: link the C/C++ runtime statically (/MT) so the plugin has no dependency
# on the Visual C++ Redistributable being installed on the user's machine.
# Safe here because nothing owned by this DLL's CRT (heap memory, FILE*,
# etc.) ever crosses the plugin boundary -- Total Commander only receives
# window/bitmap handles and plain integers back. (Requires CMake 3.15+,
# which cmake_minimum_required above already enforces.)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(SumatraLister SHARED
    SumatraLister.cpp
    SumatraLister.def
)

# Every Windows import library the source actually uses, declared explicitly
# rather than relying on a toolchain's or generator's implicit default
# library list (MinGW's driver and Visual Studio project templates each add
# a standard set automatically, which can mask a missing entry here -- but
# a Ninja/NMake build with MSVC has no such safety net, and `version` in
# particular is in neither default list):
#   shlwapi  -- PathFindExtensionW, PathRemoveFileSpecW
#   user32   -- windowing, hotkeys, SendInput, clipboard, message boxes
#   advapi32 -- registry access for SumatraPDF detection
#   shell32  -- ShellExecuteW, SHGetFolderPathW, SHCreateItemFromParsingName
#   ole32    -- COM init for thumbnails
#   gdi32    -- SetBkMode / brushes for the host and error panes
#   version  -- GetFileVersionInfoW / VerQueryValueW for SumatraPDF version detection
target_link_libraries(SumatraLister PRIVATE shlwapi user32 advapi32 shell32 ole32 gdi32 version)

# MinGW: link libgcc/libstdc++ statically, matching the manual build commands
# in README.md. Without this the DLL imports libstdc++-6.dll and
# libgcc_s_*.dll, which Total Commander's loader won't find unless they
# happen to sit next to TC or on PATH -- the plugin would just fail to load.
# (-static additionally covers libwinpthread on posix-thread MinGW toolchains
# such as MSYS2's default.)
if(MINGW)
    target_link_options(SumatraLister PRIVATE -static)
endif()

# Produce SumatraLister.wlx for 32-bit builds and SumatraLister.wlx64 for
# 64-bit builds, matching Total Commander's plugin-bitness naming convention.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(SumatraLister PROPERTIES
        SUFFIX ".wlx64"
        OUTPUT_NAME "SumatraLister")
else()
    set_target_properties(SumatraLister PROPERTIES
        SUFFIX ".wlx"
        OUTPUT_NAME "SumatraLister")
endif()
