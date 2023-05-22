@echo off

REM
REM
REM Copyright 2016 CUBRID Corporation
REM
REM Licensed under the Apache License, Version 2.0 (the "License");
REM you may not use this file except in compliance with the License.
REM You may obtain a copy of the License at
REM
REM http://www.apache.org/licenses/LICENSE-2.0
REM
REM Unless required by applicable law or agreed to in writing, software
REM distributed under the License is distributed on an "AS IS" BASIS,
REM WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
REM See the License for the specific language governing permissions and
REM limitations under the License.
REM

rem batch script for CUBRID Environments, (window services, registry)

rem LOADING CUBRID Environments
echo Setting CUBRID Environments


set CUBRID=C:\CUBRID
set CUBRID_DATABASES=%CUBRID%\databases

reg add "HKEY_LOCAL_MACHINE\SOFTWARE\CUBRID\cmserver" /v "ROOT_PATH" /t REG_SZ /d "%CUBRID%\\" /f
reg add "HKEY_LOCAL_MACHINE\SOFTWARE\CUBRID\cmserver" /v "Version" /t REG_SZ /d "11.0" /f
reg add "HKEY_LOCAL_MACHINE\SOFTWARE\CUBRID\CUBRID" /v "ROOT_PATH" /t REG_SZ /d "%CUBRID%\\" /f
reg add "HKEY_LOCAL_MACHINE\SOFTWARE\CUBRID\CUBRID" /v "Version" /t REG_SZ /d "11.0" /f
reg add "HKEY_LOCAL_MACHINE\SOFTWARE\CUBRID\CUBRID" /v "Patch" /t REG_SZ /d "0" /f

reg add "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v "CUBRID" /t REG_SZ /d "%CUBRID%\\" /f
reg add "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v "CUBRID_DATABASES" /t REG_SZ /d "%CUBRID_DATABASES%" /f
reg add "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v "Path" /t REG_SZ /d "%CUBRID%\cci\bin;%CUBRID%\bin;%PATH%" /f

echo %CUBRID%
echo %CUBRID_DATABASES%
echo %Path%
