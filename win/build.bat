@echo off

REM
REM  Copyright 2008 Search Solution Corporation
REM  Copyright 2016 CUBRID Corporation
REM 
REM   Licensed under the Apache License, Version 2.0 (the "License");
REM   you may not use this file except in compliance with the License.
REM   You may obtain a copy of the License at
REM 
REM       http://www.apache.org/licenses/LICENSE-2.0
REM 
REM   Unless required by applicable law or agreed to in writing, software
REM   distributed under the License is distributed on an "AS IS" BASIS,
REM   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
REM   See the License for the specific language governing permissions and
REM   limitations under the License.
REM 

SETLOCAL

rem CUBRID build script for MS Windows.
rem
rem Requirements
rem - cmake, ant, zip, md5sum, gnu flex, bison
rem - wix for msi installer
rem - Windows 2003 or later

if NOT "%OS%"=="Windows_NT" echo "ERROR: Not supported OS" & GOTO :EOF
rem - JAVA_HOME environment variable must be set to make JDBC package
if "%JAVA_HOME%" == "" echo "ERROR: JAVA_HOME variable is not set" & GOTO :EOF

rem clear ERRORLEVEL

set SCRIPT_DIR=%~dp0
set ANT_OPTS=-Dhttps.protocols=TLSv1.1,TLSv1.2

rem set default value
set VERSION=0
set VERSION_FILE=VERSION
set BUILD_NUMBER=0
set BUILD_GENERATOR="Visual Studio 15 2017"
set BUILD_TARGET=x64
set BUILD_MODE=Release
set BUILD_TYPE=RelWithDebInfo
set CMAKE_PATH=C:\Program Files\CMake\bin\cmake.exe
set CPACK_PATH=C:\Program Files\CMake\bin\cpack.exe
set GIT_PATH=C:\Program Files\Git\bin\git.exe
set WITH_CCI=true

rem default list is all
set BUILD_LIST=ALL
rem unset BUILD_ARGS
if NOT "%BUILD_ARGS%." == "." set BUILD_ARGS=

rem set variables
call :ABSPATH "%SCRIPT_DIR%\.." SOURCE_DIR

rem unset DIST_PKGS
if NOT "%DIST_PKGS%." == "." set DIST_PKGS=

:CHECK_OPTION
if "%~1." == "."       GOTO :BUILD
set BUILD_OPTION=%1
if /I "%~1" == "/g"       set "BUILD_GENERATOR=%~2"&shift
if "%~1" == "/32"         set BUILD_TARGET=Win32
if "%~1" == "/64"         set BUILD_TARGET=x64
if /I "%~1" == "/debug"   set "BUILD_MODE=Debug" & set BUILD_TYPE=Debug
if /I "%~1" == "/release" set "BUILD_MODE=Release" & set BUILD_TYPE=RelWithDebInfo
if /I "%~1" == "/out"     set DIST_DIR=%2&shift
if "%~1" == "/h"          GOTO :SHOW_USAGE
if "%~1" == "/?"          GOTO :SHOW_USAGE
if "%~1" == "/help"       GOTO :SHOW_USAGE
if NOT "%BUILD_OPTION:~0,1%" == "/" set BUILD_ARGS=%BUILD_ARGS% %1
shift
GOTO :CHECK_OPTION



:BUILD
if NOT "%BUILD_ARGS%." == "." set BUILD_LIST=%BUILD_ARGS%
:: Remove others if ALL found
set _TMP_LIST=%BUILD_LIST:ALL= %
if NOT "%_TMP_LIST%" == "%BUILD_LIST%" set BUILD_LIST=ALL

for /f "tokens=* delims= " %%a IN ("%BUILD_LIST%") DO set BUILD_LIST=%%a
echo Build list is [%BUILD_LIST%].
set BUILD_LIST=%BUILD_LIST:ALL=BUILD DIST%
set BUILD_LIST=%BUILD_LIST:BUILD=CUBRID%

rem To package JDBC, add JDBC_PACKAGE at the following
set BUILD_LIST=%BUILD_LIST:DIST=MSI_PACKAGE ZIP_PACKAGE CCI_PACKAGE%

call :BUILD_PREPARE
if ERRORLEVEL 1 echo *** [%DATE% %TIME%] Preparing failed. & GOTO :EOF

for %%i IN (%BUILD_LIST%) DO (
  echo.
  echo [%DATE% %TIME%] Entering target [%%i]
  call :BUILD_%%i
  if ERRORLEVEL 1 echo *** [%DATE% %TIME%] Failed target [%%i] & GOTO :EOF
  echo [%DATE% %TIME%] Leaving target [%%i]
  echo.
)
echo.
echo [%DATE% %TIME%] Completed.
echo.
echo *** Summary ***
echo   Target [%BUILD_LIST%]
echo   Version [%VERSION%]
echo   Build mode [%BUILD_TARGET%/%BUILD_MODE%]
if NOT "%DIST_PKGS%." == "." (
  echo   Generated packages in [%DIST_DIR%]
  cd /d %DIST_DIR%
  for /f "delims=" %%i in ('md5sum -t %DIST_PKGS%') DO (
    echo     - %%i
  )
)
echo.
GOTO :EOF


:BUILD_PREPARE
echo Checking for requirements...
call :FINDEXEC cmake.exe CMAKE_PATH "%CMAKE_PATH%"
call :FINDEXEC cpack.exe CPACK_PATH "%CPACK_PATH%"
call :FINDEXEC git.exe GIT_PATH "%GIT_PATH%"

echo Checking for root source path [%SOURCE_DIR%]...
if NOT EXIST "%SOURCE_DIR%\src" echo Root path for source is not valid. & GOTO :EOF
if NOT EXIST "%SOURCE_DIR%\VERSION" set VERSION_FILE=VERSION-DIST

echo Checking CCI directory [%SOURCE_DIR%\cubrid-cci]...
if NOT EXIST "%SOURCE_DIR%\cubrid-cci\src" (
  echo CCI source path is not exist. It must be built for dblink
  if EXIST "%SOURCE_DIR%\.git\module\cubrid-cci" (
    "%GIT_PATH%" submodule deinit -f "%SOURCE_DIR%\cubrid-cci"
  )
  "%GIT_PATH%" submodule init "%SOURCE_DIR%\cubrid-cci"
  "%GIT_PATH%" submodule update "%SOURCE_DIR%\cubrid-cci"
)

echo Checking build number with [%SOURCE_DIR%\%VERSION_FILE%]...
for /f %%i IN (%SOURCE_DIR%\%VERSION_FILE%) DO set VERSION=%%i
if ERRORLEVEL 1 echo Cannot check build number. & GOTO :EOF
for /f "tokens=1,2,3,4 delims=." %%a IN (%SOURCE_DIR%\%VERSION_FILE%) DO (
  set MAJOR_VERSION=%%a
  set MINOR_VERSION=%%b
  set PATCH_VERSION=%%c
  set EXTRA_VERSION=%%d
)

set CUBRID_MAJOR_START_DATE=2019-12-12
if NOT "%EXTRA_VERSION%." == "." (
  for /f "tokens=1,* delims=-" %%a IN ("%EXTRA_VERSION%") DO set SERIAL_NUMBER=%%a
) else (
  if EXIST "%SOURCE_DIR%\.git" (
    for /f "delims=" %%i in ('"%GIT_PATH%" rev-list --count --after %CUBRID_MAJOR_START_DATE% HEAD') do set SERIAL_NUMBER=0000%%i
    for /f "delims=" %%i in ('"%GIT_PATH%" rev-parse HEAD') do set HASH_TAG=%%i
  ) else (
    set EXTRA_VERSION=0000-unknown
    set SERIAL_NUMBER=0000
  )
)
set SERIAL_NUMBER=%SERIAL_NUMBER:~-4%
if NOT "%HASH_TAG%." == "." set HASH_TAG=%HASH_TAG:~0,7%

if NOT "%HASH_TAG%." == "." set EXTRA_VERSION=%SERIAL_NUMBER%-%HASH_TAG%
echo Build Version is [%VERSION% (%MAJOR_VERSION%.%MINOR_VERSION%.%PATCH_VERSION%.%EXTRA_VERSION%)]
set VERSION=%MAJOR_VERSION%.%MINOR_VERSION%.%PATCH_VERSION%.%EXTRA_VERSION%
set BUILD_NUMBER=%MAJOR_VERSION%.%MINOR_VERSION%.%PATCH_VERSION%.%SERIAL_NUMBER%

set BUILD_DIR=%SOURCE_DIR%\build_%BUILD_MODE%_%BUILD_TARGET%
if NOT EXIST "%BUILD_DIR%" md %BUILD_DIR%

rem TODO move build_prefix
set BUILD_PREFIX=%SOURCE_DIR%\win\install\CUBRID_%BUILD_MODE%_%BUILD_TARGET%
echo Build install directory is [%BUILD_PREFIX%].

if "%DIST_DIR%." == "." set DIST_DIR=%BUILD_DIR%\install\Installshield
call :ABSPATH "%DIST_DIR%" DIST_DIR
echo Packages Ootput directory is [%DIST_DIR%].
if NOT EXIST "%DIST_DIR%" md %DIST_DIR%
GOTO :EOF


:BUILD_CUBRID
echo Building CUBRID in %BUILD_DIR%
cd /d %BUILD_DIR%

rem TODO: get generator from command line
if "%BUILD_TARGET%" == "Win32" (
  set CMAKE_GENERATOR=%BUILD_GENERATOR%
) ELSE (
  set CMAKE_GENERATOR="%BUILD_GENERATOR:"=% Win64"
)
"%CMAKE_PATH%" -G %CMAKE_GENERATOR% -DWITH_CCI=%WITH_CCI% -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_INSTALL_PREFIX=%BUILD_PREFIX% -DPARALLEL_JOBS=10 %SOURCE_DIR%
if ERRORLEVEL 1 (echo FAILD. & GOTO :EOF) ELSE echo OK.

"%CMAKE_PATH%" --build . --config %BUILD_TYPE% --target install
if ERRORLEVEL 1 (echo FAILD. & GOTO :EOF) ELSE echo OK.

copy %SOURCE_DIR%\README %BUILD_PREFIX%\
copy %SOURCE_DIR%\CREDITS %BUILD_PREFIX%\
copy %SOURCE_DIR%\COPYING %BUILD_PREFIX%\
GOTO :EOF


:BUILD_MSI_PACKAGE
echo Buiding msi packages in %BUILD_DIR% ...
if NOT EXIST %BUILD_DIR% echo Cannot found built directory. & GOTO :EOF
cd /d %BUILD_DIR%

if "%BUILD_TARGET%" == "Win32" (set CUBRID_PACKAGE_NAME=CUBRID-Windows-x86-%VERSION%) ELSE set CUBRID_PACKAGE_NAME=CUBRID-Windows-x64-%VERSION%
echo drop %CUBRID_PACKAGE_NAME%.msi into %DIST_DIR%
"%CPACK_PATH%" -C %BUILD_TYPE% -G WIX -B "%DIST_DIR%"
if ERRORLEVEL 1 echo FAILD. & GOTO :EOF
rmdir /s /q "%DIST_DIR%"\_CPack_Packages
echo Package created. [%DIST_DIR%\%CUBRID_PACKAGE_NAME%.msi]
set DIST_PKGS=%DIST_PKGS% %CUBRID_PACKAGE_NAME%.msi
GOTO :EOF


:BUILD_ZIP_PACKAGE
echo Buiding zip packages in %BUILD_DIR% ...
if NOT EXIST %BUILD_DIR% echo Cannot found built directory. & GOTO :EOF
cd /d %BUILD_DIR%

if "%BUILD_TARGET%" == "Win32" (set CUBRID_PACKAGE_NAME=CUBRID-Windows-x86-%VERSION%) ELSE set CUBRID_PACKAGE_NAME=CUBRID-Windows-x64-%VERSION%
echo drop %CUBRID_PACKAGE_NAME%.zip into %DIST_DIR%
"%CPACK_PATH%" -C %BUILD_TYPE% -G ZIP -B "%DIST_DIR%"
if ERRORLEVEL 1 echo FAILD. & GOTO :EOF
rmdir /s /q "%DIST_DIR%"\_CPack_Packages
echo Package created. [%DIST_DIR%\%CUBRID_PACKAGE_NAME%.zip]
set DIST_PKGS=%DIST_PKGS% %CUBRID_PACKAGE_NAME%.zip
GOTO :EOF


:BUILD_CCI_PACKAGE
if "%WITH_CCI%" == false GOTO :EOF
set CCI_VERSION=0
set CCI_VERSION_FILE=CCI-VERSION-DIST
echo Buiding CCI package in %BUILD_DIR% ...
if NOT EXIST %BUILD_DIR% echo Cannot found built directory. & GOTO :EOF
cd /d %BUILD_DIR%

if NOT EXIST "%SOURCE_DIR%\cubrid-cci\%CCI_VERSION_FILE%" echo Cannot check %CCI_VERSION_FILE% ... & GOTO :EOF

echo Checking CCI build number with [%SOURCE_DIR%\cubrid-cci\%CCI_VERSION_FILE%]...
for /f %%i IN (%SOURCE_DIR%\cubrid-cci\%CCI_VERSION_FILE%) DO set CCI_VERSION=%%i
if ERRORLEVEL 1 echo Cannot check CCI build number. & GOTO :EOF

if "%BUILD_TARGET%" == "Win32" (set CUBRID_CCI_PACKAGE_NAME=CUBRID-CCI-Windows-x86-%CCI_VERSION%) ELSE set CUBRID_CCI_PACKAGE_NAME=CUBRID-CCI-Windows-x64-%CCI_VERSION%
echo drop %CUBRID_CCI_PACKAGE_NAME%.zip into %DIST_DIR%
"%CPACK_PATH%" -C %BUILD_TYPE% -G ZIP -D CPACK_COMPONENTS_ALL="CCI" -B "%DIST_DIR%"
if ERRORLEVEL 1 echo FAILD. & GOTO :EOF
rmdir /s /q "%DIST_DIR%"\_CPack_Packages
echo Package created. [%DIST_DIR%\%CUBRID_CCI_PACKAGE_NAME%.zip]
set DIST_PKGS=%DIST_PKGS% %CUBRID_CCI_PACKAGE_NAME%.zip
GOTO :EOF

rem JDBC_PACKAGE is disabled. To enable it, please refer to the comment above BUILD_LIST
:BUILD_JDBC_PACKAGE
if NOT EXIST "%SOURCE_DIR%\cubrid-jdbc\src" echo Cannot found cubrid-jdbc submodule. & GOTO :EOF

echo Packging JDBC in %SOURCE_DIR%\cubrid-jdbc\ ...
set JDBC_VERSION=0
for /f %%i IN (%SOURCE_DIR%\cubrid-jdbc\output\VERSION-DIST) DO set JDBC_VERSION=%%i
echo drop cubrid_jdbc.jar into %DIST_DIR%
copy %SOURCE_DIR%\cubrid-jdbc\cubrid_jdbc.jar %DIST_DIR%\JDBC-%JDBC_VERSION%-cubrid.jar
if ERRORLEVEL 1 echo FAILD. & GOTO :EOF
echo Package created. [%DIST_DIR%\JDBC-%JDBC_VERSION%-cubrid.jar]
set DIST_PKGS=%DIST_PKGS% JDBC-%JDBC_VERSION%-cubrid.jar
GOTO :EOF

:ABSPATH
set %2=%~f1
GOTO :EOF

:FINDEXEC
if EXIST %3 set %2=%~3
if NOT EXIST %3 for %%X in (%1) do set FOUNDINPATH=%%~$PATH:X
if defined FOUNDINPATH set %2=%FOUNDINPATH:"=%
if NOT defined FOUNDINPATH if NOT EXIST %3 echo Executable [%1] is not found & GOTO :EOF
call echo Executable [%1] is found at [%%%2%%]
GOTO :EOF


:SHOW_USAGE
@echo.Usage: %0 [OPTION] [TARGET]
@echo.Build and package scrtip for CUBRID (with Manager server)
@echo. OPTIONS
@echo.  /G Generator       Specify a build generator (default: %BUILD_GENERATOR%)
@echo.  /32      or /64    Build 32bit or 64bit applications (default: 64)
@echo.  /Release or /Debug Build with release or debug mode (default: %BUILD_MODE%)
@echo.  /out DIR           Package output directory (default: win\install\Installshield)
@echo.  /help /h /?        Display this help message and exit
@echo.
@echo. TARGETS
@echo.  ALL                BUILD and DIST (default)
@echo.  BUILD              Build all applications
@echo.  DIST               Create all packages (msi, zip, CCI)
@echo.
@echo. Examples:
@echo.  %0                 # Build and pack all packages with default option
@echo.  %0 /32 BUILD       # 32bit release build only
@echo.  %0 /64 /Debug DIST # Create 64bit debug mode packages
GOTO :EOF


