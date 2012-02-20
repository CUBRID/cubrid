@echo off

REM Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
REM
REM   This program is free software; you can redistribute it and/or modify 
REM   it under the terms of the GNU General Public License as published by 
REM   the Free Software Foundation; either version 2 of the License, or
REM   (at your option) any later version. 
REM
REM  This program is distributed in the hope that it will be useful, 
REM  but WITHOUT ANY WARRANTY; without even the implied warranty of 
REM  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
REM  GNU General Public License for more details. 
REM
REM  You should have received a copy of the GNU General Public License 
REM  along with this program; if not, write to the Free Software 
REM  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

SETLOCAL

rem CUBRID build script for MS Windows.
rem
rem Requirements
rem - Cygwin with make, ant, zip, md5sum
rem - Windows 2003 or later

if NOT "%OS%"=="Windows_NT" echo "ERROR: Not supported OS" & GOTO :EOF
rem - JAVA_HOME environment variable must be set to make JDBC package
if "%JAVA_HOME%" == "" echo "ERROR: JAVA_HOME variable is not set" & GOTO :EOF

rem clear ERRORLEVEL

set SCRIPT_DIR=%~dp0

rem set default value
set BUILD_NUMBER=0
set BUILD_TARGET=Win32
set BUILD_MODE=Release
set DEVENV_PATH=C:\Program Files\Microsoft Visual Studio 9.0\Common7\IDE\devenv.com
set IS_PATH=D:\IS_2010\System\IsCmdBld.exe
rem default list is all
set BUILD_LIST=ALL
rem unset BUILD_ARGS
if NOT "%BUILD_ARGS%." == "." set BUILD_ARGS=
%COMSPEC% /c

rem set variables
call :ABSPATH "%SCRIPT_DIR%\.." SOURCE_DIR
set BUILD_DIR=%SOURCE_DIR%\win

rem unset DIST_PKGS
if NOT "%DIST_PKGS%." == "." set DIST_PKGS=

:CHECK_OPTION
if "%1." == "."       GOTO :BUILD
set BUILD_OPTION=%1
if "%1" == "/32"      set BUILD_TARGET=Win32
if "%1" == "/64"      set BUILD_TARGET=x64
if "%1" == "/debug"   set BUILD_MODE=Debug
if "%1" == "/release" set BUILD_MODE=Release
if "%1" == "/out"     set DIST_DIR=%2&shift
if "%1" == "/h"       GOTO :SHOW_USAGE
if "%1" == "/?"       GOTO :SHOW_USAGE
if "%1" == "/help"    GOTO :SHOW_USAGE
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
set BUILD_LIST=%BUILD_LIST:BUILD=CUBRID JDBC MANAGER%
set BUILD_LIST=%BUILD_LIST:DIST=EXE_PACKAGE ZIP_PACKAGE CCI_PACKAGE JDBC_PACKAGE%

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
echo   Version [%BUILD_NUMBER%]
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
echo Checking for root source path [%SOURCE_DIR%]...
if NOT EXIST "%SOURCE_DIR%\src" echo Root path for source is not valid. & GOTO :EOF
if NOT EXIST "%SOURCE_DIR%\BUILD_NUMBER" echo Root path for source is not valid. & GOTO :EOF

echo Checking for Visual Studio [%DEVENV_PATH%]...
if NOT EXIST "%DEVENV_PATH%" echo Visual Studio compiler not found. & GOTO :EOF

echo Checking for InstallShield [%IS_PATH%]...
if NOT EXIST "%IS_PATH%" echo InstallShield not found. & GOTO :EOF

echo Checking build number with [%SOURCE_DIR%\BUILD_NUMBER]...
rem for /f "delims=" %%i IN ('type %SOURCE_DIR%\BUILD_NUMBER') DO set BUILD_NUMBER=%%i
for /f %%i IN (%SOURCE_DIR%\BUILD_NUMBER) DO set BUILD_NUMBER=%%i
if ERRORLEVEL 1 echo Cannot check build number. & GOTO :EOF
echo Build Number is [%BUILD_NUMBER%]

set BUILD_PREFIX=%BUILD_DIR%\install\CUBRID_%BUILD_MODE%_%BUILD_TARGET%
echo Build install directory is [%BUILD_PREFIX%].

if "%DIST_DIR%." == "." set DIST_DIR=%BUILD_DIR%\install\Installshield
call :ABSPATH "%DIST_DIR%" DIST_DIR
echo Packages Ootput directory is [%DIST_DIR%].
if NOT EXIST "%DIST_DIR%" md %DIST_DIR%
GOTO :EOF


:BUILD_CUBRID
echo Building CUBRID in %BUILD_DIR%
cd /d %BUILD_DIR%

rem build gencat win32 version when build target is x64 on win32 host
if NOT DEFINED ProgramFiles(x86) (
  if "%BUILD_TARGET%" == "x64" (
    "%DEVENV_PATH%" /rebuild "Release|Win32" "gencat/gencat.vcproj"
    if ERRORLEVEL 1 (echo FAILD. & GOTO :EOF) ELSE echo OK.
  )
)

"%DEVENV_PATH%" /rebuild "%BUILD_MODE%|%BUILD_TARGET%" "cubrid.sln"
if ERRORLEVEL 1 (echo FAILD. & GOTO :EOF) ELSE echo OK.

"%DEVENV_PATH%" /rebuild "%BUILD_MODE%|%BUILD_TARGET%" "cubrid_client.sln"
if ERRORLEVEL 1 (echo FAILD. & GOTO :EOF) ELSE echo OK.

"%DEVENV_PATH%" /rebuild "%BUILD_MODE%|%BUILD_TARGET%" "cubrid_compat.sln"
if ERRORLEVEL 1 (echo FAILD. & GOTO :EOF) ELSE echo OK.

copy %BUILD_DIR%\%BUILD_TARGET%\Release\convert_password.exe %BUILD_PREFIX%\bin

copy %SOURCE_DIR%\README %BUILD_PREFIX%\
copy %SOURCE_DIR%\CREDITS %BUILD_PREFIX%\
copy %SOURCE_DIR%\COPYING %BUILD_PREFIX%\
GOTO :EOF


:BUILD_JDBC
echo Buiding JDBC in %BUILD_DIR%...
cd /d %BUILD_DIR%

if EXIST %BUILD_PREFIX%\jdbc rd /s /q %BUILD_PREFIX%\jdbc
md %BUILD_PREFIX%\jdbc
cd %SOURCE_DIR%\src\jdbc
make clean
make
if ERRORLEVEL 1 echo FAILD. & GOTO :EOF

copy JDBC-%BUILD_NUMBER%-cubrid.jar %BUILD_PREFIX%\jdbc\cubrid_jdbc.jar
if ERRORLEVEL 1 echo FAILD. & GOTO :EOF

rem build jspserver
make -f Makefile_jspserver clean
make -f Makefile_jspserver
if ERRORLEVEL 1 echo FAILD. & GOTO :EOF

copy jspserver.jar %BUILD_PREFIX%\java
copy logging.properties %BUILD_PREFIX%\java
if ERRORLEVEL 1 echo FAILD. & GOTO :EOF
GOTO :EOF


:BUILD_MANAGER
if NOT EXIST %BUILD_PREFIX% echo Cannot found built directory. & GOTO :EOF
set CMSERVER_DIR=%SOURCE_DIR%\cubridmanager\server
if NOT EXIST %CMSERVER_DIR% echo "ERROR: cubridmanager\server not found in %SOURCE_DIR%" & GOTO :EOF
echo Buiding Manager server in %CMSERVER_DIR% ...

set CUBRID_LIBDIR=%BUILD_PREFIX%\lib
set CUBRID_INCLUDEDIR=%BUILD_PREFIX%\include

rem build cmserver with build.bat
cd /d %CMSERVER_DIR%
if "%BUILD_TARGET%" == "Win32" (
    call build.bat --prefix %BUILD_PREFIX% --with-cubrid-libdir %CUBRID_LIBDIR% --with-cubrid-includedir %CUBRID_INCLUDEDIR%
) ELSE (
    call build.bat --prefix %BUILD_PREFIX% --with-cubrid-libdir %CUBRID_LIBDIR% --with-cubrid-includedir %CUBRID_INCLUDEDIR% --enable-64bit
)

rem build cmserver directly
rem cd /d %CMSERVER_DIR%\win
rem "%DEVENV_PATH%" /rebuild "%BUILD_MODE%|%BUILD_TARGET%" /project install "cmserver.sln" 
rem if ERRORLEVEL 1 echo FAILD. & GOTO :EOF
rem copy install\CMServer_%BUILD_MODE%_%BUILD_TARGET%\bin\*.exe %BUILD_PREFIX%\bin\
rem copy install\CMServer_%BUILD_MODE%_%BUILD_TARGET%\conf\*.* %BUILD_PREFIX%\conf\
rem End build cmserver directly

if ERRORLEVEL 1 echo FAILD. & GOTO :EOF
GOTO :EOF


:BUILD_EXE_PACKAGE
echo Buiding exe packages in %BUILD_DIR% ...
if NOT EXIST %BUILD_PREFIX% echo Cannot found built directory. & GOTO :EOF
cd /d %BUILD_PREFIX%

copy /y conf\cubrid.conf conf\cubrid.conf-dist
copy /y conf\cubrid_broker.conf conf\cubrid_broker.conf-dist
copy /y conf\cm.conf conf\cm.conf-dist
copy /y conf\cmdb.pass conf\cmdb.pass-dist
copy /y conf\cm.pass conf\cm.pass-dist

cd /d %BUILD_DIR%\install\Installshield
if "%BUILD_TARGET%" == "Win32" (set CUBRID_ISM="CUBRID.ism") ELSE set CUBRID_ISM="CUBRID_x64.ism"

rem run installshield
"%IS_PATH%" -p %CUBRID_ISM% -r "CUBRID" -c COMP -x
if NOT ERRORLEVEL 0 echo FAILD. & GOTO :EOF
if ERRORLEVEL 1 echo FAILD. & GOTO :EOF

if "%BUILD_TARGET%" == "Win32" (set CUBRID_PACKAGE_NAME=CUBRID-Windows-x86-%BUILD_NUMBER%) ELSE set CUBRID_PACKAGE_NAME=CUBRID-Windows-x64-%BUILD_NUMBER%
echo drop %CUBRID_PACKAGE_NAME%.exe into %DIST_DIR%
move /y packaging\product\Package\CUBRID.EXE %DIST_DIR%\%CUBRID_PACKAGE_NAME%.exe
if ERRORLEVEL 1 echo FAILD. & GOTO :EOF

cd /d %BUILD_PREFIX%
del conf\cubrid.conf-dist
del conf\cubrid_broker.conf-dist
del conf\cm.conf-dist
del conf\cmdb.pass-dist
del conf\cm.pass-dist
echo Package created. [%DIST_DIR%\%CUBRID_PACKAGE_NAME%.exe]
set DIST_PKGS=%DIST_PKGS% %CUBRID_PACKAGE_NAME%.exe
GOTO :EOF


:BUILD_ZIP_PACKAGE
echo Buiding zip packages in %BUILD_DIR% ...
if NOT EXIST %BUILD_PREFIX% echo Cannot found built directory. & GOTO :EOF
cd /d %BUILD_PREFIX%

if NOT EXIST databases md databases

if "%BUILD_TARGET%" == "Win32" (set CUBRID_PACKAGE_NAME=CUBRID-Windows-x86-%BUILD_NUMBER%) ELSE set CUBRID_PACKAGE_NAME=CUBRID-Windows-x64-%BUILD_NUMBER%
echo drop %CUBRID_PACKAGE_NAME%.zip into %DIST_DIR%
zip -r %DIST_DIR%\%CUBRID_PACKAGE_NAME%.zip *
if ERRORLEVEL 1 echo FAILD. & GOTO :EOF
echo Package created. [%DIST_DIR%\%CUBRID_PACKAGE_NAME%.zip]
set DIST_PKGS=%DIST_PKGS% %CUBRID_PACKAGE_NAME%.zip
GOTO :EOF


:BUILD_CCI_PACKAGE
echo Buiding CCI package in %BUILD_DIR% ...
if NOT EXIST %BUILD_PREFIX% echo Cannot found built directory. & GOTO :EOF
cd /d %BUILD_PREFIX%

if EXIST cubrid-cci-%BUILD_NUMBER% rd /s /q cubrid-cci-%BUILD_NUMBER%
md cubrid-cci-%BUILD_NUMBER%\include
md cubrid-cci-%BUILD_NUMBER%\lib

copy %BUILD_PREFIX%\include\cas_cci.h cubrid-cci-%BUILD_NUMBER%\include
copy %BUILD_PREFIX%\include\cas_error.h cubrid-cci-%BUILD_NUMBER%\include
copy %BUILD_PREFIX%\bin\cascci.dll cubrid-cci-%BUILD_NUMBER%\lib
copy %BUILD_PREFIX%\lib\cascci.lib cubrid-cci-%BUILD_NUMBER%\lib
if "%BUILD_TARGET%" == "Win32" (set CUBRID_CCI_PACKAGE_NAME=CUBRID-CCI-Windows-x86-%BUILD_NUMBER%) ELSE set CUBRID_CCI_PACKAGE_NAME=CUBRID-CCI-Windows-x64-%BUILD_NUMBER%
echo drop CUBRID_CCI_PACKAGE_NAME%.zip into %DIST_DIR%
zip -r %DIST_DIR%\%CUBRID_CCI_PACKAGE_NAME%.zip cubrid-cci-%BUILD_NUMBER%
if ERRORLEVEL 1 echo FAILD. & GOTO :EOF
echo Package created. [%DIST_DIR%\%CUBRID_CCI_PACKAGE_NAME%.zip]
set DIST_PKGS=%DIST_PKGS% %CUBRID_CCI_PACKAGE_NAME%.zip
GOTO :EOF


:BUILD_JDBC_PACKAGE
echo Buiding JDBC package in %BUILD_DIR%...
if NOT EXIST %BUILD_PREFIX%\jdbc\cubrid_jdbc.jar echo Cannot found built jar. & GOTO :EOF

echo drop JDBC-%BUILD_NUMBER%-cubrid.jar into %DIST_DIR%
copy %BUILD_PREFIX%\jdbc\cubrid_jdbc.jar %DIST_DIR%\JDBC-%BUILD_NUMBER%-cubrid.jar
if ERRORLEVEL 1 echo FAILD. & GOTO :EOF
echo Package created. [%DIST_DIR%\JDBC-%BUILD_NUMBER%-cubrid.jar]
set DIST_PKGS=%DIST_PKGS% JDBC-%BUILD_NUMBER%-cubrid.jar
GOTO :EOF


:ABSPATH
set %2=%~f1
GOTO :EOF


:SHOW_USAGE
@echo.Usage: %0 [OPTION] [TARGET]
@echo.Build and package scrtip for CUBRID (with Manager server)
@echo. OPTIONS
@echo.  /32      or /64    Build 32bit or 64bit applications (default: 32bit)
@echo.  /Release or /Debug Build with release or debug mode (default: Release)
@echo.  /out DIR           Package output directory (default: win\install\Installshield)
@echo.  /help /h /?        Display this help message and exit
@echo.
@echo. TARGETS
@echo.  ALL                BUILD and DIST (default)
@echo.  BUILD              Build all applications
@echo.  DIST               Create all packages (exe, zip, jar, CCI)
@echo.
@echo. Examples:
@echo.  %0                 # Build and pack all packages (32/release)
@echo.  %0 /32 BUILD       # 64bit release build only
@echo.  %0 /64 /Debug DIST # Create 64bit debug mode packages
GOTO :EOF

