echo off
set COMPILER_PATH=C:\Users\edwar\OneDrive\compiler_tools
set PATH=C:\Program Files\CMake\bin;
set PATH=C:\Program Files (x86)\NSIS;%PATH%
set PATH=C:\Program Files\Git\cmd;%PATH%
set PATH=%COMPILER_PATH%\mingw32\msys\1.0\bin;%PATH%
set PATH=%COMPILER_PATH%\cygwin64\bin;%PATH%
set PATH=%COMPILER_PATH%\mingw-w64\i686-7.3.0-posix-dwarf-rt_v5-rev0\mingw32\bin;%PATH%
set PATH=C:\Program Files\7-Zip;%PATH%
set PATH=%COMPILER_PATH%\curl\curl-7.71.1-win32-mingw\bin;%PATH%

mkdir cmake_build
cd cmake_build
SET VERSION=%date:~0,4%%date:~5,2%%date:~8,2%
REM %time:~0,2%%time:~3,2%%time:~6,2%
cmake -G "MinGW Makefiles" -DENGINE_VERSION=%VERSION% ../
mingw32-make -C ./
cd ..
pause