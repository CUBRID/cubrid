@echo off

if %1 == "" goto exit
if %2 == "" goto exit
if %3 == "" goto exit

set SRC_DIR=%1
set DEST_DIR=%2
set GENCAT=%SRC_DIR%\..\..\gencat\gencat.exe
set MSG_EN_US_DIR=%DEST_DIR%\msg\en_US
set MSG_EN_US_UTF8_DIR=%DEST_DIR%\msg\en_US.utf8
set MSG_EUCKR_DIR=%DEST_DIR%\msg\ko_KR.euckr
set MSG_UTFKR_DIR=%DEST_DIR%\msg\ko_KR.utf8
set MSG_TR_UTF8_DIR=%DEST_DIR%\msg\tr_TR.utf8
set MSG_DE_UTF8_DIR=%DEST_DIR%\msg\de_DE.utf8
set MSG_ES_UTF8_DIR=%DEST_DIR%\msg\es_ES.utf8
set MSG_FR_UTF8_DIR=%DEST_DIR%\msg\fr_FR.utf8
set MSG_IT_UTF8_DIR=%DEST_DIR%\msg\it_IT.utf8
set MSG_JA_UTF8_DIR=%DEST_DIR%\msg\ja_JP.utf8
set MSG_KM_UTF8_DIR=%DEST_DIR%\msg\km_KH.utf8
set MSG_VI_UTF8_DIR=%DEST_DIR%\msg\vi_VN.utf8
set MSG_ZH_UTF8_DIR=%DEST_DIR%\msg\zh_CN.utf8

rem -------------------------------------------
rem Make directory
mkdir %DEST_DIR%
mkdir %DEST_DIR%\bin
mkdir %DEST_DIR%\lib
mkdir %DEST_DIR%\java
mkdir %DEST_DIR%\conf
mkdir %DEST_DIR%\include
mkdir %DEST_DIR%\log
mkdir %DEST_DIR%\msg
mkdir %MSG_EN_US_DIR%
mkdir %MSG_EN_US_UTF8_DIR%
mkdir %MSG_EUCKR_DIR%
mkdir %MSG_UTFKR_DIR%
mkdir %MSG_TR_UTF8_DIR%
mkdir %MSG_DE_UTF8_DIR%
mkdir %MSG_ES_UTF8_DIR%
mkdir %MSG_FR_UTF8_DIR%
mkdir %MSG_IT_UTF8_DIR%
mkdir %MSG_JA_UTF8_DIR%
mkdir %MSG_KM_UTF8_DIR%
mkdir %MSG_VI_UTF8_DIR%
mkdir %MSG_ZH_UTF8_DIR%
mkdir %DEST_DIR%\tmp
mkdir %DEST_DIR%\var

rem -------------------------------------------
rem Libraries
copy %SRC_DIR%\cubridcs.lib %DEST_DIR%\lib
copy %SRC_DIR%\cubridsa.lib %DEST_DIR%\lib
copy %SRC_DIR%\cascci*.lib %DEST_DIR%\lib
copy %SRC_DIR%\libesql.lib %DEST_DIR%\lib
copy %SRC_DIR%\cmstat.lib %DEST_DIR%\lib
copy %SRC_DIR%\cmdep.lib %DEST_DIR%\lib

copy %SRC_DIR%\DBGWConnector3.lib %DEST_DIR%\lib

rem -------------------------------------------
rem Binaries
copy %SRC_DIR%\*.exe %DEST_DIR%\bin
copy %SRC_DIR%\*.dll %DEST_DIR%\bin
copy %SRC_DIR%\..\..\external\dll\*.dll %DEST_DIR%\bin

copy %SRC_DIR%\DBGWConnector3.dll %DEST_DIR%\bin

rem -------------------------------------------
rem Conf
copy %SRC_DIR%\..\..\..\conf\*conf* %DEST_DIR%\conf
copy %SRC_DIR%\..\..\..\conf\*.txt %DEST_DIR%\conf
copy %SRC_DIR%\..\..\..\conf\cubrid_*.xml %DEST_DIR%\conf
copy %SRC_DIR%\..\..\..\cmserver\conf\*.conf %DEST_DIR%\conf
copy %SRC_DIR%\..\..\..\cmserver\conf\*.pass %DEST_DIR%\conf

rem -------------------------------------------
rem Headers
copy %SRC_DIR%\..\..\..\src\cci\cas_cci.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\broker\cas_error.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\executables\cubrid_esql.h %DEST_DIR%\include\cubrid_esql.h
copy %SRC_DIR%\..\..\..\src\compat\dbi_compat.h %DEST_DIR%\include\dbi.h
copy %SRC_DIR%\..\..\..\src\cm_common\cm_stat.h %DEST_DIR%\include\cm_stat.h
copy %SRC_DIR%\..\..\..\src\cm_common\cm_dep.h %DEST_DIR%\include\cm_dep.h

copy %SRC_DIR%\..\..\..\src\cci\cci_log.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWMock.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWCommon.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWError.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWValue.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWDataBaseInterface.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWPorting.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWAdapter.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWQuery.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWLogger.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWConfigurationFwd.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWConfiguration.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWWorkFwd.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWWork.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWClientFwd.h %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWClient.h %DEST_DIR%\include

rem -------------------------------------------
rem Messages
copy %SRC_DIR%\..\..\..\msg\en_US\*.msg 		%MSG_EN_US_DIR%
copy %SRC_DIR%\..\..\..\msg\en_US.utf8\*.msg 		%MSG_EN_US_UTF8_DIR%
copy %SRC_DIR%\..\..\..\msg\ko_KR.euckr\*.msg 		%MSG_EUCKR_DIR%
copy %SRC_DIR%\..\..\..\msg\ko_KR.utf8\*.msg	 	%MSG_UTFKR_DIR%
copy %SRC_DIR%\..\..\..\msg\tr_TR.utf8\*.msg 		%MSG_TR_UTF8_DIR%
copy %SRC_DIR%\..\..\..\msg\de_DE.utf8\*.msg 		%MSG_DE_UTF8_DIR%
copy %SRC_DIR%\..\..\..\msg\es_ES.utf8\*.msg 		%MSG_ES_UTF8_DIR%
copy %SRC_DIR%\..\..\..\msg\fr_FR.utf8\*.msg 		%MSG_FR_UTF8_DIR%
copy %SRC_DIR%\..\..\..\msg\it_IT.utf8\*.msg 		%MSG_IT_UTF8_DIR%
copy %SRC_DIR%\..\..\..\msg\ja_JP.utf8\*.msg 		%MSG_JA_UTF8_DIR%
copy %SRC_DIR%\..\..\..\msg\km_KH.utf8\*.msg 		%MSG_KM_UTF8_DIR%
copy %SRC_DIR%\..\..\..\msg\vi_VN.utf8\*.msg 		%MSG_VI_UTF8_DIR%
copy %SRC_DIR%\..\..\..\msg\zh_CN.utf8\*.msg 		%MSG_ZH_UTF8_DIR%

mkdir %DEST_DIR%\locales
mkdir %DEST_DIR%\locales\data
mkdir %DEST_DIR%\locales\data\ldml
mkdir %DEST_DIR%\locales\data\codepages
mkdir %DEST_DIR%\locales\loclib
copy %SRC_DIR%\..\..\..\locales\data\*.txt %DEST_DIR%\locales\data
copy %SRC_DIR%\..\..\..\locales\data\ldml\*.xml %DEST_DIR%\locales\data\ldml
copy %SRC_DIR%\..\..\..\locales\data\codepages\*.txt %DEST_DIR%\locales\data\codepages
xcopy %SRC_DIR%\..\..\..\locales\loclib_win_%3 %DEST_DIR%\locales\loclib /e /c /i /f /r /y
copy %SRC_DIR%\..\..\..\src\base\locale_lib_common.h %DEST_DIR%\locales\loclib\locale_lib_common.h /y
copy %SRC_DIR%\..\..\..\locales\make_locale_%3.bat %DEST_DIR%\bin\make_locale.bat /y

del %DEST_DIR%\bin\migrate_r40beta2ga.exe

echo on
for %%f in (%MSG_EN_US_DIR%,%MSG_EN_US_UTF8_DIR%,%MSG_EUCKR_DIR%,%MSG_UTFKR_DIR%,%MSG_TR_UTF8_DIR%) do %GENCAT% %%f\csql.cat %%f\csql.msg
for %%f in (%MSG_EN_US_DIR%,%MSG_EN_US_UTF8_DIR%,%MSG_EUCKR_DIR%,%MSG_UTFKR_DIR%,%MSG_TR_UTF8_DIR%) do %GENCAT% %%f\cubrid.cat %%f\cubrid.msg
for %%f in (%MSG_EN_US_DIR%,%MSG_EN_US_UTF8_DIR%,%MSG_EUCKR_DIR%,%MSG_UTFKR_DIR%,%MSG_TR_UTF8_DIR%) do %GENCAT% %%f\esql.cat %%f\esql.msg
for %%f in (%MSG_EN_US_DIR%,%MSG_EN_US_UTF8_DIR%,%MSG_EUCKR_DIR%,%MSG_UTFKR_DIR%,%MSG_TR_UTF8_DIR%) do %GENCAT% %%f\utils.cat %%f\utils.msg

for %%f in (%MSG_DE_UTF8_DIR%,%MSG_ES_UTF8_DIR%,%MSG_FR_UTF8_DIR%,%MSG_IT_UTF8_DIR%) do %GENCAT% %%f\csql.cat %%f\csql.msg
for %%f in (%MSG_DE_UTF8_DIR%,%MSG_ES_UTF8_DIR%,%MSG_FR_UTF8_DIR%,%MSG_IT_UTF8_DIR%) do %GENCAT% %%f\cubrid.cat %%f\cubrid.msg
for %%f in (%MSG_DE_UTF8_DIR%,%MSG_ES_UTF8_DIR%,%MSG_FR_UTF8_DIR%,%MSG_IT_UTF8_DIR%) do %GENCAT% %%f\esql.cat %%f\esql.msg
for %%f in (%MSG_DE_UTF8_DIR%,%MSG_ES_UTF8_DIR%,%MSG_FR_UTF8_DIR%,%MSG_IT_UTF8_DIR%) do %GENCAT% %%f\utils.cat %%f\utils.msg

for %%f in (%MSG_JA_UTF8_DIR%,%MSG_KM_UTF8_DIR%,%MSG_VI_UTF8_DIR%,%MSG_ZH_UTF8_DIR%) do %GENCAT% %%f\csql.cat %%f\csql.msg
for %%f in (%MSG_JA_UTF8_DIR%,%MSG_KM_UTF8_DIR%,%MSG_VI_UTF8_DIR%,%MSG_ZH_UTF8_DIR%) do %GENCAT% %%f\cubrid.cat %%f\cubrid.msg
for %%f in (%MSG_JA_UTF8_DIR%,%MSG_KM_UTF8_DIR%,%MSG_VI_UTF8_DIR%,%MSG_ZH_UTF8_DIR%) do %GENCAT% %%f\esql.cat %%f\esql.msg
for %%f in (%MSG_JA_UTF8_DIR%,%MSG_KM_UTF8_DIR%,%MSG_VI_UTF8_DIR%,%MSG_ZH_UTF8_DIR%) do %GENCAT% %%f\utils.cat %%f\utils.msg
:EXIT
