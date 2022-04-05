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
