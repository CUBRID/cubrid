@echo off
REM %1 : conffile , %2 : dist_file , %3 : version_file
set CONF_FILE=%1
set SRC_FILE=%2
set VER_FILE=%3


if not exist %CONF_FILE% (
		copy %SRC_FILE% %CONF_FILE%
		)

move %SRC_FILE% %VER_FILE%


