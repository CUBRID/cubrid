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

if not "%1"=="" (
  set CUBRID=%1
  set CUBRID_DATABASES=%1\Databases
)

if NOT exist %CUBRID_DATABASES% md %CUBRID_DATABASES%
if NOT exist %CUBRID_DATABASES%\databases.txt type NUL > %CUBRID_DATABASES%\databases.txt

:exit
