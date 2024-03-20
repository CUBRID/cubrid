
$
$  Copyright 2008 Search Solution Corporation
$  Copyright 2016 CUBRID Corporation
$ 
$   Licensed under the Apache License, Version 2.0 (the "License");
$   you may not use this file except in compliance with the License.
$   You may obtain a copy of the License at
$ 
$       http://www.apache.org/licenses/LICENSE-2.0
$ 
$   Unless required by applicable law or agreed to in writing, software
$   distributed under the License is distributed on an "AS IS" BASIS,
$   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
$   See the License for the specific language governing permissions and
$   limitations under the License.
$ 
$ 
$

$
$ $Id$
$
$ utils.msg
$
$ The following set numbers have been assigned:
$
$       1       generic
$       2       backupdb
$       3       copydb
$       4       createdb
$       5       dbstatlink
$       6       deletedb
$       7       renamedb
$       9       master
$       10      optimizedb
$       11      restoredb
$       12      loaddb
$       13      unloaddb
$       14      compactdb
$       15      commdb
$       16      pathdb
$       17      addvoldb
$       18      checkdb
$       19      spacedb
$       20      estimatedb_data
$       21      estimatedb_index
$       22      installdb
$       23      migdb
$       24      diagdb
$       25      lockdb
$       26      killtran
$       27      restartevnt
$       28      prestartldb
$       29      shutdownldb
$       30      mqueueldb
$       31      migrate_v73
$       32      load_audit_trail
$       33      alterdbhost
$       37      plandump
$       38      paramdump
$       39      changemode
$       40      copylogdb
$       41      applylogdb
$       43      statdump
$       44      applyinfo
$       45      acldb
$       46      genlocale
$       47      dumplocale
$       48      synccolldb
$       49      tranlist
$       51      gen_tz
$       52      dump_tz
$       53      restoreslave
$       54      delvoldb
$       56      checksumdb
$       57      tde
$       58      flashback
$       59      memmon

$set 1 MSGCAT_UTIL_SET_GENERIC
1 Longitud o caracter '%1$c' en el nombre de la base de datos '%2$s'\n no valida.
2 No se puede abrir archivo de salida '%1$s'\n
3 Nombre de utilidad falta o no valido.\n
4 Nombre de utilidad no valido '%1$s'\n
5 La siguente lista contiene los nombres de utilidad reconocidos:\n
6 Caracter no valido '%1$c' en nombre de volumen '%2$s'\n
9 \
%1$s (CUBRID utilidades)\n\
%2$s\n\
Copyright (C) 2008 Search Solution Corporation.\nCopyright (C) 2016 CUBRID Corporation.\n
10 \
utilidad de administrador de cubrid, version %1$s\n\
uso: %2$s <nombre-de-utilidad> [args]\n\
Escriba '%3$s <nombre-de-utilidad>' para ayuda sobre una utilidad especifica.\n\
\n\
Nombre de utilidad admin disponible:\n\
    createdb\n\
    deletedb\n\
    installdb\n\
    renamedb\n\
    copydb\n\
    backupdb\n\
    restoredb\n\
    addvoldb\n\
    spacedb\n\
    lockdb\n\
    tranlist\n\
    killtran\n\
    optimizedb\n\
    compactdb\n\
    diagdb\n\
    checkdb\n\
    alterdbhost\n\
    plandump\n\
    loaddb\n\
    unloaddb\n\
    paramdump\n\
    statdump\n\
    changemode\n\
    copylogdb\n\
    applylogdb\n\
    applyinfo\n\
    acldb\n\
    genlocale\n\
    dumplocale\n\
    synccolldb\n\
    gen_tz\n\
    dump_tz\n\
    restoreslave\n\
    vacuumdb\n\
    tde\n\
    flashback\n\
\n\
%4$s es una hieramenta para DBMS.\n\
Para informacion adicional, vea http://www.cubrid.org\n
12 '%1$s' nombre de utilidad es invalido.\nEscriba 'cubrid --help' para ver uso.\n
13 '%1$s' comando es invalido.\nEscriba 'cubrid --help' para ver uso.\n
14 No se pueden cargar una o más propiedades obligatorias en los archivos de configuración.\n
15 @ %1$s %2$s: %3$s\n
16 @ %1$s %2$s\n
17 ++ %1$s '%2$s' no se esta ejecutando.\n
18 ++ %1$s no se esta ejecutando.\n
19 ++ %1$s '%2$s' se esta ejecutando.\n
20 ++ %1$s se esta ejecutando.\n
21 ++ %1$s %2$s: %3$s\n
22 Uno o mas argumentos faltan.\nEscriba 'cubrid --help' para ver uso.\n
23 utilidad de cubrid, version %1$s\n\
uso: %2$s <nombre-de-utilidad> [args]\n\
Escriba '%3$s <nombre-de-utilidad>' para ayuda sobre una utilidad especifica.\n\
\n\
Utilidades de servicio disponible:\n\
    service\n\
    server\n\
    broker\n\
    gateway\n\
    manager\n\
    heartbeat\n\
    javasp\n\
\n\
Utilidades de administrador disponible:\n\
    addvoldb\n\
    alterdbhost\n\
    backupdb\n\
    checkdb\n\
    compactdb\n\
    copydb\n\
    createdb\n\
    deletedb\n\
    diagdb\n\
    installdb\n\
    tranlist\n\
    killtran\n\
    loaddb\n\
    lockdb\n\
    optimizedb\n\
    plandump\n\
    renamedb\n\
    restoredb\n\
    restoreslave\n\
    spacedb\n\
    unloaddb\n\
    paramdump\n\
    statdump\n\
    changemode\n\
    applyinfo\n\
    genlocale\n\
    dumplocale\n\
    synccolldb\n\
    gen_tz\n\
    dump_tz\n\
    vacuumdb\n\
    checksumdb\n\
    tde\n\
    flashback\n\
\n\
%4$s es una hieramenta para DBMS.\n\
Para informacion adicional, vea http://www.cubrid.org\n
24 utilidad de cubrid, version %1$s\n\
uso: %2$s service <command>\n\
\n\
Comando disponible:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 utilidad de cubrid, version %1$s\n\
uso: %2$s server <command> [args]\n\
\n\
Comando disponible:\n\
    start       [database-name]\n\
    stop        [database-name]\n\
    restart     [database-name]\n\
    status\n\
    acl         <status|reload> database-name\n
37 El servidor ha sido configurado para HA.\n
26 utilidad de cubrid, version %1$s\n\
uso: %2$s broker <command> [args]\n\
\n\
Comando disponible:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [-S] [-P] [-m] [-c] [-u] [<expr>]\n\
    on          broker-name\n\
    off         broker-name\n\
    reset       broker-name\n\
    acl         <status|reload> [broker-name]\n\
    getid       -b <broker-name> [-f] shard-key\n\
    test        broker-name [-D <database_name>] [-u <user_name>] [-p <user_password>] [-c <SQL_command>] [-i <input_file>] [-o <output_file>] [-v] [-s]\n
27 utilidad de cubrid, version %1$s\n\
uso: %2$s manager <command>\n\
\n\
Comando disponible: \n\
    start\n\
    stop\n\
    status\n
28 utilidad de cubrid, version %1$s\n\
uso: %2$s heartbeat <command> [args]\n\
\n\
Comando disponible: \n\
    start          [database-name]\n\
    stop           [-h <host-name>] [-i] [database-name]\n\
    copylogdb      <start|stop> [-h <host-name>] database-name node-name\n\
    applylogdb     <start|stop> [-h <host-name>] database-name node-name\n\
    replication    <start|stop> node-name\n\
    status         [-v] [-h <host-name>]\n\
    reload\n
31 Argumento '%1$s' no es necesario.\n
32 Falta de un nombre de base de datos.\n
33 %1$s: Tal opcion es depreciada.\n
34 Parametro no valido. %1$s %2$s\n
35 No se puede asignar memoria.\n
36 El servidor no ha sido configurado para HA.\n
38 El servidor configurado para HA no es listado en ha_db_list.\n
39 El servidor configurado para HA no es listado en ha_node_list.\n
40 Comando invalido.\n
41 El servidor CUBRID manager no es instalado.\n
42 Argumento invalido.\n
43 Error al abrir archivo (%1$s).\n
44 utilidad de cubrid, version %1$s\n\
uso: %2$s javasp <command> [args]\n\
\n\
Comando disponible:\n\
    start       [database-name]\n\
    stop        [database-name]\n\
    restart     [database-name]\n\
    status      [database-name]\n
45 cubrid utility, version %1$s\n\
usage: %2$s gateway <command> [args]\n\
\n\
Available command:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [-c] [-u] [<expr>]\n\
    on          gateway-name\n\
    off         gateway-name\n\
    reset       gateway-name\n\
    acl         <status|reload> [gateway-name]\n
46 La longitud de nombre_propietario debe ser inferior a %1$d y la longitud de nombre_clase debe ser inferior a %2$d bytes.\n
47 Formato no válido para nombre_propietario.nombre_clase ('%1$s').\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 ADVERTENCIA: -t opcion es ignorada en modo de ejecucion independiente.\n
31 ERROR: Calle de destino no existe o no es un directorio.\n
32 Al usar la opción --separate-keys, separa el archivo de claves (_keys) del volumen de respaldo.\n\
Si se pierde el archivo de claves, es posible que la restauración (restoredb) no funcione correctamente incluso si existe el volumen de copia de seguridad.\n
33 El volumen de respaldo incluye el archivo de claves (_keys).\n\
Si una tabla está encriptada con TDE, puede causar un problema de seguridad para esos datos.\n\
Para separar el archivo de claves para evitarlo, use la opción --separate-keys.\n
34 ERROR: No se puede usar el archivo FIFO como ruta de destino con una clave separada.\n
60 \
backupdb: Respaldar una base de datos.\n\
uso: %1$s backupdb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -D, --destination-path=PATH  almacenar volumenes de respaldo al directorio de RAIZ; estandar: directorio de registro de la base de datos\n\
  -r, --remove-archive         eliminar carpetas de registro no necesidatas; usar con cuidado y consultar manual\n\
  -l, --level=LEVEL            nivel de respaldo; estandar: nivel completo\n\
                               NIVEL es permitido:\n\
                                   0 - completo\n\
                                   1 - incremental 1\n\
                                   2 - incremental 2\n\
  -o, --output-file=FILE       imprimir mensajes de respaldo detalados al ARCHIVO\n\
  -S, --SA-mode                modo de ejecucion independiente\n\
  -C, --CS-mode                modo de ejecucion cliente-servidor\n\
      --no-check               no verificar una consistencia de la base de datos; estandar: verificar\n\
  -t, --thread-count=COUNT     numero de hilos; estandar: 0(auto)\n\
      --no-compress            no comprima los volúmenes de respaldo; predeterminado: compresión\n\
      --sleep-msecs=N          dormir N milisecondas para cada 1M leido estandar: 0\n\
  -k, --separate-keys          el archivo de claves (_keys) para TDE no se incluye en el volumen de respaldo, sino que se divide en un archivo\n
71 Argumentos incompatibles, \"%1$s\", y \"%2$s\"\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 ERROR: Nombre de destino es identico con el nombre de origen.\n
31 ERROR: volext-pathname no existe o no se puede escribir.\n
32 ERROR: vols-tofrom-paths_file no existe o no se puede leer.\n
60 \
copydb: Copiar una base de datos.\n\
uso: %1$s copydb [OPTION] src-database-name dest-database-name\n\
\n\
opciones validas:\n\
      --server-name=HOST          nombre de HOST del servidor; estandar: nombre de host actual\n\
  -F, --file-path=PATH            RUTA de directorio para volumenes de base de datos; estandar: directorio actual\n\
  -L, --log-path=PATH             RUTA de directorio para volumenes de registro; estandar: la misma que la ruta de archivo\n\
  -B, --lob-base-path=PATH        RUTA de directorio de base para datos LOB externos; estandar: <file-path>/lob\n\
  -E, --extended-volume-path=PATH RUTA de directorio para volumenes de informacion extendidos; estandar: la misma ruta que los volumenes de base de datos o la ruta dada en el archivo de control\n\
  -i, --control-file=FILE         copiar volumenes sobre varias ubicaciones escritas en el archivo de control; estandar: ninguno\n\
  -r, --replace                   sustituir una base de datos existente; estandar: no sustituir\n\
  -d, --delete-source             eliminar una base de datos de origen; estandar: no eliminar\n\
      --copy-lob-path             copiar ruta de almacenamiento externa para datos LOB de la base de datos de origen. no puede ser utilizado con la opcion -B; estandar: no copiar\n


$set 4 MSGCAT_UTIL_SET_CREATEDB
40 Grupo '%1$s' no existe\n
41 Utilizador '%1$s' no existe\n
42 Comando desconocido '%1$s'\n
43 No se puede abrir archivo de salida '%1$s'\n
44 No se puede eliminar base de datos existente.\n
45 Creando base de datos de tamano %1$s utilizando la localizacion estandar %2$s. La cantidad total de espacio en disco necesaria es %3$s. \n
46 No se pudo crear base de datos.\n
47 Problema con el archivo de utilizador '%1$s'\n
48 Para el '%1$s' dado, %2$s es fuera de rango. Escoger un tamano entre %3$s y %4$s.\n
49 El valor de %1$s '%2$s' no es valido.\n
60 \
createdb: Crear una base de datos.\n\
usage: %1$s createdb [OPTION] database-name database-locale\n\
\n\
El formato para la localizacion de la base de datos es <idioma>.<charset>; ejemplos: en_US.iso88591, ko_KR.utf8\n\
opciones validas:\n\
      --db-volume-size=SIZE           tamano del primer volumen de informacion\n\
                                      estandar: db_volume_size en cubrid.conf\n\
      --db-page-size=SIZE             TAMANO de pagina de base de datos en bytes; estandar: 16K\n\
      --log-volume-size=SIZE          tamano de carpetas de registro\n\
                                      estandar: log_volume_size en cubrid.conf\n\
      --log-page-size=SIZE            TAMANO de pagina de registro en bytes; estandar: tamano de pagina de base de datos\n\
      --comment=COMMENT               COMENTARIO para una base de datos; estandar: ninguno\n\
  -F, --file-path=PATH                RUTA de directorio para volumenes de base de datos; estandar: directorio actual\n\
  -L, --log-path=PATH                 RUTA de directorio para volumenes de registro; estandar: la misma que la ruta de archivo\n\
  -B, --lob-base-path=PATH            RUTA de directorio de base para datos LOB externos; estandar: <file-path>/lob\n\
      --server-name=HOST              nombre HOST del servidor; estandar: nombre de host actual\n\
  -r, --replace                       sustituir una base de datos existente; estandar: no sustituir\n\
      --more-volume-file=FILE         ARCHIVO contiene especificaciones para la creacion de volumenes adicionales; estandar: ninguna\n\
      --user-definition-file=FILE     ARCHIVO contiene definiciones de utilizador; estandar: ninguna\n\
      --csql-initialization-file=FILE ARCHIVO contiene SQLs para una inicializacion de la base de datos; estandar: ningun\n\
  -o, --output-file=FILE              redireccionar mensajes de salida al ARCHIVO; estandar: ninguno\n\
  -v, --verbose                       habilitar mensajes de estado verbose; estandar: inhabilitado\n


$set 6 MSGCAT_UTIL_SET_DELETEDB
60 \
deletedb: Eliminar una base de datos.\n\
uso: %1$s deletedb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -o, --output-file=FILE       redireccionar mensajes de salida al ARCHIVO; estandar: ninguno\n\
  -d, --delete-backup          eliminar volumenes de respaldo; estandar: no eliminar\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 ERROR: volext-pathname no existe o no se puede escribir.\n
32 ERROR: vols-tofrom-paths_file no existe o no se puede leer.\n
60 \
renamedb: Renombrar una base de datos.\n\
uso: %1$s renamedb [OPTION] src-database-name dest-database-name\n\
\n\
opciones validas:\n\
  -E, --extended-volume-path=PATH RUTA de directorio para volumenes de informacion extendidos; estandar: la misma ruta que la de los volumenes de base de datos o la ruta dada en el archivo de control\n\
  -i, --control-file=FILE         renombrar volumenes sobre varias ubicaciones escritas en el archivo de control; estandar: ninguno\n\
  -d, --delete-backup             eliminar volumenes de respaldo; estandar: no eliminar\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 Ejecutando ya un proceso %1$s.\n
12 SERVIDOR MAESTRO inicializando. \n
13 SERVIDOR MAESTRO saliendo.\n
14 Error al encontrar nombre de host local\n
15 No se puede encontrar archivo de parametros\n
16 No se puede inicializar proceso maestro\n
17 Servidor %1$s apagando en %2$d minutos
18 Servidor %1$s notificado del cierre.\nEsto puede durar unos minutos. Por favor espere.
19 Server %1$s no encontrado.
20 Maestro apagando en %1$d minutos\n
21 La función de 'heartbeat' está desactivada. Espere hasta que se hayan completado todos los procesos de cierre de cub_server.\n

$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 No se puede abrir archivo de salida '%1$s'\n
60 \
optimizedb: Optimizar una base de datos.\n\
uso: %1$s optimizedb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -n, --class-name=NAME        actualizar estadisticas para la clase NOMBRE; estandar: todas clases\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 Formato de fecha no valido, formato valido es dd-mm-yyyy:hh:mm:ss o 'backuptime'\n
20 Restoredb cancelado o un error ha ocurrido.\n
60 \
restoredb: Restablecer una base de datos.\n\
uso: %1$s restoredb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -d, --up-to-date=DATE             restablecer la base de datos a la altura de su condicion en FECHA determinada. FECHA es dada como sigue: dd-mm-yyyy:hh:mm:ss o palabra clave 'backuptime'\n\
      --list                        mostrar una lista de volumenes de respaldo sin restablecer\n\
  -B, --backup-file-path=PATH       RUTA es un directorio de volumenes de respaldo que necesitan ser restablecidos\n\
  -l, --level=LEVEL                 NIVEL de respaldo a restablecer; vea uso de respaldo; estandar: 0(respaldo completo)\n\
  -p, --partial-recovery            realizar recuperacion parcial si alguna carpeta de registro falta\n\
  -o, --output-file=FILE            redireccionar mensajes de salida al ARCHIVO; estandar: ninguno\n\
  -u, --use-database-location-path  restablecer la base de datos y volumenes de registro a la ruta especificada en el archivo de ubicacion de la base de datos\n\
  -k, --keys-file-path              Ruta del archivo de claves (_keys) para que TDE se utilice durante la restauración\n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 El nombre de base de datos falta.\n
20 Archivo '%1$s' no tiene %2$d lineas.\n
21 interrumpir ... \n
22 Carga interrumpida - transaccion actual cometida\n
23 Carga interrumpida - transaccion actual abortada\n
24 No se puede abrir archivo '%1$s'\n
25 \n*** Verificar sintaxis ***\n\n
26 %1$d errores detectados.\n
27 \n*** Insertando objetos ***\n\n
28 Total %1$ld objetos insertados. \n
29 Total %1$8d objetos referenciados insertados con contenidos estandar.\n
30 \n*** Cometiendo la transaccion ***\n
31 \n*** Cerrando la base de datos ***\n
32 Linea %1$d:
33 Error al analizar '%1$s' como un valor '%2$s' para atributo '%3$s' de '%4$s'\n
34 Falta dominio para atributo %1$s de %2$s\n
35 Para atributo %1$s de %2$s, tipo %3$s no es permitido en el dominio del conjunto\n
36 Para atributo %1$s de %2$s, tipo esperado %3$s, ha obtenido una constante de conjunto\n
37 Para atributo %1$s de %2$s, tipo esperado %3$s, ha obtenido %4$s\n
38 Ninguna clase con el nombre %1$s referenciada por el atributo %2$s de %3$s\n
39 Ninguna clase con el nombre %1$s, ignorando tuplas\n
40 Ninguna clase con identificador %1$d referenciada por el atributo %2$s de %3$s\n
41 Clase %1$s no es permitida en el archivo de entrada del cargador\n
42 Terminado con error.\n
43 ADVERTENCIA: actualizado instancia existente!\n
44 ADVERTENCIA: redefiniendo Instancia %1$d de la clase %2$s\n
45 Instancia %1$d de la clase %2$s ha sido ya definida\n
46 Instancia %1$d de la clase %2$s ha sido ya reservada\n
47 Violacion UNIQUE en atributo '%1$s', convertida en NULL\n
48 %1$s %2$ld instancias\n
49 Clase %1$s\n
50 Contrasena:
51 \n*** Actualizando estadisticas de clase ***\n
52 %1$s cerca de la linea %2$d (texto era '%3$s')\n
53 Error lexical\n
54 linea %1$d: error de sintaxis en \"%2$s\"
55 faltando
56 en %1$s
57 Argumentos incompatibles, \"%1$s\", y \"%2$s\"\n
58 %1$s %2$ld instancias cometidas\n
59 No se pueden apelar versiones no-optimizadas de loaddb.\n
60 Error al analizar \"%1$s\", como tipo %2$s.\n
61 La referencia de objeto no es compatible con el modo CS.\n
62 Estadísticas actualizadas para la clase %1$s.\n
112 %1$ld instancias\r
113 Ultima linea cometida: %1$d\n
114 error-control-file
115 archivo de entrada para controlar error(es) al cargar
116 Total %ld object(s) inserted, %2$ld object(s) failed.\n
117 Carga fallida.\n
118 Maximum length of class name is %1$d bytes.\n
119 Total de %1$8d objeto(s) verificado, %2$d objeto(s) falló. \n
120 del archivo de datos no contiene '%%class' especificador o '--table' falta la opción \n
121 La clase %1$s es ignorada. \n
122 Maximum length of user name is %1$d bytes.\n
123 %1$s es un archivo vacío.\n
124 \
loaddb: Importar objetos y esquemas en la base de datos.\n\
uso: %1$s loaddb [OPTION] database-name[@host]\n\
\n\
opciones validas:\n\
  -S, --SA-mode                  modo de ejecucion independiente\n\
  -C, --CS-mode                  modo de ejecucion cliente-servidor\n\
                                 ADVERTENCIA: En modo HA, solo los archivos de datos (-d, --data-file=FILE) se pueden cargar en modo cliente-servidor.\n\
  -u, --user=ID                  ID de utilizador para acceso a la base de datos; estandar: "public"\n\
  -p, --password=PASS            contrasena de utilizador; estandar: ninguna\n\
      --data-file-check-only     check syntax for data file without load; default: disabled\n\
  -l, --load-only                load data file without prior syntax checking; SA_MODE only\n\
      --estimated-size=NUMBER    NUMERO estimado de instancias; estandar: 5000\n\
  -v, --verbose                  habilitar mensajes de estado verbose; estandar: inhabilitado\n\
  -c, --periodic-commit=COUNT    insertar COUNT al cometer periodicamente; estandar: 10240\n\
      --no-oid                   no utilizar OID\n\
      --no-statistics            no actualizar las estadisticas mientras cargando\n\
  -s, --schema-file=FILE[:LINE]  crear esquema de la LINEA con ARCHIVO\n\
  -i, --index-file=FILE[:LINE]   crear indice de la LINEA CON ARCHIVO\n\
      --trigger-file=FILE[:LINE] crear trigger de la LINEA CON ARCHIVO\n\
  -d, --data-file=FILE           cargar datos con ARCHIVO\n\
  -t, --table=TABLE              nombre de tabla que es sustituido por falta de encabezamiento de clase en archivo de datos\n\
      --error-control-file=FILE  ARCHIVO para controlar error(es) al cargar\n\
      --ignore-class-file=FILE   archivo de entrada de nombres de clase que saltan carga\n\
      --no-user-specified-name   Find classes, serials, and triggers by their object names without their owner names.\n\
                                 This is to load files generated by cubrid before version 11.2.\n\
      --schema-file-list=FILE    El contenido de este archivo es una lista de nombres de archivos de esquema que se utilizarán en loaddb.\n\
                                 loaddb se ejecuta secuencialmente desde arriba.\n\
                                 ADVERTENCIA: No se puede utilizar con la opción -s.\n


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 valor de cached-pages no es valido.\n
42 valor de cached-page-size no es valido.\n
43 %1$ld objetos botados.\n
44 Clase %1$s
45 , %1$d instancias.\n
46 Solo %1$ld de %2$ld objetos botados con exito.\n
47 directorio de salida es demasiado largo.\n
48 LSA : %1$d %2$d \n
49 Contrasena:
50 Maximum length of class name is %1$d bytes.\n
51 Maximum length of user name is %1$d bytes.\n
52 Class %1$s not found.\n
60 \
unloaddb: Exportar objetos y esquemas de la base de datos.\n\
uso: %1$s unloaddb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -u, --user=ID               ID de utilizador para acceso a la base de datos; estandar: "dba"\n\
  -p, --password=PASS         contrasena de utilizador; estandar: ninguna\n\
  -i, --input-class-file=FILE entrar ARCHIVO de nombres de tablas; estandar: botar todas clases\n\
      --include-reference     incluir tablas referenciadas; con opcion -i; estandar: inhabilitado\n\
      --input-class-only      incluir solo la clase especificada; con opcion -i; estandar: inhabilitado\n\
      --lo-count=COUNT        lo file COUNT para un directorio: estandar: 0\n\
      --estimated-size=NUMBER NUMERO estimado de instancias; estandar: auto calculado\n\
      --cached-pages=NUMBER   NUMERO de paginas almacenadas; estandar: 100\n\
  -O, --output-path=PATH      RUTA de directorio de salida; estandar: directorio de trabajo\n\
  -s, --schema-only           botar solo esquema; estandar: esquema y objetos\n\
  -d, --data-only             botar solo objetos; estandar: esquema y objetos\n\
      --output-prefix=PREFIEX PREFIJO para archivos de salida; estandar: el nombre de la base de datos\n\
      --hash-file=FILE        ARCHIVO para hash; estandar: generar por tmpnam()\n\
      --latest-image          al volcar objetos, vuelca la última imagen de los objetos en los volúmenes de datos, sin hacer referencia a los volúmenes de registro (ignorando MVCC)\n\
                              Podría incluir datos no confirmados y datos eliminados que no se vacían; predeterminado: deshabilitado\n\
  -v, --verbose               habilitar mensajes de estado verbose; estandar: inhabilitado\n\
      --use-delimiter         utilizar '"' donde un identificador empieza y acaba; estandar: no utilizar\n\
  -S, --SA-mode               modo de ejecucion independiente\n\
  -C, --CS-mode               modo de ejecucion cliente-servidor\n\
      --datafile-per-class    crear un archivo de objeto para cada clase; estandar: inhabilitado\n\
      --split-schema-files    dividir la información del esquema por objeto y generar cada archivo; Predeterminado: generar un archivo de esquema con todos los objetos\n\
      --skip-index-detail     No imprima las opciones de la cláusula WITH especificadas al crear el índice; predeterminado: deshabilitado\n\
      --as-dba                Si el usuario de inicio de sesión es miembro del grupo DBA, extraiga el mismo archivo de esquema que el DBA.\n



$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 \nPass 1\n\n
12 %1$d objetos procesados.\n
13 \nPass 2\n\n
14 Clase %1$s\n
15 oid:  %1$d|%2$d|%3$d,\n
16 %1$d instancias.\n
17 Actualizando objeto.\n
18 ref_oid:  %1$d.%2$d.%3$d, ref_class_oid:  %4$d.%5$d.%6$d\n
19 No se pudo transformar objeto\n
20 Falta de archivo heap de clase\n
21 No se pudo actualizar objeto\n
22 Solo %1$d de %2$d objetos procesados con exito.\n
23 COMPACTDB ya inicializado.\n
24 El numero de paginas tiene que ser entre %1$d y %2$d.\n
25 El tiempo de expirar para el bloqueo de instancia tiene que ser entre %1$d y %2$d.\n
26 Total objetos: %1$d\n
27 Objetos fracasados: %1$d\n
28 Objetos modificados: %1$d\n
29 Objetos grandes: %1$d\n
30 Representaciones viejas de clase eliminadas del catalogo.\n
31 Representaciones viejas de clase preservadas en catalogo.\n
32 No se puede ajustar el nivel de aislamiento.\n
33 No se puede compactar la base de datos.\n
34 El tiempo de expirar del bloqueo de clase tiene que ser entre %1$d y %2$d.\n
35 Clase bloqueada... Procesamiento incompleto...\n
36 Clase no procesada...\n
37 Error de proceso... Procesamiento incompleto...\n
38 Nada para procesar...\n
39 Parametros no validos...\n
40 Nombre de clase desconocido...\n
41 Los OIDs y las paginas no utilizadas de la clase han sido reclamadas.\n
42 Saltado la recuperacion de los OIDs y paginas.\n
43 Error al procesar la clase, clase saltada.\n
44 \nPass 3\n\n
45 Fracaso al compactar el heap...\n
46 Exito al compactar el heap...\n
47 Maximum length of class name is %1$d bytes.\n
48 Maximum length of user name is %1$d bytes.\n
60 \
compactdb: Spacio libre no utilizado en una base de datos.\n\
uso: %1$s compactdb [OPTION] database-name [class_name1 class_name2 ...]\n\
\n\
opciones validas:\n\
  -v, --verbose                     habilitar mensajes de estado verbose; estandar: inhabilitado\n\
  -i, --input-class-file=FILE       ARCHIVO de entrada de nombres de tabla\n\
                                    lista class_name debe ser vacia\n\
                                    estandar: compactar todas clases\n\
  -p, --pages-commited-once=NUMBER  numero maximo de paginas para cometer a la vez\n\
                                    estandar: 10\n\
                                    habilitado solo en modo CS\n\
  -S, --SA-mode                     modo de ejecucion independiente\n\
  -C, --CS-mode                     modo de ejecucion cliente-servidor\n\
  -d, --delete-old-repr             eliminar representaciones viejas de clase del catalogo; estandar: inhabilitado\n\
  -I, --Instance-lock-timeout       tiempo de expirar para bloqueo de instancia\n\
                                    habilitado solo en modo CS\n\
  -c, --class-lock-timeout          tiempo de expirar para bloqueo de clase\n\
                                    habilitado solo en modo CS\n\
  class_name_list                   Nombres de clase compresivos\n

$set 15 MSGCAT_UTIL_SET_COMMDB
21 CUBRID master ejecutado en host %1$s \nha sido reinicializado a %2$s
22 ha servido %1$d peticiones. %2$d servidor(es) que ejecutan.\n
23 APAGANDO en %1$s minutos\n
24 %1$s
25 Nombre del servidor para apagar:
26 Cuantos minutos hasta el cierre? :
27 \
cub_commdb: Esta utilidad es depreciada. En cambio, utilizar comando 'cubrid'.\n\
uso: cub_commdb [OPTION]\n\
\n\
opciones validas:\n\
  -P, --server-list                imprimir procesos de servidor\n\
  -O, --all-list                   imprimir todos los procesos\n\
  -S, --shutdown-server=NAME       apagar servidor NOMBRE de la base de datos\n\
  -A, --shutdown-all               cerrar todos los procesos\n\
  -c, --server-mode=NAME           imprimir el modo HA del servidor NOMBRE de la base de datos\n\
  -N, --node-list                  listar nodos patrocinados por CUBRID heartbeat\n\
  -L, --process-list               listar proceso local patrocinado por CUBRID heartbeat\n\
  -p, --ping-host                  list ping hosts used for CUBRID heartbeat\n\
  -D, --dereg-process=PID          salir del proceso de destino y eliminarlo de las entradas de proceso patrocinadas\n\
  -F, --reconfig-node-list         aplicar la configuracion CUBRID heartbeat recien cambiada\n\
  -V, --verbose                    habilitar mensajes de estado verbose; estandar: inhabilitado\n
28 Disculpe, comando desconocido: %1$s\n
29 No se puede obtener el nombre de host para el host local - abortando\n
30 No se puede encontrar archivo de parametro\n
31 No se puede conectar al servidor maestro en %1$s\n
32 Cuantos minutos para el cierre? :
33 Maestro se va a apagar en %1$d minutos\n
34 Error al recibir datos del cliente
35 el servidor de base de datos %1$s no esta actualmente ejecutando
36 no se puede ejecutar shell (%1$s) para %2$s, %3$s
37 proceso fork %1$s, regreso %2$d, %3$s
38 no se puede ejecutar %1$s
39 -i(--immediately) opcion debe ser utilizada con la opcion --deact-stop-all.\n

$set 16 MSGCAT_UTIL_SET_PATCHDB
1 Ejecute sin la opción --recreate-log o elimine el volumen de registro activo manualmente.\n
60 \
emergency_patchlog: Parchar una base de datos en caso de emergencia.\n\
usage: %1$s emergency_patchlog [OPTION] database-name [database-locale]\n\
\n\
opciones validas:\n\
  -r, --recreate-log           recrear archivos de registro; Utilizar con cuidado (necesita el argumento de la base de datos-locale)\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 Valor equivocado %1$d para npages\n
21 Valor equivocado '%1$s' para proposito de almacenamiento de volumen.\n
22 ADVERTENCIA: opcion --max-writesize-in-sec es ignorada en modo de ejecucion stand-alone.\n
60 \
addvoldb: Agregar un archivo de volumen para la base de datos.\n\
uso: %1$s addvoldb [OPTION] database-name\n\
\n\
opciones validas:\n\
      --db-volume-size=SIZE        tamano de volumen adicional; estandar: db_volume_size en cubrid.conf\n\
      --max-writesize-in-sec=SIZE  la cantidad de volumen escrita por segundo; (ex. 512K, 1M, 10M); estandar: no utilizado; minimum: 160K\n\
  -n, --volume-name=NAME           NOMBRE de volumen de informacion; estandar: generar como "db"_ext1)\n\
  -F, --file-path=PATH             RUTA para archivo de agregacion volumen; estandar: volume_extension_path\n\
      --comment=COMMENT            COMENTARIO para archivo de agregacion volumen; estandar: ninguno\n\
  -p, --purpose=PURPOSE            PROPOSITO para archivo de agregacion volumen; permitido:\n\
                                           DATA - solo para datos\n\
                                           INDEX - solo para indice\n\
                                           TEMP - solo para temporario\n\
                                           GENERIC - para todos propositos\n\
  -S, --SA-mode                    modo de ejecucion independiente\n\
  -C, --CS-mode                    modo de ejecucion cliente-servidor\n

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 Algunas inconsistencias han sido detectadas en su base de datos.\n Por favor consulte error_log_file %1$s para informacion adicional.\n
21 %1$s: No se puede encontrar la clase. Va a ser omitida.\n
22 %1$s: Cannot find the index on class %2$s.\n
60 \
checkdb: Verificar consistencia de la base de datos.\n\
uso: %1$s checkdb [OPTION] database-name [class_name1 class_name2 ...]\n\
\n\
opciones validas:\n\
  -S, --SA-mode                modo de ejecucion independiente\n\
  -C, --CS-mode                modo de ejecucion cliente-servidor\n\
  -r, --repair                 reparar cuando se encuentran inconsistencias\n\
  -i, --input-class-file=FILE  entrar ARCHIVO de nombres de clase\n\
                               estandar: verificar todas clases\n\
  -I, --index-name             index name that will be checked only\n\
                               default: check all indices\n\
      --check-prev-link        check only previous links of indices\n\
      --repair-prev-link       reparar enlaces de indices anteriores\n\
      --check-file-tracker     check all allocated pages of file\n\
      --check-heap             check all pages of heap files\n\
      --check-catalog          check catalog consistency\n\
      --check-btree            check all btree indices\n\
      --check-class-name       check class name consistency\n\
      --check-btree-entries    check entries of btree consistency\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
10 Descripcion de espacio para la base de datos '%1$s' con pagesize %2$s. (log pagesize: %3$s)\n\n
11 No se puede abrir archivo de salida '%1$s'\n
22 Descripcion de espacio LOB %1$s \n\n
20 type                purpose            volume_count        used_pages          free_pages          total_pages\n
21 type                purpose            volume_count         used_size           free_size           total_size\n
22 PERMANENT           PERMANENT DATA            %1$5d     %2$13s       %3$13s        %4$13s\n
23 PERMANENT           TEMPORARY DATA            %1$5d     %2$13s       %3$13s        %4$13s\n
24 TEMPORARY           TEMPORARY DATA            %1$5d     %2$13s       %3$13s        %4$13s\n
25 -                   -                         %1$5d     %2$13s       %3$13s        %4$13s\n
30 Descripción del espacio para todos los volúmenes:\n
31 volid               type                purpose            used_pages          free_pages          total_pages         volume_name\n
32 volid               type                purpose             used_size           free_size           total_size         volume_name\n
33 %1$5d               PERMANENT           PERMANENT DATA  %2$13s       %3$13s        %4$13s         %5$-64s\n
34 %1$5d               PERMANENT           TEMPORARY DATA  %2$13s       %3$13s        %4$13s         %5$-64s\n
35 %1$5d               TEMPORARY           TEMPORARY DATA  %2$13s       %3$13s        %4$13s         %5$-64s\n
40 Descripción detallada del espacio para los archivos:\n
41 data_type           file_count          used_pages         file_table_pages    reserved_pages      total_pages\n
42 data_type           file_count           used_size          file_table_size     reserved_size       total_size\n
43 %1$-10s               %2$5d       %3$13s            %4$13s     %5$13s    %6$13s\n
50 -----------------------------------------------------------------------------------------------------------------\n
60 \
spacedb: Mostrar espacio usado/libre de la base de datos.\n\
uso: %1$s spacedb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -o, --output-file=FILE       redireccionar mensajes de salida al ARCHIVO; estandar: ninguno\n\
  -S, --SA-mode                modo de ejecucion independiente\n\
  -C, --CS-mode                modo de ejecucion cliente-servidor\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               ajustar la unidad de visualizacion (pagina, mega, giga, tera, legible por humanos); estandar: H\n\
  -s, --summarize              mostrar solo un resumen para cada proposito de volumen\n\
  -p, --purpose                muestra la dimension de cada proposito (data/index/temp) de cada volumen\n

$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 Numero estimado de Paginas = %1$d\n
60 \
estimate_data: Estimar tamano de datos.\n\
uso: %1$s estimate_data number-of-instances average-instance-size number-of-attributes variable-number-of-attributes\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 Tipo malo de clave del indice especificado = %1$s\n
16 Mala longitud media de clave especificada = %1$d cuando precision = %2$d\n
17 Argumentos malos especificados\n
20 Numero estimado del total de paginas de indice = %1$d\n (Cuando indice creado, despues de que las instancias sean pobladas.)\n
21 Numero estimado del total de paginas de indice = %1$d\n (Cuando indice creado, antes de que las instancias sean pobladas (caso medio))\n
22 Numero estimado del total de paginas de indice = %1$d\n (Cuando indice creado, antes de que las instancias sean pobladas (peor caso))\n
23 Para la entrada dada...\n
24 Numero total de instancias : %1$d\n
25 Numero de claves diferentes: %1$d\n
26 Tamano medio de clave      : %1$d\n
27 Tipo de clave              : %1$s\n\n
60 \
estimate_index: Tamano estimado de un indice.\n\
uso: %1$s estimate_index number-of-instances number-of-different-keys average-key-size key-type\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: Instalar una base de datos.\n\
uso: %1$s installdb [OPTION] database-name\n\
\n\
opciones validas:\n\
      --server-name=HOST       nombre HOST del servidor; estandar: nombre actual de host\n\
  -F, --file-path=PATH         RUTA de directorio para volumenes de base de datos; estandar: directorio actual\n\
  -L, --log-path=PATH          RUTA de directorio para volumenes de registro; estandar: la misma que la ruta de archivo\n

$set 23 MSGCAT_UTIL_SET_MIGDB
1 OID de clase temporaria\n
2 No se pueden imprimir valores elo\n
3 Error al accesar objeto largo para migracion\n
4 Error al abrir archivo de salida para migracion de objeto largo\n
5 Error de leer\n
6 Error de escribir\n
7 Error al abrir archivo de entrada ELO "%1$s"\n
8 CLASES:
9 Nombre de archivo hash
10 Nombre:           %1$s\n
11 Tamano:                       %1$d\n
12 Tamano de pagina:             %1$d\n
13 Tamano de datos:              %1$d\n
14 Tamano de entrada:            %1$d\n
15 Entradas por pagina:          %1$d\n
16 Paginas almacenadas:          %1$d\n
17 Numero de entradas:           %1$d\n
18 Numero de colisiones:         %1$d\n
19 Nombre de archivo hash:       %1$s\n
20 Siguente entrada de sobreflujo: %1$d\n
21 Tipo de clave:                %1$s\n
22 Direccion de encabezamientos de pagina: %1$x\n
23 Direccion del ultimo encabezamiento de pagina: %1$x\n
24 Direccion del encabezamiento de pagina libre: %1$x\n
25 Direccion del mapa de bits de la pagina: %1$x\n
26 Tamano del mapa de bits de la pagina: %1$d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
15 No se puede abrir archivo de salida '%1$s'\n
60 \
diagdb: Imprimir una base de datos.\n\
uso: %1$s diagdb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -o, --output-file=FILE       redireccionar mensajes de salida al ARCHIVO; estandar: ninguno\n\
      --emergency              emergency restart; default: none\n\
  -d, --dump-type=TYPE         TIPO de imprimir\n\
                               -1 imprimir todo\n\
                                1 imprimir tablas de archivo\n\
                                2 imprimir capacidades de archivo\n\
                                3 imprimir capacidades de heap\n\
                                4 imprimir capacidades de indice\n\
                                5 imprimir nombres de clase\n\
                                6 imprimir mapas de bits de disco\n\
                                7 imprimir catalogo\n\
                                8 imprimir registro\n\
                                9 imprimir heap\n


$set 25 MSGCAT_UTIL_SET_LOCKDB
15 No se puede abrir archivo de salida '%1$s'\n
59 lockdb no puede ser ejecutado en modo independiente.\n
60 \
lockdb: Mostrar estado de bloqueo de la base de datos.\n\
uso : %1$s lockdb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -o, --output-file=FILE       redireccionar mensajes de salida al ARCHIVO; estandar: ninguno\n\
  -c, --contention             display only lock information which has contention\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 Solo un argumento debe ser dado entre: tran-index, user, host, progname\n
21 Introducir contrasena DBA:
26 No hay ninguna transaccion con los valores de argumento especificados.\n
27 Listo para matar las siguentes transacciones:\n
28 Desea continuar? (Y/N)
29 Matando transaccion asociada con el indice de transaccion %1$d\n
30 Fracaso al matar las siguentes transacciones\n\n
31 La transaccion todavia no acaba, pero puede que haga trabajo de reversion.\n Por favor, espere un segundo y reintentar luego si todavia necesario.\n
32 Indice de transaccion tranindex(%1$s) no valido.\n
59 killtran no puede ejecutar en modo independiente.\n
60 \
killtran: Patrocinar transacciones de una base de datos.\n\
uso: %1$s killtran [OPTION] database-name\n\
\n\
opciones validas:\n\
  -i, --kill-transaction-index=INDEX matar transaccion con INDICE de transaccion\n\
      --kill-user-name=ID            matar todas transacciones con ID de utilizador\n\
      --kill-host-name=HOST          matar todas transacciones con HOST de cliente\n\
      --kill-program-name=NAME       matar todas transacciones con NOMBRE de programa de cliente\n\
      --kill-sql-id=SQL_ID           matar todas transacciones con SQL_ID\n\
  -p, --dba-password=PASS            contrasena del utilizador DBA; va a ser pedida si no es especificado\n\
                                     (solo funciona con las opciones -i o --kill. De lo contrario, obtiene un error)\n\
  -d, --display-information          mostrar informacion relacionada a las transacciones activas\n\
  -q, --query-exec-info              mostrar informacion sobre informacion de ejecucion de transacciones activas de la consulta\n\
  -f, --force                        matar la transaccion sin un aviso para verificar\n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1 nombre-de-base-de-datos
2 argumento pedido, no hay estandar
3 verificacion de compatibilidad
4 solo verificacion de compatibilidad
5 destino de respaldo
6 una ruta de directorio o dispositivo para la destinacion de los volumenes de respaldo
20 la base de datos '%1$s' no existe.
21 '%1$s' no puede ser migrada a %2$s, porque su version DB es demasiado baja.\n
22 '%1$s' no necesitan ser migrados.\n
23 '%1$s' necesita ser migrado.\n
24 Respaldar es recomendado despues de migracion. Quiere respaldar su base de datos ahora? (si/no):
25 Le recomendamos respaldar su nueva base de datos con el comando backupdb despues de la migracion.
26 \n*** ADVERTENCIA: Respaldar su base de datos antes de inicializar la migracion hacia %1$s ***\n\nPara recuperar errores que pueden haber ocurrido durante la migracion, le recomendamos muy fuerte respaldar su base de datos.\nHa ya creado un respaldo para su base de datos? (si/no):
27 \n1. Ejecutar respaldo \n2. Introducir la nueva ruta de respaldo para la base de datos nueva. (ruta actual: %1$s)\n3. Salir \n
28 \n La nueva ruta de respaldo para la base de datos:
29 \nMigracion y respaldo han sido finalizados con exito.
30 Fracaso al respaldar. Por favor, reintente respaldar su base de datos.
31 \n\n*** ADVERTENCIA : Una vez que la migracion es finalizada, Ud no puede restablecer la base de datos con volumenes de respaldo anteriores.\nNotar que la version anterior de los archivos de respaldo pueden ser restablecidos solo en versiones correspondientes de restoredb.\nPor favor, verifique que los volumenes de respaldo anteriores son guardados a un nivel alto de seguridad.\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: Alterar una host de la base de datos.\n\
uso: %1$s alterdbhost [OPTION] database-name\n\
\n\
opciones validas:\n\
  -h, --host=HOST              nombre nuevo de HOST; estandar: nombre actual de host\n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 nombre-de-base-de-datos
2 argumento pedido, no hay estandar
3 archivo-de-clase-java
4 argumento pedido, no hay estandar
60 \
Usage: loadjava [OPTION] database-name java-class-file\n\
\n\
valid options:\n\
  -y, --overwrite              sobrescribir archivo de clase si existe, estandar no\n\
  -j, --jni                    add a Class or Jar to static loading for JNI\n

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 No se puede abrir archivo de salida '%1$s'\n
59 plandump no puede ejecutar como modo independiente.\n
60 \
plandump: Mostrar o controlar planos de consulta almacenados.\n\
uso: %1$s plandump [OPTION] database-name\n\
\n\
opciones validas:\n\
  -d, --drop                   soltar todos planes en la memoria cache del servidor\n\
  -o, --output-file=FILE       redireccionar mensajes de salida al ARCHIVO; estandar: ninguno\n

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 No se puede abrir archivo de salida '%1$s'\n
21 Parametros de sistema a ser utilizados por el programa cliente.\n
22 Parametros de sistema cargados por el servidor '%1$s'\n
23 Parametros de sistema a ser utilizados en modo independiente.\n
60 \
paramdump: Imprimir ajustes de los parametros de sistema.\n\
uso: %1$s paramdump [OPTION] database-name\n\
\n\
opciones validas:\n\
  -o, --output-file=FILE       redireccionar mensaje de salida al ARCHIVO; estandar: ninguno\n\
  -b, --both                   imprimir parametros de cliente y servidor\n\
  -S, --SA-mode                modo de ejecucion independiente\n\
  -C, --CS-mode                modo de ejecucion cliente-servidor\n

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 Valor equivocado \"%1$s\" para el modo HA de ejecucion del servidor.\n
12 No se puede cambiar el modo de ejecucion HA del servidor.\n
21 Introducir contrasena DBA:
22 El modo HA actual de ejecucion del servidor '%1$s' es %2$s.\n
23 El modo HA de ejecucion del servidor '%1$s' cambiado a \"%2$s\".\n
24 El servidor no ha sido configurado para HA.\n
58 Caracteristica HA no soportada en esta plataforma.\n
59 changemode no puede ejecutar como modo independiente.\n
60 \
changemode: Mostrar o cambiar el modo HA de ejecucion del servidor.\n\
uso: %1$s changemode [OPTION] database-name\n\
\n\
opciones validas:\n\
  -m, --mode=MODE             cambiar el modo de servidore en MODE; estandar: ninguno\n\
  -f, --force                 forzar cambiar\n\
  -t, --timeout=S             espedando S segundos mientras la transaccion activa es terminada normalmente\n\
                              opcion -t es eficaz solo cuando el utilizador cambia el modo del servidor en mantenimiento\n\
                              estandar: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 Valor equivocado \"%1$s\" para el modo de ejecucion de copylogdb.\n
21 Introducir contrasena DBA:
22 El servidor no ha sido configurado para HA.\n
58 Caracteristica HA no es soportada en esta plataforma.\n
59 copylogdb no puede ejecutar en modo independiente.\n
60 \
copylogdb: Copiar paginas de registro del servidor.\n\
uso: %1$s copylogdb [OPTION] database-name\n\
\n\
opciones validas: \n\
  -L, --log-path=PATH         RUTA de directorio para los volumenes de registro copiadas\n\
  -m, --mode=MODE             modo de operacion; estandar: sync\n\
                              MODO: sync o semisync o async\n\
  -S, --start-page-id=ID      first log page to copy\n

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 Introducir contrasena DBA:
22 El servidor no ha sido configurado para HA. \n
58 Caracteristica HA no es soportada en esta plataforma.\n
59 applylogdb no puede ejecutar como modo independiente.\n
60 \
applylogdb: Aplicar registro copiado al servidor.\n\
uso: %1$s applylogdb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -L, --log-path=PATH         RUTA de directorio de los volumenes de registro copiados\n\
      --max-mem-size=SIZE     TAMANO maximo de proceso en MB; max 500\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 No se puede abrir archivo de salida '%1$s'\n
59 statdump no puede ejecutar como modo independiente.\n
60 \
statdump: Imprimir estadisticas globales.\n\
uso: %1$s statdump [OPTION] database-name\n\
\n\
opciones validas:\n\
  -o, --output-file=FILE       redireccionar mensaje de salida al ARCHIVO; estandar: ninguno\n\
  -c, --cumulative             imprimir estadisticas cumulativas\n\
  -i, --interval=S             imprimir estadisticas casa S segundos\n\
  -s, --substr=STRING          imprimir solo aquellos articulos de estadistica que emparejan con el substring\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 Introducir contrasena DBA:
22 El servidor no ha sido configurado para HA. \n
57 No se puede generar la información aplicada debido a una ruta no válida de la opción -L o no hay información relacionada en la tabla de catálogo db_ha_apply_info.\n
58 Caracteristica HA no es soportada en esta plataforma.\n
59 applyinfo no puede ejecutar como modo independiente.\n
60 \
applyinfo: mostrar informacion HA Apply de CUBRID.\n\
uso: %1$s applyinfo [OPTION] database-name\n\
\n\
opciones validas:\n\
  -r, --remote-host-name      nombre de host remote; mostrar la informacion de registro activa del nodo remote \n\
  -L, --copied-log-path=PATH  ruta de los volumenes de registro copiados; mostrar informacion de registro copiado \n\
  -a, --applied-info          mostrar informacion aplicada; se requiere la opción -L \n\
  -p, --page=ID               id de pagina; se requiere la opción -L; estandar : 0\n\
  -v, --verbose               habilitar mensajes de estado verbose; estandar : inhabilitar\n\
  -i, --interval=S            imprimir informacion casa S segundos\n

$set 45 MSGCAT_UTIL_SET_ACLDB
59 acldb no puede ejecutar como modo independiente.\n
60 \
acldb: Mostrar o recargar ajuste de control de acceso de la base de datos.\n\
uso: %1$s acldb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -r, --reload       recargar archivo de control ip\n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
genlocale: Genera biblioteca local compartida utilizando el archivo de entrada LDML.\n\
uso: %1$s genlocale [OPTION] [language-string]\n\
\n\
language-string       string de idioma local para cual generar biblioteca compartida.\n\
                      Si no hay ningun string de idioma dado, todos los idiomas en 'cubrid_locales.txt'\n\
                      van a ser procesados.\n\
opciones validas:\n\
  -v, --verbose               habilitar mensajes de estado verbose; estandar : inhabilitar\n\
  -i, --input-ldml-file       ruta para archivo de entrada LDML. Solo cuando el string de idioma es dado.\n
61 Argumento de nombre local no valido.\n

$set 47 MSGCAT_UTIL_SET_DUMPLOCALE
57 No se pueden utilizar simultaneamente -i/--input-file y el identificador local.\n
58 Rango codepoint no valido (-s -e).\n
59 Argumento de nombre local no valido.\n
60 \
dumplocale: Abandona el contenido de la biblioteca local compartida generada anteriormente utilizando el archivo de entrada LDML.\n\
uso: %1$s dumplocale [OPTION] [language-string]\n\
\n\
language-string  string de idiomas local para cual imprimir biblioteca compartida local.\n\
                 Si no hay ningun string de idioma dado, todos los idiomas en 'cubrid_locales.txt'\n\
                 van a ser procesados.\n\
opciones validas:\n\
  -i, --input-file    ruta para biblioteca local compartida de entrada (generada anteriormente).\n\
                      Puede ser utilizada solo cuando no hay ningun string de idioma dado.\n\
  -d, --calendar      imprimir calendar y la informacion fecha/tiempo; estandar : inhabilitar\n\
  -n, --numeric       imprimir informacion numerica; estandar : inhabilitar\n\
  -a, --alphabet=l|lower|u|upper|both        imprimir alfabeto y informacion de carcasa; estandar : inhabilitar\n\
      --identifier-alphabet=l|lower|u|upper  imprimir alfabeto y informacion de carcasapara identificadores; estandar : inhabilitar\n\
  -c, --codepoint-order  imprimir informacion de colacion ordenada basado en valores codepoint\n\
                         (cp, char, weight, next-cp, char y weight);\n\
                         estandar : inhabilitar\n\
  -w, --weight-order  imprimir informacion de colacion ordenada basado en valores weight\n\
                      (weight, cp, char); estandar : disable\n\
  -s, --start-value   opcion de filtrar; inicializando codepoint para\n\
                      -a, --identificador-alfabeto, -c, -w; estandar : 0\n\
  -e, --end-value     opcion de filtrar; terminando codepoint para \n\
                      -a, --identificador-alfabeto, -c, -w; \n\
                      estandar : valor maximo leido de la biblioteca compartida local\n\
  -k, --console-conversion  imprimir datos de la conversion de consola; estandar : inhabilitar\n\
  -z, --normalization   imprimir datos de normalizacion; estandar : inhabilitar\n

$set 48 MSGCAT_UTIL_SET_SYNCCOLLDB
43 Particionamiento debe ser eliminado antes de cambiar la colacion actual '%1$s" de su atributo de paticionamiento.\nClass name\n
44 Claves externas en atributos teniendo colacion '%1$s' deben ser bajadas antes de cambiar la colacion de atributos.\nTable | Foreign Key\n
45 Clases teniendo colacion '%1$s'; deberian ser redefinidas:\nClass name\n
46 Indices de funcion teniendo expresion que contiene colacion '%1$s'; deberian ser redefinidas:\n\
Tabla | Indice | Expresion de funcion\n
47 Sincronizacion de colaciones no ha sido ejecutada. La base de datos no ha sido cambiada.\n
48 Contenido de '%1$s' tabla de sistema ha sido actualizado con nuevas colaciones de sistema.\n
49 Actualizar colaciones de sistema podria causar corrupcion de la base de datos. Desea continuar (y/n)?\n
50 Colacion '%1$s' (Id: %2$d) no encontrada el la base de datos o cambiada en la nueva configuracion de sistema.\n
51 Disparadores teniendo condicion que tiene colacion '%1$s'; deberian ser redefinidos:\nTrigger | Condition\n
52 Vistas de consulta que contienen colacion '%1$s'; deberian ser redefinidas:\nView | Query\n
53 Atributos teniendo colacion '%1$s'; deberian ser redefinidos.\nTable | Attribute Domain\n
54 Intervención manual en la base de datos se requiere.\n\
Por favor verificar archivo '%1$s' y ejecutarlo antes de forzar colaciones en la base de datos.\n
55 Sincronizacion no es necesaria.\n
56 Sincronizacion de colacion de sistema en la base de datos es requerida.\n\
Ejecutar 'cubrid synccolldb -f %1$s'\n
57 Hay %1$d colaciones nuevas en la configuracion de sistema comparado con la base de datos:\n
58 Todas colaciones en la base de datos '%1$s' son encontradas en la nueva configuracion de sistema.\n
59 Hay %1$d colaciones en la base de datos que no son configuradas o son cambiadas comparado con las colaciones del sistema.\n
60 \
synccolldb: Sincroniza las colaciones de la base de datos con la colaciones del sistema.\n\
uso: %1$s synccolldb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -c, --check-only    Verifica las colaciones de la base de datos con las colaciones del sistema.\n\
                      Si se requiere sincronizacion de colaciones, codigo de error 1 es returnado.\n\
                      Muestra un informe de las colaciones incompatibles.\n\
  -f, --force-only    Actualiza las colaciones en la base de datos para que sean iguales con las colaciones de la nueva configuracion del sistema.\n\

$set 49 MSGCAT_UTIL_SET_TRANLIST
20 Introducir %1$s clave:
22 Tran index            User name      Host name      Process id      Program name\n
23 ---------------------------------------------------------------------------------\n
24 %1$4d%2$-12s %3$14s %4$14s %5$15d %6$17s\n
25 No hay transacciones\n
26 tranlist puede ser ejecutada solamente por el DBA or por un miembro del grupo DBA.\n
27 El numero de la columna de la clave de ordenacion (%1$d) no es valido.\n
32 Tran index           Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
33 %1$4d%2$-12s %3$14d    %4$-25s %5$10.2f %6$12.2f    %7$28s     %8$13s   %9$s\n
34 ----------------------------------------------------------------------------------------------------------------------------------------------------\n
42 Tran index            User name      Host name      Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
43 %1$4d%2$-12s %3$14s %4$14s %5$14d    %6$-25s %7$10.2f %8$12.2f    %9$28s     %10$13s   %11$s\n
44 -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 Tran index: %1$d
46 SQL_ID: %1$s\n
59 tranlist no puede ser ejecutado en modo standalone.\n
60 \
tranlist: Imprimir informacion de transaccion activa.\n\
uso: %1$s tranlist [OPTION] database-name\n\
\n\
opciones validas:\n\
  -s, --summary                imprimir resume de informacion de la transaccion\n\
      --sort-key=COLUMN_NUMBER numero de la columna de ordenacion \n\
      --reverse                invierte el resultado de la clasificacion\n

$set 51 MSGCAT_UTIL_SET_GEN_TZ
59 Modo invalido de generacion suministrado.\n
60 Carpeta de entradas invalida o corrupta.\n
61 \
gen_tz: Generar archivos fuente conteniendo datos de zona horaria a ser compilados dentro de\n\
        una biblioteca compartida.\n\
uso:  %s gen_tz -g [new|extend] [database-name] (only for extend mode)\n\
\n\
opciones validas:\n\
  -i, --input-folder    camino hacia una carpeta de entradas conteniendo una version de\n\
                        archivos de zona horaria de la base de datos de IANA.\n\
                        Default: $CUBRID/timezones/tzdata\n\
  -g, --gen-mode=TYPE   controla el resultado de gen_tz, basado en TYPE:\n\
                        TYPE:\n\
                        1. NEW (default) : generar archivo(s) fuente de zona horaria para\n\
                        la biblioteca compartida sin referencia a versiones anteriores;\n\
                        tambien generar un archivo fuente conteniendo todos los paises\n\
                        y nombres de zona horaria (destinado para el uso de desarolladores, a\n\
                        ser integrado dentro futuras versiones  de CUBRID).\n\
                        2. EXTEND : Generar archivo(s) fuente de zona horaria datos de la \n\
                        carpeta de entrada; Nuevas zonas horarias encontradas en la entrada\n\
                        archivos se agregan; desplazamientos GMT y horario de verano\n\
                        la información se actualiza (o se agrega, para nuevas zonas horarias);\n\
                        los intervalos horarios nunca se eliminan si un huso horario codificado en\n\
                        CUBRID no está presente en los archivos de entrada, los datos se recuperan\n\
                        de la biblioteca de zona horaria existente.\n\
                        Si la primera fase de la extensión no logra que todos los datos sean\n\
                        compatibles con versiones anteriores, todas las filas de todas las tablas\n\
                        que tengan tipos de datos de zona horaria se actualizarán.\n

$set 52 MSGCAT_UTIL_SET_DUMP_TZ
59 El identificador de zona suministrado es fuera de rango. Utilizar -z para ver todas\n\
 las zonas horarias y sus identificadores.\n
60 \
dump_tz: Mostrar informacion relacionada a la zona horaria.\n\
uso: %s dump_tz [OPTION]\n\
\n\
opciones validas:\n\
  -c, --list-countries      mostar la lista de paises actualmente soportada\n\
  -l, --leap-seconds        mostrar la lista de segundas leap\n\
  -s, --summary             mostrar un resumen de los datos de zona horaria\n\
  -z, --list-zones          mostrar una lista de zonas horarias actualmente soportadas\n\
  -d, --zone-id=ZONE_ID|ALL mostrar la informacion disponible relacionada a la zona horaria\n\
                            a una zona horaria que tiene el ZONE_ID suministrado o para\n\
                            TODAS zonas horarias. El identificador de una zona horaria puede ser obtenido\n\
                            de la lista mostrada utilizando la opcion -z.\n

$set 53 MSGCAT_UTIL_SET_RESTORESLAVE
20 Restoreslave cancelado o un error ha ocurrido.\n
21 La restauracion de la base de datos ha sido finalizada. Fallo al inicializar applyinfo. (db_creation_time='%1$s', pageid=%2$lld, offset=%3$d)\n
22 El nombre de la base de datos o el nombre de host no ha sido configurado para HA.\n
23 Valor incorrecto '%1$s' para el estado del nodo fuente de backup.\n
60 \
restoreslave: Restaurar una base de datos esclava.\n\
uso: %1$s restoreslave [OPTION] database-name\n\
\n\
opciones validas:\n\
  -s, --source-state=STATE          estado del nodo fuente de backup; obligatorio\n\
                                    STATE puede ser 'master', 'slave', o 'replica'\n\
  -m, --master-host-name=NAME       nombre de host del nodo amo actual; obligatorio\n\
      --list                        mostrar una lista de volumenes de backup sin restaurar\n\
  -B, --backup-file-path=PATH       PATH es una carpeta de volumenes de backup a ser restaurados\n\
  -o, --output-file=FILE            redirigir los mensajes de salida hacia FILE; default: none\n\
  -u, --use-database-location-path  restaurar la base de datos y los volumenes de registro (log) en el camino especificado dentro del archivo de localizacion de la base de datos\n\
  -k, --keys-file-path              Ruta del archivo de claves (_keys) para que TDE se utilice durante la restauración\n

$set 54 MSGCAT_UTIL_SET_DELVOLDB
21 Listo para eliminar los siguentes volumenes:\n\n
22 Desea continuar? (Y/N)
31 No se puede eliminar el volumen de sistema o el primer volumen (Volid : %1$d).\n
32 No se puede encontrar volumen (Volid : %1$d).\n
33 Son demasiados Volids (Max Volids : %1$d).\n
34 Volid (%1$s) invalido.\n
60 \
delvoldb: Eliminar un archivo-volumen vacio de la base de datos.\n\
uso: %1$s delvoldb [OPTION] database-name -i Volid\n\
\n\
opciones validas:\n\
  -c, --clear-cache                borrar todos los archivos de cache en el volumen temporal temp\n\
  -f, --force                      eliminar el volumen sin una verificacion pronta\n\
  -p, --dba-password=PASS          clave del usuario DBA; mostrara si no especifica\n\
  -S, --SA-mode                    modo de ejecucion stand-alone\n\
  -C, --CS-mode                    modo de ejecucion cliente-servidor\n

$set 55 MSGCAT_UTIL_SET_VACUUMDB
20 Vacuumdb no disponible temporalmente para modo cliente-servidor\n
21 La operacion de aspirar ha fallado\n
22 No se puede abrir archivo de salida '%1$s'\n
60 \
vacuumdb: Aspira registros muertos de la base de datos.\n\
uso: %1$s vacuumdb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -S, --SA-mode                modo de ejecucion stand-alone\n\
  -C, --CS-mode                modo de ejecucion cliente-servidor\n\
      --dump                   volcar el estado actual de vacío\n\
  -o, --output-file=FILE       redireccionar mensajes de salida al ARCHIVO; estandar: ninguno\n

$set 56 MSGCAT_UTIL_SET_CHECKSUMDB
1 Fallo al cargar los nombres de tablas en %1$s.\n
2 checksumdb tiene que ejecutar en un servidor de HA activo. %1$s (estado actual: %2$s)\n
3 El propietario de la tabla para guardar el resultado de la suma de comprobación debe ser DBA.\n
58 Caracteristica HA no soportada en esta plataforma.\n
59 checksumdb no puede ejecutar como modo stand-alone.\n
60 \
checksumdb: verifica la integridad de la replicacion.\n\
uso: %1$s checksumdb [OPTION] database-name@hostname\n\
\n\
opciones validas:\n\
  -c, --chunk-size=NUMBER       el numero de filas a seleccionar para cada calculo de checksum\n\
                                default: 500 filas, minimo: 100 filas\n\
  -s, --sleep=NUMBER            sleep time entre los calculos de checksum; default: 100 ms\n\
  -i, --include-class-file=FILE archivo de entrada de los nombres de tabla a ser incluidos\n\
  -e, --exclude-class-file=FILE archivo de entrada de los nombres de tabla a ser excluidos; o -i o -e pueden ser utilizados, no ambos\n\
  -t, --timeout=NUMBER          tiempo de expera de la consulta (query) en milisegundos; default: 1000 ms\n\
  -n, --table-name=NAME         nombre de tabla para guardar los resultados de la checksum; default: dba.db_ha_checksum\n\
  -r, --report-only             reportar integridad de la replicacion en base al calculo de checksum\n\
                                cualquier opcion excepto a --table-name va a ser ignorada\n\
      --resume                  reanudar calculo de checksum\n\
      --schema-only             verificar solo la consistencia de la esquema\n\
      --cont-on-error           continuar en error de calculo\n

$set 57 MSGCAT_UTIL_SET_TDE
21 Ingrese la contraseña de DBA:
25 No se pudo obtener la información clave establecida en la base de datos\n
26 Intentando cambiar la clave de la clave (índice: %1$d) a la clave (índice: %2$d) ..\n
27 La clave se ha cambiado de la clave (índice: %1$d) a la clave (índice: %2$d)\n
28 El conjunto de claves en la base de datos no se puede eliminar\n
29 La clave (índice: %1$d) ha sido eliminada\n
30 Se ha generado una nueva clave: índice de clave: %1$d creado en%2$s
60 \
tde: Gestión de cifrado transparente de datos (TDE)\n\
uso: %1$s tde OPERACIÓN (-s|-n|-d|-c) [OPCIÓN] database-name\n\
\n\
opciones válidas:\n\
    -p, --dba-password=PASS     contraseña del usuario DBA; le preguntará si no especifica\n\
\n\
OPERACIONES: se debe dar una de las operaciones, y son exclusivas\n\
    -s, --show-keys             imprime la información de la clave establecida en la base de datos y el archivo de clave (_keys)\n\
    -n, --generate-new-key      genera una nueva clave en el archivo de claves (_keys); recuento máximo: 128\n\
    -d, --delete-key=KEY_INDEX  eliminar una clave en el archivo de claves (_keys)\n\
    -c, --change-key=KEY_INDEX  cambia el conjunto de claves en la base de datos por otra clave en el archivo de claves (_keys)\n

$set 58 MSGCAT_UTIL_SET_FLASHBACK
1 Introduzca la contraseña de DBA:
2 Formato de fecha no válido, el formato válido es dd-mm-yyyy:hh:mm:ss\n
3 Se requiere que la fecha de inicio (%1$s) sea menor que la fecha de finalización (%2$s)\n
4 Intente con una fecha anterior a la actual (%1$s) o posterior a la creación de la base de datos (%2$s)\n
5 La tabla (%1$s) no existe en la base de datos\n
6 Demasiadas transacciones para flashback. Reduzca el intervalo entre la fecha de inicio y la fecha de finalización\n
7 No se pudo obtener flashback porque se cambió el esquema (%1$s).\n
8 Se ha eliminado el volumen de registro. Intente con la fecha posterior\n
9 No se pudo abrir el archivo de salida '%1$s'\n
10 ID de transacción (%1$d) no existe\n
11 Configure 'supplemental_log' en 'cubrid.conf'. Solo los registros de SQL ejecutados después de configurar 'supplemental_log' pueden recuperarse.\n
12 El ID de transacción debe ingresarse en %1$d segundos. El tiempo de espera se puede ajustar a través de la configuración de 'flashback_timeout' en 'cubrid.conf'.\n
13 Flashback ya se está ejecutando. Dos procesos no pueden ejecutar Flashback.\n
14 Flashback no se puede ejecutar como modo independiente.\n
15 La clase de catálogo del sistema (%1$s) no se admite en flashback.\n
60 \
flashback: proporciona declaraciones para retroceder una transacción específica.\n\
uso: %1$s flashback [OPCIÓN] database-name owner_name.class_name1 [owner_name.class_name2 ...]\n\
\n\
opciones válidas:\n\
    -o, --output-file=ARCHIVO     redirige el mensaje de salida a ARCHIVO; predeterminado: ninguno.\n\
    -u, --user=ID                 especifica el usuario que ejecutó la instrucción SQL para retroceder; predeterminado: TODO.\n\
    -p, --dba-password            contraseña del usuario DBA; le preguntará si no especifica\n\
    -s, --start-date=FECHA        especifica la FECHA de inicio para el flashback. Los registros de SQL se extraen de DATE.\n\
                                  La FECHA se da de la siguiente manera: dd-mm-yyyy:hh:mm:ss.\n\
    -e, --end-date=FECHA          especifica la FECHA de finalización para el flashback. Los registros SQL se extraen hasta la FECHA.\n\
                                  si no se especifica la fecha de inicio, entonces ((END - 10min) ~ END) se establece en rango,\n\
                                  y de lo contrario, si no se especifica END, entonces se establece (start ~ (start + 10min)).\n\
                                  Y no se especifica una fecha de inicio ni una fecha de finalización,\n\
                                  se extraen los registros SQL, ejecutados entre (hora actual - 10 minutos) y la hora actual.\n\
    --detail                      genera información detallada para cada instrucción SQL.\n\
                                  El ID de transacción, el nombre de usuario y la declaración SQL original se devolverán con la\n\
                                  declaración SQL\n\
                                  para retrospectiva; predeterminado: genera solo sentencias SQL para flashback\n\
    --oldest                      flashback más antiguo se realiza en orden cronológico para las sentencias SQL ejecutadas desde\n\
                                  la hora de inicio hasta la hora de finalización; predeterminado: último\n

$set 59 MSGCAT_UTIL_SET_MEMMON
1 Por favor, configure el parámetro del sistema 'enable_memory_monitoring' en 'cubrid.conf' y reinicie el servidor de la base de datos.\n
2 memmon no puede ejecutarse en modo independiente.\n
3 Error al abrir el archivo de salida con el nombre %1$s.\n
60 \
memmon: Proporciona información sobre el uso de memoria de un servidor de base de datos.\n\
uso: %1$s memmon [OPCIÓN] nombre-de-base-de-datos\n\
\n\
opción válida:\n\
    -o, --output-file=ARCHIVO    guardar resultados en ARCHIVO\n
