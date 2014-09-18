@echo off

if %1 == "" goto exit
if %2 == "" goto exit
if %3 == "" goto exit

set SRC_DIR=%1
set DEST_DIR=%2
set PLATFROM=%3
if exist %SRC_DIR%\..\..\gencat\gencat.exe (
  set GENCAT=%SRC_DIR%\..\..\gencat\gencat.exe
) else (
  set GENCAT=%SRC_DIR%\..\..\gencat\gencat_x64.exe
)
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
set MSG_RO_UTF8_DIR=%DEST_DIR%\msg\ro_RO.utf8

rem -------------------------------------------
rem Make directory
mkdir %DEST_DIR%
mkdir %DEST_DIR%\bin
mkdir %DEST_DIR%\lib
mkdir %DEST_DIR%\java
mkdir %DEST_DIR%\conf
mkdir %DEST_DIR%\include
mkdir %DEST_DIR%\include\dbgw3
mkdir %DEST_DIR%\include\dbgw3\sql
mkdir %DEST_DIR%\include\dbgw3\system
mkdir %DEST_DIR%\include\dbgw3\client
mkdir %DEST_DIR%\include\dbgw3\adapter
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
mkdir %MSG_RO_UTF8_DIR%
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

copy %SRC_DIR%\DBGWConnector3*.lib %DEST_DIR%\lib

rem -------------------------------------------
rem Binaries
copy %SRC_DIR%\*.exe %DEST_DIR%\bin
copy %SRC_DIR%\*.dll %DEST_DIR%\bin
copy %SRC_DIR%\*.pdb %DEST_DIR%\bin
copy %SRC_DIR%\..\..\external\dll\*.dll %DEST_DIR%\bin
copy %SRC_DIR%\..\..\external\dll\%PLATFORM%\*.dll %DEST_DIR%\bin

copy %SRC_DIR%\DBGWConnector3*.dll %DEST_DIR%\bin
copy %SRC_DIR%\DBGWConnector3*.pdb %DEST_DIR%\bin

rem -------------------------------------------
rem Conf
copy %SRC_DIR%\..\..\..\conf\*conf* %DEST_DIR%\conf
copy %SRC_DIR%\..\..\..\conf\*.txt %DEST_DIR%\conf
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
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWClient.h  %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\DBGWConnector3.h  %DEST_DIR%\include
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\Common.h  %DEST_DIR%\include\dbgw3
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\Exception.h  %DEST_DIR%\include\dbgw3
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\Lob.h  %DEST_DIR%\include\dbgw3
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\Logger.h  %DEST_DIR%\include\dbgw3
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\Value.h  %DEST_DIR%\include\dbgw3
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\ValueSet.h  %DEST_DIR%\include\dbgw3
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\SynchronizedResource.h  %DEST_DIR%\include\dbgw3
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\system\ThreadEx.h  %DEST_DIR%\include\dbgw3\system
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\system\DBGWPorting.h  %DEST_DIR%\include\dbgw3\system
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\sql\CallableStatement.h  %DEST_DIR%\include\dbgw3\sql
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\sql\Connection.h  %DEST_DIR%\include\dbgw3\sql
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\sql\DatabaseInterface.h  %DEST_DIR%\include\dbgw3\sql
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\sql\DriverManager.h  %DEST_DIR%\include\dbgw3\sql
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\sql\PreparedStatement.h  %DEST_DIR%\include\dbgw3\sql
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\sql\ResultSet.h  %DEST_DIR%\include\dbgw3\sql
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\sql\ResultSetMetaData.h  %DEST_DIR%\include\dbgw3\sql
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\sql\Statement.h  %DEST_DIR%\include\dbgw3\sql
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\client\Interface.h  %DEST_DIR%\include\dbgw3\client
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\client\Mock.h  %DEST_DIR%\include\dbgw3\client
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\client\ConfigurationObject.h  %DEST_DIR%\include\dbgw3\client
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\client\Configuration.h  %DEST_DIR%\include\dbgw3\client
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\client\ClientResultSet.h  %DEST_DIR%\include\dbgw3\client
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\client\Resource.h  %DEST_DIR%\include\dbgw3\client
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\client\QueryMapper.h  %DEST_DIR%\include\dbgw3\client
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\client\Client.h  %DEST_DIR%\include\dbgw3\client
copy %SRC_DIR%\..\..\..\src\dbgw\dbgw3\adapter\Adapter.h  %DEST_DIR%\include\dbgw3\adapter

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
copy %SRC_DIR%\..\..\..\msg\ro_RO.utf8\*.msg 		%MSG_RO_UTF8_DIR%

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

mkdir %DEST_DIR%\timezones
mkdir %DEST_DIR%\timezones\tzdata
mkdir %DEST_DIR%\timezones\tzlib
copy %SRC_DIR%\..\..\..\timezones\tzdata\*.* %DEST_DIR%\timezones\tzdata
del %DEST_DIR%\timezones\tzdata\Makefile.am
xcopy %SRC_DIR%\..\..\..\timezones\tzlib_win_%3 %DEST_DIR%\timezones\tzlib /e /c /i /f /r /y
copy %SRC_DIR%\..\..\..\src\base\timezone_lib_common.h %DEST_DIR%\timezones\tzlib\timezone_lib_common.h /y
copy %SRC_DIR%\..\..\..\src\base\tz_list.h %DEST_DIR%\timezones\tz_list.h /y
copy %SRC_DIR%\..\..\..\timezones\make_tz_%3.bat %DEST_DIR%\bin\make_tz.bat /y

echo on
for %%f in (%MSG_EN_US_DIR%,%MSG_EN_US_UTF8_DIR%,%MSG_EUCKR_DIR%,%MSG_UTFKR_DIR%,%MSG_TR_UTF8_DIR%) do %GENCAT% %%f\csql.cat %%f\csql.msg
for %%f in (%MSG_EN_US_DIR%,%MSG_EN_US_UTF8_DIR%,%MSG_EUCKR_DIR%,%MSG_UTFKR_DIR%,%MSG_TR_UTF8_DIR%) do %GENCAT% %%f\cubrid.cat %%f\cubrid.msg
for %%f in (%MSG_EN_US_DIR%,%MSG_EN_US_UTF8_DIR%,%MSG_EUCKR_DIR%,%MSG_UTFKR_DIR%,%MSG_TR_UTF8_DIR%) do %GENCAT% %%f\esql.cat %%f\esql.msg
for %%f in (%MSG_EN_US_DIR%,%MSG_EN_US_UTF8_DIR%,%MSG_EUCKR_DIR%,%MSG_UTFKR_DIR%,%MSG_TR_UTF8_DIR%) do %GENCAT% %%f\utils.cat %%f\utils.msg

for %%f in (%MSG_DE_UTF8_DIR%,%MSG_ES_UTF8_DIR%,%MSG_FR_UTF8_DIR%,%MSG_IT_UTF8_DIR%) do %GENCAT% %%f\csql.cat %%f\csql.msg
for %%f in (%MSG_DE_UTF8_DIR%,%MSG_ES_UTF8_DIR%,%MSG_FR_UTF8_DIR%,%MSG_IT_UTF8_DIR%) do %GENCAT% %%f\cubrid.cat %%f\cubrid.msg
for %%f in (%MSG_DE_UTF8_DIR%,%MSG_ES_UTF8_DIR%,%MSG_FR_UTF8_DIR%,%MSG_IT_UTF8_DIR%) do %GENCAT% %%f\esql.cat %%f\esql.msg
for %%f in (%MSG_DE_UTF8_DIR%,%MSG_ES_UTF8_DIR%,%MSG_FR_UTF8_DIR%,%MSG_IT_UTF8_DIR%) do %GENCAT% %%f\utils.cat %%f\utils.msg

for %%f in (%MSG_JA_UTF8_DIR%,%MSG_KM_UTF8_DIR%,%MSG_VI_UTF8_DIR%,%MSG_ZH_UTF8_DIR%,%MSG_RO_UTF8_DIR%) do %GENCAT% %%f\csql.cat %%f\csql.msg
for %%f in (%MSG_JA_UTF8_DIR%,%MSG_KM_UTF8_DIR%,%MSG_VI_UTF8_DIR%,%MSG_ZH_UTF8_DIR%,%MSG_RO_UTF8_DIR%) do %GENCAT% %%f\cubrid.cat %%f\cubrid.msg
for %%f in (%MSG_JA_UTF8_DIR%,%MSG_KM_UTF8_DIR%,%MSG_VI_UTF8_DIR%,%MSG_ZH_UTF8_DIR%,%MSG_RO_UTF8_DIR%) do %GENCAT% %%f\esql.cat %%f\esql.msg
for %%f in (%MSG_JA_UTF8_DIR%,%MSG_KM_UTF8_DIR%,%MSG_VI_UTF8_DIR%,%MSG_ZH_UTF8_DIR%,%MSG_RO_UTF8_DIR%) do %GENCAT% %%f\utils.cat %%f\utils.msg
:EXIT
