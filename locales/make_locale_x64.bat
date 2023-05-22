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
REM  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

set APP_NAME=%0
echo %APP_NAME%

set BUILD_TARGET=x64
set BUILD_MODE=.
set SELECTED_LOCALE=.
set LOCALE_PARAM=

set VCVARS=bin\amd64\vcvarsamd64.bat

:CHECK_OPTION
if "%1." == "." (GOTO :CHECK_ENV)

if "%1" == "/debug" (
    if "%BUILD_MODE%" == "." (
        set BUILD_MODE=debug
        GOTO :DO_SHIFT
    ) else (
        GOTO :ERROR_BUILD_MODE
    )
)
if "%1" == "/release" (
    if "%BUILD_MODE%" == "." (
        set BUILD_MODE=release
        GOTO :DO_SHIFT
    ) else (
        GOTO :ERROR_BUILD_MODE
    )
)

if "%1" == "/h"            (GOTO :SHOW_USAGE)
if "%1" == "/?"            (GOTO :SHOW_USAGE)
if "%1" == "/help"         (GOTO :SHOW_USAGE)
if NOT "%2" == "" (GOTO :ERROR)
if "%SELECTED_LOCALE%" == "." (
    set SELECTED_LOCALE=%1
    GOTO :CHECK_ENV
)

GOTO :ERROR

:DO_SHIFT
shift
GOTO :CHECK_OPTION

:CHECK_ENV
if "%SELECTED_LOCALE%" == "." (
    set SELECTED_LOCALE=all_locales
) else (
    if "%SELECTED_LOCALE:~0,1%" == "-" goto :ERROR
    set LOCALE_PARAM=%SELECTED_LOCALE%
) 

if "%BUILD_MODE%" == "." set BUILD_MODE=release

@echo. Matching environment with the selected options...

@echo. Searching for Visual Studio installs...

@rem 1. search Visual Studio installation (from newest to oldest)
@echo checking Visual Studio 2017 v150... VS150COMNTOOLS = "%VS150COMNTOOLS%"
if defined VS150COMNTOOLS (
    @echo. Found installation for Visual Studio 2017 150: "%VS150COMNTOOLS%"
    if exist "%VS150COMNTOOLS%VsDevCmd.bat" (
        echo Found %BUILD_TARGET% configuration in Visual Studio 2017_150 Community.
        @echo call "%VS150COMNTOOLS%VsDevCmd.bat" -arch=amd64
        call "%VS150COMNTOOLS%VsDevCmd.bat" -arch=amd64
        goto :BUILD
    )
    @rem configuration for this VS version didn't work, try older VS versions...
)
@echo checking Visual Studio 2017 v150... VS150COMCOMNTOOLS = "%VS150COMCOMNTOOLS%"
if defined VS150COMCOMNTOOLS (
    @echo. Found installation for Visual Studio 2017 150: "%VS150COMCOMNTOOLS%"
    if exist "%VS150COMCOMNTOOLS%VsDevCmd.bat" (
        echo Found %BUILD_TARGET% configuration in Visual Studio 2017_150 Community.
        @echo call "%VS150COMCOMNTOOLS%VsDevCmd.bat" -arch=amd64
        call "%VS150COMCOMNTOOLS%VsDevCmd.bat" -arch=amd64
        goto :BUILD
    )
    @rem configuration for this VS version didn't work, try older VS versions...
)
@echo checking Visual Studio 2017 v140... VS140COMNTOOLS = "%VS140COMNTOOLS%"
if defined VS140COMNTOOLS (
    @echo. Found installation for Visual Studio 2017 v140: "%VS140COMNTOOLS%"
    @echo checking "%VS140COMNTOOLS%..\..\..\..\2017\Community\Common7\Tools\VsDevCmd.bat"
    if exist "%VS140COMNTOOLS%..\..\..\..\2017\Community\Common7\Tools\VsDevCmd.bat" (
        echo Found %BUILD_TARGET% configuration in Visual Studio 2017 Community.
        call "%VS140COMNTOOLS%..\..\..\..\2017\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64
        goto :BUILD
    )
    @echo checking "%VS140COMNTOOLS%..\..\..\..\2017\Professional\Common7\Tools\VsDevCmd.bat"
    if exist "%VS140COMNTOOLS%..\..\..\..\2017\Professional\Common7\Tools\VsDevCmd.bat" (
        echo Found %BUILD_TARGET% configuration in Visual Studio 2017 Professional.
        @echo call "%VS140COMNTOOLS%..\..\..\..\2017\Professional\Common7\Tools\VsDevCmd.bat" -arch=amd64
        call "%VS140COMNTOOLS%..\..\..\..\2017\Professional\Common7\Tools\VsDevCmd.bat" -arch=amd64
        goto :BUILD
    )
    @echo checking "%VS140COMNTOOLS%..\..\..\..\2017\Enterprise\Common7\Tools\VsDevCmd.bat"
    if exist "%VS140COMNTOOLS%..\..\..\..\2017\Enterprise\Common7\Tools\VsDevCmd.bat" (
        echo Found %BUILD_TARGET% configuration in Visual Studio 2017 Enterprise.
        @echo call "%VS140COMNTOOLS%..\..\..\..\2017\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=amd64
        call "%VS140COMNTOOLS%..\..\..\..\2017\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=amd64
        goto :BUILD
    )
    @rem configuration for this VS version didn't work, try older VS versions...
)
@echo checking Visual Studio 2010... VS100COMNTOOLS = "%VS100COMNTOOLS%"
if defined VS100COMNTOOLS (
    @echo. Found installation for Visual Studio 2010
    @echo checking "%VS100COMNTOOLS%..\..\VC\%VCVARS%"
    if exist "%VS100COMNTOOLS%..\..\VC\%VCVARS%" (
        echo Found %BUILD_TARGET% configuration in Visual Studio 2010.
        @echo call "%VS100COMNTOOLS%..\..\VC\%VCVARS%"
        call "%VS100COMNTOOLS%..\..\VC\%VCVARS%"
        goto :BUILD
    )
    @rem configuration for this VS version didn't work, try older VS versions...
)
@echo checking Visual Studio 2008... VS90COMNTOOLS = "%VS90COMNTOOLS%"
if defined VS90COMNTOOLS (
    @echo. Found installation for Visual Studio 2008
    @echo checking "%VS90COMNTOOLS%..\..\VC\%VCVARS%"
    if exist "%VS90COMNTOOLS%..\..\VC\%VCVARS%" (
        @echo. Found %BUILD_TARGET% configuration in Visual Studio 2008.
        @echo call "%VS90COMNTOOLS%..\..\VC\%VCVARS%"
        call "%VS90COMNTOOLS%..\..\VC\%VCVARS%"
        goto :BUILD
    )
    @rem configuration for this VS version didn't work, try older VS versions...
)
@echo checking Visual Studio 2005... VS80COMNTOOLS = "%VS80COMNTOOLS%"
if defined VS80COMNTOOLS (
    @echo. Found installation for Visual Studio 2005
    @echo checking "%VS80COMNTOOLS%..\..\VC\%VCVARS%"
    if exist "%VS80COMNTOOLS%..\..\VC\%VCVARS%" (
        echo Found %BUILD_TARGET% configuration in Visual Studio 2005.
        @echo call "%VS80COMNTOOLS%..\..\VC\%VCVARS%"
        call "%VS80COMNTOOLS%..\..\VC\%VCVARS%"
        goto :BUILD
    )
    @rem configuration for this VS version didn't work, try older VS versions...
)
@echo. ERROR: no valid Visual Studio installation found
goto :ENV_ERROR

@rem 2. build
:BUILD
@echo. Running %APP_NAME% with parameters:
@echo.         BUILD_TARGET = %BUILD_TARGET%
@echo.         BUILD_MODE = %BUILD_MODE%
@echo.         SELECTED_LOCALE = %SELECTED_LOCALE%
@echo. 

@echo. Generating locale C file for %SELECTED_LOCALE% ...
@echo cubrid genlocale %LOCALE_PARAM%
cubrid genlocale %LOCALE_PARAM%

if %ERRORLEVEL% NEQ 0 goto :ERROR_GENLOCALE

echo Compiling locale library ... using:
@where cl.exe
@where link.exe

mkdir %CUBRID%\locales\loclib\Output
set CURRENT_DIR=%CD%
cd /D %CUBRID%\locales\loclib
@echo nmake -f makefile %BUILD_MODE%
nmake -f makefile %BUILD_MODE%
echo Done.
cd /D %CURRENT_DIR%

if not exist "%CUBRID%\locales\loclib\Output\loclib.dll" goto :BUILD_FAILED

echo Copying %CUBRID%\locales\loclib\Output\loclib.dll to %CUBRID%\lib\libcubrid_%SELECTED_LOCALE%.dll ...
copy %CUBRID%\locales\loclib\Output\loclib.dll %CUBRID%\lib\libcubrid_%SELECTED_LOCALE%.dll /y
echo Done.

echo Removing build folder %CUBRID%\locales\loclib\Output...
rmdir /S /Q %CUBRID%\locales\loclib\Output
echo Done.

echo Removing locale C source file %CUBRID%\locales\loclib\locale.c ...
del /Q %CUBRID%\locales\loclib\locale.c
echo Done!

echo The library for the selected locale(s) has been created at %CUBRID%\lib\libcubrid_%SELECTED_LOCALE%.dll
echo To check compatibility and synchronize your existing databases, run:
echo "cubrid synccolldb <database-name>"

goto :eof

:ERROR_TARGET
@echo. Target already set to %BUILD_TARGET%
GOTO :SHOW_USAGE

:ERROR_BUILD_MODE
@echo. Build mode already set to %BUILD_MODE%
GOTO :SHOW_USAGE

:ERROR
@echo. Invalid parameter %1
GOTO :SHOW_USAGE

:ERROR_GENLOCALE
@echo. ERROR : cubrid genlocale %LOCALE_PARAM% failed!
goto :GENERIC_ERROR

:ENV_ERROR
@echo. ERROR: Tools and configuration for %BUILD_TARGET% were not found.
goto :GENERIC_ERROR

:BUILD_FAILED
@echo. ERROR: Locale DLL compile failed!
goto :GENERIC_ERROR

:SHOW_USAGE
@echo.
@echo.USAGE: %APP_NAME% [/release^|/debug] [LOCALE]
@echo.Build locale shared 64bit DLL for CUBRID
@echo. OPTIONS
@echo.  /release or /debug    Build with release or debug mode (default: release)
@echo.  LOCALE                The locale name for which to build the library.
@echo.                        (Ommit param to build all configured locales.)
@echo.  /help /h /?           Display this help message and exit
@echo.
@echo. Examples:
@echo.  %APP_NAME%              # Build and pack all locales (64bit/release)
@echo.  %APP_NAME% de_DE        # Create release library for de_DE (German) locale
@echo.  %APP_NAME% /debug       # Create debug mode library with all locales
@echo.  %APP_NAME% /debug de_DE # Create debug mode library for de_DE locale

:GENERIC_ERROR
if exist %CUBRID%\locales\loclib\Output (
    rmdir /S /Q %CUBRID%\locales\loclib\Output
)
if exist %CUBRID%\locales\loclib\locale.c (
    del /Q %CUBRID%\locales\loclib\locale.c
)

:eof
