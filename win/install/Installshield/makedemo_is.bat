set CUBRID_DISABLE_JAVA_STORED_PROCEDURE=1
set DBNAME=demodb
if not "%1"=="" (
        set DBNAME=%1

        if exist "%2" (
                set CUBRID=%2
                set CUBRID_DATABASES=%2\Databases
                set CUBRID_CHARSET=en_US
                set CUBRID_MODE=client
        )
)

if exist %DBNAME% goto done

cd %CUBRID_DATABASES%\demodb
echo ********** Creating database %DBNAME% ...
%CUBRID%\bin\cub_admin createdb --db-volume-size=100M --log-volume-size=100M --replace %DBNAME%
echo ********** Loading objects ...
%CUBRID%\bin\cub_admin loaddb %DBNAME% --schema-file=%CUBRID%\bin\demodb_schema --data-file=%CUBRID%\bin\demodb_objects -u dba
echo ********** Makedemo complete.
goto exit

:done
echo The %DBNAME% database has already been created.
echo Use %CUBRID%\cubrid deletedb %DBNAME% to remove it.

:exit
