@echo off

if "%1" == "" goto exit
if "%2" == "" goto exit

set SRC_DIR=%1
set DEST_DIR=%2
set GENCAT=%SRC_DIR%\..\..\gencat\gencat.exe
set MSG_EN_US_DIR=%DEST_DIR%\msg\en_US
set MSG_EUCKR_DIR=%DEST_DIR%\msg\ko_KR.euckr
set MSG_UTFKR_DIR=%DEST_DIR%\msg\ko_KR.utf8

mkdir %DEST_DIR%
mkdir %DEST_DIR%\bin
mkdir %DEST_DIR%\lib
mkdir %DEST_DIR%\java
mkdir %DEST_DIR%\conf
mkdir %DEST_DIR%\include
mkdir %DEST_DIR%\log
mkdir %DEST_DIR%\msg
mkdir %MSG_EN_US_DIR%
mkdir %MSG_EUCKR_DIR%
mkdir %MSG_UTFKR_DIR%
mkdir %DEST_DIR%\tmp
mkdir %DEST_DIR%\var

copy %SRC_DIR%\cubridcs.lib %DEST_DIR%\lib
copy %SRC_DIR%\cubridsa.lib %DEST_DIR%\lib
copy %SRC_DIR%\cascci*.lib %DEST_DIR%\lib
copy %SRC_DIR%\libesql.lib %DEST_DIR%\lib
copy %SRC_DIR%\cmstat.lib %DEST_DIR%\lib
copy %SRC_DIR%\cmdep.lib %DEST_DIR%\lib

copy %SRC_DIR%\*.exe %DEST_DIR%\bin
copy %SRC_DIR%\*.dll %DEST_DIR%\bin
copy %SRC_DIR%\..\..\external\dll\*.dll %DEST_DIR%\bin

copy %SRC_DIR%\..\..\..\conf\*.conf %DEST_DIR%\conf
copy %SRC_DIR%\..\..\..\cmserver\conf\*.conf %DEST_DIR%\conf
copy %SRC_DIR%\..\..\..\cmserver\conf\*.pass %DEST_DIR%\conf

copy %SRC_DIR%\..\..\..\src\cci\cas_cci.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\broker\cas_error.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\executables\cubrid_esql.h %DEST_DIR%\include\cubrid_esql.h
copy %SRC_DIR%\..\..\..\src\compat\dbi_compat.h %DEST_DIR%\include\dbi.h
copy %SRC_DIR%\..\..\..\src\cm_common\cm_stat.h %DEST_DIR%\include\cm_stat.h
copy %SRC_DIR%\..\..\..\src\cm_common\cm_dep.h %DEST_DIR%\include\cm_dep.h

copy %SRC_DIR%\..\..\..\msg\en_US\*.msg 		%MSG_EN_US_DIR%
copy %SRC_DIR%\..\..\..\msg\en_US\syntax.txt	%MSG_EN_US_DIR%
copy %SRC_DIR%\..\..\..\msg\ko_KR.euckr\*.msg 	%MSG_EUCKR_DIR%
copy %SRC_DIR%\..\..\..\msg\ko_KR.euckr\syntax.txt	%MSG_EUCKR_DIR%
copy %SRC_DIR%\..\..\..\msg\ko_KR.utf8\*.msg 	%MSG_UTFKR_DIR%
copy %SRC_DIR%\..\..\..\msg\ko_KR.utf8\syntax.txt 	%MSG_UTFKR_DIR%

echo on
for %%f in (%MSG_EN_US_DIR%,%MSG_EUCKR_DIR%,%MSG_UTFKR_DIR%) do "%GENCAT%" "%%f\csql.cat" "%%f\csql.msg"
for %%f in (%MSG_EN_US_DIR%,%MSG_EUCKR_DIR%,%MSG_UTFKR_DIR%) do "%GENCAT%" "%%f\cubrid.cat" "%%f\cubrid.msg"
for %%f in (%MSG_EN_US_DIR%,%MSG_EUCKR_DIR%,%MSG_UTFKR_DIR%) do "%GENCAT%" "%%f\esql.cat" "%%f\esql.msg"
for %%f in (%MSG_EN_US_DIR%,%MSG_EUCKR_DIR%,%MSG_UTFKR_DIR%) do "%GENCAT%" "%%f\utils.cat" "%%f\utils.msg"

:EXIT
