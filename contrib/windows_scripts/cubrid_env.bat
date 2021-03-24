@echo off
rem batch script for CUBRID Environments, (window services, registry)

rem LOADING CUBRID Environments
echo Setting CUBRID Environments


set CUBRID "C:\CUBRID"
set CUBRID_DATABASES "%CUBRID%\DATABASES"

reg add "HKEY_LOCAL_MACHINE\SOFTWARE\CUBRID\cmserver" /v "ROOT_PATH" /t REG_SZ /d "%CUBRID%\\" /f
reg add "HKEY_LOCAL_MACHINE\SOFTWARE\CUBRID\cmserver" /v "Version" /t REG_SZ /d "11.0" /f
reg add "HKEY_LOCAL_MACHINE\SOFTWARE\CUBRID\CUBRID" /v "ROOT_PATH" /t REG_SZ /d "%CUBRID%\\" /f
reg add "HKEY_LOCAL_MACHINE\SOFTWARE\CUBRID\CUBRID" /v "Version" /t REG_SZ /d "11.0" /f
reg add "HKEY_LOCAL_MACHINE\SOFTWARE\CUBRID\CUBRID" /v "Patch" /t REG_SZ /d "0" /f

reg add "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v "CUBRID" /t REG_SZ /d "%CUBRID%\\" /f
reg add "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v "CUBRID_DATABASES" /t REG_SZ /d "%CUBRID%\databases" /f
reg add "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v "Path" /t REG_SZ /d "%CUBRID%\bin;%PATH%" /f

echo %CUBRID%
echo %CUBRID_DATABASES%
echo %Path%