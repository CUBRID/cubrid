@echo off
REM %1 : conffile , %2 : dist_file , %3 : version_file , %4 : keep old conf
set CONF_FILE=%1
set SRC_FILE=%2
set VER_FILE=%3
set KEEP_OLD_CONF=%4

if "%KEEP_OLD_CONF%"=="TRUE" (
    if not exist %CONF_FILE% (
		copy %SRC_FILE% %CONF_FILE%
	)
    move %SRC_FILE% %VER_FILE%
) else (
    if exist %CONF_FILE% (
        move %CONF_FILE% %CONF_FILE%.bak
	)
    move %SRC_FILE% %CONF_FILE%
)

