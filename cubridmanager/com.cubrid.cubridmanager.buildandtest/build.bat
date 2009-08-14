cd /d %~dp0
set ECLIPSE_HOME=e:\dev\eclipse
set JAVA_HOME=C:\Program Files\Java\jdk1.5.0_06

dir /b %ECLIPSE_HOME%\plugins\org.apache.ant_*>nametmp.txt
set /p ANT_HOME_Dir=<nametmp.txt
set ANT_HOME=%ECLIPSE_HOME%\plugins\%ANT_HOME_Dir%

del nametmp.txt

echo %ANT_HOME%

ant
