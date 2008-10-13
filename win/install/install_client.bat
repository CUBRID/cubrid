@echo off

if "%1" == "" goto exit
if "%2" == "" goto exit

set SRC_DIR=%1
set DEST_DIR=%2

mkdir %DEST_DIR%
mkdir %DEST_DIR%\bin
mkdir %DEST_DIR%\msg\
mkdir %DEST_DIR%\msg\en_US
mkdir %DEST_DIR%\msg\ko_KR.euckr
mkdir %DEST_DIR%\msg\ko_KR.utf8

copy %SRC_DIR%\cubridservice.exe          %DEST_DIR%\bin
copy %SRC_DIR%\ctrlservice.exe            %DEST_DIR%\bin
copy %SRC_DIR%\setupmanage.exe            %DEST_DIR%\bin
copy %SRC_DIR%\CUBRID_Service_tray.exe    %DEST_DIR%\bin
copy %SRC_DIR%\cubrid_odbc.dll            %DEST_DIR%\bin
copy %SRC_DIR%\CUBRIDProvider.dll         %DEST_DIR%\bin
copy %SRC_DIR%\CUBRIDProvider.tlb         %DEST_DIR%\bin

REM copy cubridtray msg file
copy %SRC_DIR%\..\..\src\win_tools\cubridtray\msg\UNITRAYEN.txt  %DEST_DIR%\msg\en_US\cubridtray.msg
copy %SRC_DIR%\..\..\src\win_tools\cubridtray\msg\UNITRAYKO.txt  %DEST_DIR%\msg\ko_KR.euckr\cubridtray.msg
copy %SRC_DIR%\..\..\src\win_tools\cubridtray\msg\UNITRAYKO.txt  %DEST_DIR%\msg\ko_KR.utf8\cubridtray.msg

