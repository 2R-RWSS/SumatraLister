@ECHO OFF
CLS
ECHO This batch file builds the X86 and X64 builds of SumatraLister.
cmake -B build64 -A x64
cmake --build build64 --config Release
cmake -B build32 -A Win32
cmake --build build32 --config Release
GOTO END
:END
@ECHO ON
