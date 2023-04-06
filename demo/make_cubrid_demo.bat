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

set CUBRID_DISABLE_JAVA_STORED_PROCEDURE=1
set DBNAME=demodb
if not "%1"=="" (
        set DBNAME=%1

        if exist "%2" (
                set CUBRID=%2
                set CUBRID_DATABASES=%2\Databases
        )
)

if exist %DBNAME% goto done

if NOT exist %CUBRID_DATABASES%\demodb md %CUBRID_DATABASES%\demodb
set PATH=%PATH%;%CUBRID%\cci\bin;
cd %CUBRID_DATABASES%\demodb
echo ********** Creating database %DBNAME% ...
%CUBRID%\bin\cub_admin createdb --db-volume-size=100M --log-volume-size=100M --replace %DBNAME% en_US.utf8
echo ********** Loading objects ...
%CUBRID%\bin\cub_admin loaddb %DBNAME% --schema-file=%CUBRID%\demo\demodb_schema --data-file=%CUBRID%\demo\demodb_objects --no-user-specified-name -u dba
echo ********** Makedemo complete.
goto exit

:done
echo The %DBNAME% database has already been created.
echo Use %CUBRID%\cubrid deletedb %DBNAME% to remove it.

:exit
