$
$ Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
$
$   This program is free software; you can redistribute it and/or modify 
$   it under the terms of the GNU General Public License as published by 
$   the Free Software Foundation; either version 2 of the License, or
$   (at your option) any later version.
$
$  This program is distributed in the hope that it will be useful, 
$  but WITHOUT ANY WARRANTY; without even the implied warranty of 
$  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
$  GNU General Public License for more details. 
$
$  You should have received a copy of the GNU General Public License 
$  along with this program; if not, write to the Free Software 
$  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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

$set 1 MSGCAT_UTIL_SET_GENERIC
1 Longitud o caracter '%c' en el nombre de la base de datos '%s'\n no valida.
2 No se puede abrir archivo de salida '%s'\n
3 Nombre de utilidad falta o no valido.\n
4 Nombre de utilidad no valido '%s'\n
5 La siguente lista contiene los nombres de utilidad reconocidos:\n
6 Caracter no valido '%c' en nombre de volumen '%s'\n
9 \
%s (CUBRID utilidades)\n\
%s\n\
Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n
10 \
utilidad de administrador de cubrid, version %s\n\
uso: %s <nombre-de-utilidad> [args]\n\
Escriba '%s <nombre-de-utilidad>' para ayuda sobre una utilidad especifica.\n\
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
\n\
%s es una hieramenta para DBMS.\n\
Para informacion adicional, vea http://www.cubrid.com\n
12 '%s' nombre de utilidad es invalido.\nEscriba 'cubrid --help' para ver uso.\n
13 '%s' comando es invalido.\nEscriba 'cubrid --help' para ver uso.\n
14 No se pueden cargar una o más propiedades obligatorias en los archivos de configuración.\n
15 @ %s %s: %s\n
16 @ %s %s\n
17 ++ %s '%s' no se esta ejecutando.\n
18 ++ %s no se esta ejecutando.\n
19 ++ %s '%s' se esta ejecutando.\n
20 ++ %s se esta ejecutando.\n
21 ++ %s %s: %s\n
22 Uno o mas argumentos faltan.\nEscriba 'cubrid --help' para ver uso.\n
23 utilidad de cubrid, version %s\n\
uso: %s <nombre-de-utilidad> [args]\n\
Escriba '%s <nombre-de-utilidad>' para ayuda sobre una utilidad especifica.\n\
\n\
Utilidades de servicio disponible:\n\
    service\n\
    server\n\
    broker\n\
    manager\n\
    heartbeat\n\
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
    spacedb\n\
    unloaddb\n\
    paramdump\n\
    statdump\n\
    changemode\n\
    copylogdb\n\
    applylogdb\n\
    applyinfo\n\
    genlocale\n\
    dumplocale\n\
    synccolldb\n\
\n\
%s es una hieramenta para DBMS.\n\
Para informacion adicional, vea http://www.cubrid.com\n
24 utilidad de cubrid, version %s\n\
uso: %s service <command>\n\
\n\
Comando disponible:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 utilidad de cubrid, version %s\n\
uso: %s server <command> [args]\n\
\n\
Comando disponible:\n\
    start       [database-name]\n\
    stop        [database-name]\n\
    restart     [database-name]\n\
    status\n\
    acl         <status|reload> database-name\n
37 El servidor ha sido configurado para HA.\n
26 utilidad de cubrid, version %s\n\
uso: %s broker <command> [args]\n\
\n\
Comando disponible:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [<expr>]\n\
    on          broker-name\n\
    off         broker-name\n\
    reset       broker-name\n\
    acl         <status|reload> [broker-name]\n\
    getid       -b <broker-name> [-f] shard-key\n
27 utilidad de cubrid, version %s\n\
uso: %s manager <command>\n\
\n\
Comando disponible: \n\
    start\n\
    stop\n\
    status\n
28 utilidad de cubrid, version %s\n\
uso: %s heartbeat <command> [args]\n\
\n\
Comando disponible: \n\
    start       [database-name]\n\
    stop        [-i|database-name]\n\
    copylogdb   <start|stop> database-name node-name\n\
    applylogdb  <start|stop> database-name node-name\n\
    status\n\
    reload\n
31 Argumento '%s' no es necesario.\n
32 Falta de un nombre de base de datos.\n
33 %s: Tal opcion es depreciada.\n
34 Parametro no valido. %s %s\n
35 No se puede asignar memoria.\n
36 El servidor no ha sido configurado para HA.\n
38 El servidor configurado para HA no es listado en ha_db_list.\n
39 El servidor configurado para HA no es listado en ha_node_list.\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 ADVERTENCIA: -t opcion es ignorada en modo de ejecucion independiente.\n
31 ERROR: Destination-path does not exist or is not a directory.\n
60 \
backupdb: Respaldar una base de datos.\n\
uso: %s backupdb [OPTION] database-name\n\
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
  -z, --compress               comprimir volumenes de respaldo; estandar: descomprimir\n\
  -e, --except-active-log      no incluir un volumen de registro activo; estandar: incluir\n\
      --sleep-msecs=N          dormir N milisecondas para cada 1M leido estandar: 0\n
71 Argumentos incompatibles, \"%s\", y \"%s\"\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 ERROR: Nombre de destino es identico con el nombre de origen.\n
31 ERROR: volext-pathname no existe o no se puede escribir.\n
32 ERROR: vols-tofrom-paths_file no existe o no se puede leer.\n
60 \
copydb: Copiar una base de datos.\n\
uso: %s copydb [OPTION] src-database-name dest-database-name\n\
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
40 Grupo '%s' no existe\n
41 Utilizador '%s' no existe\n
42 Comando desconocido '%s'\n
43 No se puede abrir archivo de salida '%s'\n
44 No se puede eliminar base de datos existente.\n
45 Creating database with %1$s size using locale %2$s. The total amount of disk space needed is %3$s.\n
46 No se pudo crear base de datos.\n
47 Problema con el archivo de utilizador '%s'\n
48 Para el '%s' dado, %s es fuera de rango. Escoger un tamano entre %s y %s.\n
49 El valor de %s '%s' no es valido.\n
60 \
createdb: Crear una base de datos.\n\
usage: %s createdb [OPTION] database-name database-locale\n\
\n\
database-locale format is <language>.<charset>; examples : en_US.iso88591, ko_KR.utf8\n\
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
uso: %s deletedb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -o, --output-file=FILE       redireccionar mensajes de salida al ARCHIVO; estandar: ninguno\n\
  -d, --delete-backup          eliminar volumenes de respaldo; estandar: no eliminar\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 ERROR: volext-pathname no existe o no se puede escribir.\n
32 ERROR: vols-tofrom-paths_file no existe o no se puede leer.\n
60 \
renamedb: Renombrar una base de datos.\n\
uso: %s renamedb [OPTION] src-database-name dest-database-name\n\
\n\
opciones validas:\n\
  -E, --extended-volume-path=PATH RUTA de directorio para volumenes de informacion extendidos; estandar: la misma ruta que la de los volumenes de base de datos o la ruta dada en el archivo de control\n\
  -i, --control-file=FILE         renombrar volumenes sobre varias ubicaciones escritas en el archivo de control; estandar: ninguno\n\
  -d, --delete-backup             eliminar volumenes de respaldo; estandar: no eliminar\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 Ejecutando ya un proceso %s.\n
12 SERVIDOR MAESTRO inicializando. \n
13 SERVIDOR MAESTRO saliendo.\n
14 Error al encontrar nombre de host local\n
15 No se puede encontrar archivo de parametros\n
16 No se puede inicializar proceso maestro\n
17 Servidor %s apagando en %d minutos
18 Servidor %s notificado del cierre.\nEsto puede durar unos minutos. Por favor espere.
19 Server %s no encontrado.
20 Maestro apagando en %d minutos\n
21 Heartbeat failover was finished. Please wait until all shutdown processes of cub_servers are completed.\n

$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 No se puede abrir archivo de salida '%s'\n
60 \
optimizedb: Optimizar una base de datos.\n\
uso: %s optimizedb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -n, --class-name=NAME        actualizar estadisticas para la clase NOMBRE; estandar: todas clases\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 Formato de fecha no valido, formato valido es dd-mm-yyyy:hh:mm:ss o 'backuptime'\n
20 Restoredb cancelado o un error ha ocurrido.\n
60 \
restoredb: Restablecer una base de datos.\n\
uso: %s restoredb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -d, --up-to-date=DATE             restablecer la base de datos a la altura de su condicion en FECHA determinada. FECHA es dada como sigue: dd-mm-yyyy:hh:mm:ss o palabra clave 'backuptime'\n\
      --list                        mostrar una lista de volumenes de respaldo sin restablecer\n\
  -B, --backup-file-path=PATH       RUTA es un directorio de volumenes de respaldo que necesitan ser restablecidos\n\
  -l, --level=LEVEL                 NIVEL de respaldo a restablecer; vea uso de respaldo; estandar: 0(respaldo completo)\n\
  -p, --partial-recovery            realizar recuperacion parcial si alguna carpeta de registro falta\n\
  -o, --output-file=FILE            redireccionar mensajes de salida al ARCHIVO; estandar: ninguno\n\
  -u, --use-database-location-path  restablecer la base de datos y volumenes de registro a la ruta especificada en el archivo de ubicacion de la base de datos\n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 El nombre de base de datos falta.\n
20 Archivo '%s' no tiene %d lineas.\n
21 interrumpir ... \n
22 Carga interrumpida - transaccion actual cometida\n
23 Carga interrumpida - transaccion actual abortada\n
24 No se puede abrir archivo '%s'\n
25 \n*** Verificar sintaxis ***\n\n
26 %d errores detectados.\n
27 \n*** Insertando objetos ***\n\n
28 Total %8d objetos insertados. \n
29 Total %8d objetos referenciados insertados con contenidos estandar.\n
30 \n*** Cometiendo la transaccion ***\n
31 \n*** Cerrando la base de datos ***\n
32 Linea %d:
33 Error al analizar '%s' como un valor '%s' para atributo '%s' de '%s'\n
34 Falta dominio para atributo %s de %s\n
35 Para atributo %s de %s, tipo %s no es permitido en el dominio del conjunto\n
36 Para atributo %s de %s, tipo esperado %s, ha obtenido una constante de conjunto\n
37 Para atributo %s de %s, tipo esperado %s, ha obtenido %s\n
38 Ninguna clase con el nombre %s referenciada por el atributo %s de %s\n
39 Ninguna clase con el nombre %s, ignorando tuplas\n
40 Ninguna clase con identificador %d referenciada por el atributo %s de %s\n
41 Clase %s no es permitida en el archivo de entrada del cargador\n
42 Terminado con error.\n
43 ADVERTENCIA: actualizado instancia existente!\n
44 ADVERTENCIA: redefiniendo Instancia %d de la clase %s\n
45 Instancia %d de la clase %s ha sido ya definida\n
46 Instancia %d de la clase %s ha sido ya reservada\n
47 Violacion UNIQUE en atributo '%s', convertida en NULL\n
48 %d instancias\n
49 Clase %s\n
50 Contrasena:
51 \n*** Actualizando estadisticas de clase ***\n
52 %s cerca de la linea %d (texto era '%s')\n
53 Error lexical
54 linea %d: error de sintaxis en \"%s\"
55 faltando
56 en %s
57 Argumentos incompatibles, \"%s\", y \"%s\"\n
58 %d instancias cometidas\n
59 No se pueden apelar versiones no-optimizadas de loaddb.\n
60 Error al analizar \"%s\", como tipo %s.\n
112 %d instancias\r
113 Ultima linea cometida: %d
114 error-control-file
115 archivo de entrada para controlar error(es) al cargar
120 \
loaddb: Importar objetos y esquemas en la base de datos.\n\
uso: %s loaddb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -u, --user=ID                 ID de utilizador para acceso a la base de datos; estandar: "public"\n\
  -p, --password=PASS           contrasena de utilizador; estandar: ninguna\n\
      --data-file-check-only    verificar sintaxis para archivo de datos sin cargar; estandar: cargar despues de verificar\n\
  -l, --load-only               cargar archivo de datos sin verificar sintaxis; estandar: cargar despues de verificar\n\
      --estimated-size=NUMBER   NUMERO estimado de instancias; estandar: 5000\n\
  -v, --verbose                 habilitar mensajes de estado verbose; estandar: inhabilitado\n\
  -c, --periodic-commit=COUNT   insertar COUNT al cometer periodicamente; estandar: no cometer mientras cargando\n\
      --no-oid                  no utilizar OID\n\
      --no-statistics           no actualizar las estadisticas mientras cargando\n\
  -s, --schema-file=FILE[:LINE] crear esquema de la LINEA con ARCHIVO\n\
  -i, --index-file=FILE[:LINE]  crear indice de la LINEA CON ARCHIVO\n\
  -d, --data-file=FILE          cargar datos con ARCHIVO\n\
  -t, --table=TABLE             nombre de tabla que es sustituido por falta de encabezamiento de clase en archivo de datos\n\
      --error-control-file=FILE ARCHIVO para controlar error(es) al cargar\n\
      --ignore-class-file=FILE  archivo de entrada de nombres de clase que saltan carga\n
121 \
load_noopt: Esta utilidad es depreciada. Sea atento al utilizar.\n\
uso: load_noopt [OPTION] database-name\n\
\n\
opciones validos:\n\
  -u, --user=ID                 ID de utilizador para acceso a la base de datos; estandar: "public"\n\
  -p, --password=PASS           contrasena de utilizador; estandar: ninguna\n\
      --data-file-check-only    verificar sintaxis para archivo de datos sin carga; estandar: cargar despues de verificar\n\
  -l, --load-only               cargar archivo de datos sin verificar sintaxis; estandar: cargar despues de verificar\n\
      --estimated-size=NUMBER   NUMERO estimado de instancias; estandar: 5000\n\
  -v, --verbose                 habilitar mensajes de estado verbose; estandar: inhabilitado\n\
  -c, --periodic-commit=COUNT   insertar COUNT al cometer periodicamente; estandar: no cometer mientras cargando\n\
      --no-oid                  no utilizar OID\n\
  -s, --schema-file=FILE[:LINE] crear esquema de la LINEA con ARCHIVO\n\
  -i, --index-file=FILE[:LINE]  crear indice de la LINEA con ARCHIVO\n\
  -d, --data-file=FILE          cargar datos con ARCHIVO\n\
      --error-control-file=FILE ARCHIVO para controlar error(es) mientras cargando\n


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 valor de cached-pages no es valido.\n
42 valor de cached-page-size no es valido.\n
43 %d objetos botados.\n
44 Clase %s
45 , %d instancias.\n
46 Solo %d de %d objetos botados con exito.\n
47 directorio de salida es demasiado largo.\n
48 LSA : %d %d \n
60 \
unloaddb: Exportar objetos y esquemas de la base de datos.\n\
uso: %s unloaddb [OPTION] database-name\n\
\n\
opciones validas:\n\
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
  -v, --verbose               habilitar mensajes de estado verbose; estandar: inhabilitado\n\
      --use-delimiter         utilizar '"' donde un identificador empieza y acaba; estandar: no utilizar\n\
  -S, --SA-mode               modo de ejecucion independiente\n\
  -C, --CS-mode               modo de ejecucion cliente-servidor\n\
      --datafile-per-class    crear un archivo de objeto para cada clase; estandar: inhabilitado\n



$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 \nPass 1\n\n
12 %d objetos procesados.\n
13 \nPass 2\n\n
14 Clase %s\n
15 oid:  %d|%d|%d,\n
16 %d instancias.\n
17 Actualizando objeto.\n
18 ref_oid:  %d.%d.%d, ref_class_oid:  %d.%d.%d\n
19 No se pudo transformar objeto\n
20 Falta de archivo heap de clase\n
21 No se pudo actualizar objeto\n
22 Solo %d de %d objetos procesados con exito.\n
23 COMPACTDB ya inicializado.\n
24 El numero de paginas tiene que ser entre %d y %d.\n
25 El tiempo de expirar para el bloqueo de instancia tiene que ser entre %d y %d.\n
26 Total objetos: %d\n
27 Objetos fracasados: %d\n
28 Objetos modificados: %d\n
29 Objetos grandes: %d\n
30 Representaciones viejas de clase eliminadas del catalogo.\n
31 Representaciones viejas de clase preservadas en catalogo.\n
32 No se puede ajustar el nivel de aislamiento.\n
33 No se puede compactar la base de datos.\n
34 El tiempo de expirar del bloqueo de clase tiene que ser entre %d y %d.\n
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
60 \
compactdb: Spacio libre no utilizado en una base de datos.\n\
uso: %s compactdb [OPTION] database-name [class_name1 class_name2 ...]\n\
\n\
opciones validas:\n\
  -v, --verbose                     habilitar mensajes de estado verbose; estandar: inhabilitado\n\
  -i, --input-class-file=FILE       ARCHIVO de entrada de nombres de tabla\n\
                                    lista class_name debe ser vacia\n\
                                    estandar: compactar todas clases\n\
                                    habilitado solo en modo CS\n\
  -p, --pages-commited-once=NUMBER  numero maximo de paginas para cometer a la vez\n\
                                    estandar: 10\n\
                                    habilitado solo en modo CS\n\
  -S, --SA-mode                     modo de ejecucion independiente\n\
  -C, --CS-mode                     modo de ejecucion cliente-servidor\n\
  -d, --delete-old-repr             eliminar representaciones viejas de clase del catalogo; estandar: inhabilitado\n\
  class_name_list                   habilitado solo en modo CS\n\
  -I, --Instance-lock-timeout       tiempo de expirar para bloqueo de instancia\n\
  -c, --class-lock-timeout          tiempo de expirar para bloqueo de clase\n

$set 15 MSGCAT_UTIL_SET_COMMDB
21 CUBRID master ejecutado en host %s \nha sido reinicializado a %s
22 ha servido %d peticiones. %d servidor(es) que ejecutan.\n
23 APAGANDO en %s minutos\n
24 %s
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
  -D, --dereg-process=PID          salir del proceso de destino y eliminarlo de las entradas de proceso patrocinadas\n\
  -F, --reconfig-node-list         aplicar la configuracion CUBRID heartbeat recien cambiada\n\
  -U, --deactivate-heartbeat       inhabilitar caracteristica CUBRID heartbeat y eliminar el nodo actual del grupo de nodos\n\
  -i, --immediately                Immediately kill active ones, if one of HA processes are not terminated while CUBRID heartbeat is shutting down; with -U option; default: disabled\n\
  -T, --activate-heartbeat         habilitar caracteristica CUBRID heartbeat y nodo actual se convierte en un miembro del grupo de nodos\n\
  -V, --verbose                    habilitar mensajes de estado verbose; estandar: inhabilitado\n
28 Disculpe, comando desconocido: %s\n
29 No se puede obtener el nombre de host para el host local - abortando\n
30 No se puede encontrar archivo de parametro\n
31 No se puede conectar al servidor maestro en %s\n
32 Cuantos minutos para el cierre? :
33 Maestro se va a apagar en %d minutos\n
34 Error al recibir datos del cliente
35 el servidor de base de datos %s no esta actualmente ejecutando
36 no se puede ejecutar shell (%s) para %s, %s
37 proceso fork %s, regreso %d, %s
38 no se puede ejecutar %s
39 -i(--immediately) option must be used with -U(--deactivate-heartbeat) option.\n

$set 16 MSGCAT_UTIL_SET_PATCHDB
60 \
emergency_patchlog: Parchar una base de datos en caso de emergencia.\n\
uso: %s emergency_patchlog [OPTION] database-name\n\
\n\
opciones validas:\n\
  -r, --recreate-log           recrear archivo de registro; utilizar atentamente\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 Valor equivocado %d para npages\n
21 Valor equivocado '%s' para proposito de almacenamiento de volumen.\n
22 ADVERTENCIA: opcion --max-writesize-in-sec es ignorada en modo de ejecucion stand-alone.\n
60 \
addvoldb: Agregar un archivo de volumen para la base de datos.\n\
uso: %s addvoldb [OPTION] database-name\n\
\n\
opciones validas:\n\
      --db-volume-size=SIZE        tamano de volumen adicional; estandar: db_volume_size en cubrid.conf\n\
      --max-writesize-in-sec=SIZE  la cantidad de volumen escrita por segundo; (ex. 512K, 1M, 10M); estandar: no utilizado; minimum: 160K\n\
  -n, --volume-name=NAME           NOMBRE de volumen de informacion; estandar: generar como "db"_ext1)\n\
  -F, --file-path=PATH             RUTA para archivo de agregacion volumen; estandar: directorio de trabajo\n\
      --comment=COMMENT            COMENTARIO para archivo de agregacion volumen; estandar: ninguno\n\
  -p, --purpose=PURPOSE            PROPOSITO para archivo de agregacion volumen; permitido:\n\
                                           DATA - solo para datos\n\
                                           INDEX - solo para indice\n\
                                           TEMP - solo para temporario\n\
                                           GENERIC - para todos propositos\n\
  -S, --SA-mode                    modo de ejecucion independiente\n\
  -C, --CS-mode                    modo de ejecucion cliente-servidor\n

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 Algunas inconsistencias han sido detectadas en su base de datos.\n Por favor consulte error_log_file %s para informacion adicional.\n
21 %s: No se puede encontrar la clase. Va a ser omitida.\n
60 \
checkdb: Verificar consistencia de la base de datos.\n\
uso: %s checkdb [OPTION] database-name [class_name1 class_name2 ...]\n\
\n\
opciones validas:\n\
  -S, --SA-mode                modo de ejecucion independiente\n\
  -C, --CS-mode                modo de ejecucion cliente-servidor\n\
  -r, --repair                 reparar cuando se encuentran inconsistencias\n\
  -i, --input-class-file=FILE  entrar ARCHIVO de nombres de clase\n\
                               estandar: verificar todas clases\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
15 Descripcion de espacio para la base de datos '%s' con pagesize %s. (log pagesize: %s)\n\n
16 %5d %9s  %s %s  %s\n
17 No se puede abrir archivo de salida '%s'\n
18 Descripcion de espacio para volumenes temporarios para la base de datos '%s' con pagesize %s.\n\n
19 Volid  Purpose   total_pages  free_pages  Vol Name\n\n
20 Volid  Purpose    total_size   free_size  Vol Name\n\n
21 Descripcion de espacio LOB %s \n\n
22 -------------------------------------------------------------------------------\n
30 Descripcion de espacio resumida para la base de datos '%s' con pagesize %s. (log pagesize: %s)\n\n
31 Purpose    total_pages  used_pages  free_pages  volume_count\n
32 Purpose     total_size   used_size   free_size  volume_count\n
33 %10s %s %s %s %10d\n
34 -------------------------------------------------------------\n
40 Volid  Purpose   total_pages  free_pages  data_pages index_pages  temp_pages  Vol Name\n\n
41 Volid  Purpose    total_size   free_size   data_size  index_size   temp_size  Vol Name\n\n
42 %5d %9s  %s %s %s %s %s  %s\n
43 ----------------------------------------------------------------------------------------------------\n
50 Purpose    total_pages  used_pages  free_pages  data_pages index_pages  temp_pages  volume_count\n
51 Purpose     total_size   used_size   free_size   data_size  index_size   temp_size  volume_count\n
52 %10s %s %s %s %s %s %s %10d\n
53 -------------------------------------------------------------------------------------------------\n
60 \
spacedb: Mostrar espacio usado/libre de la base de datos.\n\
uso: %s spacedb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -o, --output-file=FILE       redireccionar mensajes de salida al ARCHIVO; estandar: ninguno\n\
  -S, --SA-mode                modo de ejecucion independiente\n\
  -C, --CS-mode                modo de ejecucion cliente-servidor\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               ajustar la unidad de visualizacion (pagina, mega, giga, tera, legible por humanos); estandar: H\n\
  -s, --summarize              mostrar solo un resumen para cada proposito de volumen\n\
  -p, --purpose                display the used size of each purpose(data/index/temp) of each volume\n


$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 Numero estimado de Paginas = %d\n
60 \
estimate_data: Estimar tamano de datos.\n\
uso: %s estimate_data number-of-instances average-instance-size number-of-attributes variable-number-of-attributes\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 Tipo malo de clave del indice especificado = %s\n
16 Mala longitud media de clave especificada = %d cuando precision = %d\n
17 Argumentos malos especificados\n
20 Numero estimado del total de paginas de indice = %d\n (Cuando indice creado, despues de que las instancias sean pobladas.)\n
21 Numero estimado del total de paginas de indice = %d\n (Cuando indice creado, antes de que las instancias sean pobladas (caso medio))\n
22 Numero estimado del total de paginas de indice = %d\n (Cuando indice creado, antes de que las instancias sean pobladas (peor caso))\n
23 Para la entrada dada...\n
24 Numero total de instancias : %d\n
25 Numero de claves diferentes: %d\n
26 Tamano medio de clave      : %d\n
27 Tipo de clave              : %s\n\n
60 \
estimate_index: Tamano estimado de un indice.\n\
uso: %s estimate_index number-of-instances number-of-different-keys average-key-size key-type\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: Instalar una base de datos.\n\
uso: %s installdb [OPTION] database-name\n\
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
7 Error al abrir archivo de entrada ELO "%s"\n
8 CLASES:
9 Nombre de archivo hash
10 Nombre: 			 %s\n
11 Tamano:                       %d\n
12 Tamano de pagina:             %d\n
13 Tamano de datos:              %d\n
14 Tamano de entrada:            %d\n
15 Entradas por pagina:          %d\n
16 Paginas almacenadas:          %d\n
17 Numero de entradas:           %d\n
18 Numero de colisiones:         %d\n
19 Nombre de archivo hash:       %s\n
20 Siguente entrada de sobreflujo: %d\n
21 Tipo de clave:                %s\n
22 Direccion de encabezamientos de pagina: %x\n
23 Direccion del ultimo encabezamiento de pagina: %x\n
24 Direccion del encabezamiento de pagina libre: %x\n
25 Direccion del mapa de bits de la pagina: %x\n
26 Tamano del mapa de bits de la pagina: %d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
60 \
diagdb: Imprimir una base de datos.\n\
uso: %s diagdb [OPTION] database-name\n\
\n\
opciones validas:\n\
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
15 No se puede abrir archivo de salida '%s'\n
59 lockdb no puede ser ejecutado en modo independiente.\n
60 \
lockdb: Mostrar estado de bloqueo de la base de datos.\n\
uso : %s lockdb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -o, --output-file=FILE       redireccionar mensajes de salida al ARCHIVO; estandar: ninguno\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 Solo un argumento debe ser dado entre: tran-index, user, host, progname\n
21 Introducir contrasena DBA:
26 No hay ninguna transaccion con los valores de argumento especificados.\n
27 Listo para matar las siguentes transacciones:\n
28 Desea continuar? (Y/N)
29 Matando transaccion asociada con el indice de transaccion %d\n
30 Fracaso al matar las siguentes transacciones\n\n
31 La transaccion todavia no acaba, pero puede que haga trabajo de reversion.\n Por favor, espere un segundo y reintentar luego si todavia necesario.\n
32 Indice de transaccion tranindex(%s) no valido.\n
59 killtran no puede ejecutar en modo independiente.\n
60 \
killtran: Patrocinar transacciones de una base de datos.\n\
uso: %s killtran [OPTION] database-name\n\
\n\
opciones validas:\n\
  -i, --kill-transaction-index=INDEX matar transaccion con INDICE de transaccion\n\
      --kill-user-name=ID            matar todas transacciones con ID de utilizador\n\
      --kill-host-name=HOST          matar todas transacciones con HOST de cliente\n\
      --kill-program-name=NAME       matar todas transacciones con NOMBRE de programa de cliente\n\
      --kill-sql-id=SQL_ID           matar todas transacciones con SQL_ID\n\
  -p, --dba-password=PASS            contrasena del utilizador DBA; va a ser pedida si no es especificado\n\
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
20 la base de datos '%s' no existe.
21 '%s' no puede ser migrada a %s, porque su version DB es demasiado baja.\n
22 '%s' no necesitan ser migrados.\n
23 '%s' necesita ser migrado.\n
24 Respaldar es recomendado despues de migracion. Quiere respaldar su base de datos ahora? (si/no):
25 Le recomendamos respaldar su nueva base de datos con el comando backupdb despues de la migracion.
26 \n*** ADVERTENCIA: Respaldar su base de datos antes de inicializar la migracion hacia %s ***\n\nPara recuperar errores que pueden haber ocurrido durante la migracion, le recomendamos muy fuerte respaldar su base de datos.\nHa ya creado un respaldo para su base de datos? (si/no):
27 \n1. Ejecutar respaldo \n2. Introducir la nueva ruta de respaldo para la base de datos nueva. (ruta actual: %s)\n3. Salir \n
28 \n La nueva ruta de respaldo para la base de datos:
29 \nMigracion y respaldo han sido finalizados con exito.
30 Fracaso al respaldar. Por favor, reintente respaldar su base de datos.
31 \n\n*** ADVERTENCIA : Una vez que la migracion es finalizada, Ud no puede restablecer la base de datos con volumenes de respaldo anteriores.\nNotar que la version anterior de los archivos de respaldo pueden ser restablecidos solo en versiones correspondientes de restoredb.\nPor favor, verifique que los volumenes de respaldo anteriores son guardados a un nivel alto de seguridad.\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: Alterar una host de la base de datos.\n\
uso: %s alterdbhost [OPTION] database-name\n\
\n\
opciones validas:\n\
  -h, --host=HOST              nombre nuevo de HOST; estandar: nombre actual de host\n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 nombre-de-base-de-datos
2 argumento pedido, no hay estandar
3 archivo-de-clase-java
4 argumento pedido, no hay estandar
5 forzar sobrescribir
6 sobrescribir archivo de clase si existe, estandar no

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 No se puede abrir archivo de salida '%s'\n
59 plandump no puede ejecutar como modo independiente.\n
60 \
plandump: Mostrar o controlar planos de consulta almacenados.\n\
uso: %s plandump [OPTION] database-name\n\
\n\
opciones validas:\n\
  -d, --drop                   soltar todos planes en la memoria cache del servidor\n\
  -o, --output-file=FILE       redireccionar mensajes de salida al ARCHIVO; estandar: ninguno\n  

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 No se puede abrir archivo de salida '%s'\n
21 Parametros de sistema a ser utilizados por el programa cliente.\n
22 Parametros de sistema cargados por el servidor '%s'\n
23 Parametros de sistema a ser utilizados en modo independiente.\n
60 \
paramdump: Imprimir ajustes de los parametros de sistema.\n\
uso: %s paramdump [OPTION] database-name\n\
\n\
opciones validas:\n\
  -o, --output-file=FILE       redireccionar mensaje de salida al ARCHIVO; estandar: ninguno\n\
  -b, --both                   imprimir parametros de cliente y servidor\n\
  -S, --SA-mode                modo de ejecucion independiente\n\
  -C, --CS-mode                modo de ejecucion cliente-servidor\n

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 Valor equivocado \"%s\" para el modo HA de ejecucion del servidor.\n
12 No se puede cambiar el modo de ejecucion HA del servidor.\n
21 Introducir contrasena DBA:
22 El modo HA actual de ejecucion del servidor '%s' es %s.\n
23 El modo HA de ejecucion del servidor '%s' cambiado a \"%s\".\n
24 El servidor no ha sido configurado para HA.\n
58 Caracteristica HA no soportada en esta plataforma.\n
59 changemode no puede ejecutar como modo independiente.\n
60 \
changemode: Mostrar o cambiar el modo HA de ejecucion del servidor.\n\
uso: %s changemode [OPTION] database-name\n\
\n\
opciones validas:\n\
  -m, --mode=MODE             cambiar el modo de servidore en MODE; estandar: ninguno\n\
  -f, --force                 forzar cambiar\n\
  -t, --timeout=S             espedando S segundos mientras la transaccion activa es terminada normalmente\n\
                              opcion -t es eficaz solo cuando el utilizador cambia el modo del servidor en mantenimiento\n\
                              estandar: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 Valor equivocado \"%s\" para el modo de ejecucion de copylogdb.\n
21 Introducir contrasena DBA:
22 El servidor no ha sido configurado para HA.\n
58 Caracteristica HA no es soportada en esta plataforma.\n
59 copylogdb no puede ejecutar en modo independiente.\n
60 \
copylogdb: Copiar paginas de registro del servidor.\n\
uso: %s copylogdb [OPTION] database-name\n\
\n\
opciones validas: \n\
  -L, --log-path=PATH         RUTA de directorio para los volumenes de registro copiadas\n\
  -m, --mode=MODE             modo de operacion; estandar: sync\n\
                              MODO: sync o semisync o async\n

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 Introducir contrasena DBA:
22 El servidor no ha sido configurado para HA. \n
58 Caracteristica HA no es soportada en esta plataforma.\n
59 applylogdb no puede ejecutar como modo independiente.\n
60 \
applylogdb: Aplicar registro copiado al servidor.\n\
uso: %s applylogdb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -L, --log-path=PATH         RUTA de directorio de los volumenes de registro copiados\n\
      --max-mem-size=SIZE     TAMANO maximo de proceso en MB; max 500\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 No se puede abrir archivo de salida '%s'\n
59 statdump no puede ejecutar como modo independiente.\n
60 \
statdump: Imprimir estadisticas globales.\n\
uso: %s statdump [OPTION] database-name\n\
\n\
opciones validas:\n\
  -o, --output-file=FILE       redireccionar mensaje de salida al ARCHIVO; estandar: ninguno\n\
  -c, --cumulative             imprimir estadisticas cumulativas\n\
  -i, --interval=S             imprimir estadisticas casa S segundos\n\
  -s, --substr=STRING          imprimir solo aquellos articulos de estadistica que emparejan con el substring\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 Introducir contrasena DBA:
22 El servidor no ha sido configurado para HA. \n
58 Caracteristica HA no es soportada en esta plataforma.\n
59 applyinfo no puede ejecutar como modo independiente.\n
60 \
applyinfo: mostrar informacion HA Apply de CUBRID.\n\
uso: %s applyinfo [OPTION] database-name\n\
\n\
opciones validas:\n\
  -r, --remote-host-name      nombre de host remote; mostrar la informacion de registro activa del nodo remote \n\
  -a, --applied-info 	      mostrar informacion aplicada \n\
  -L, --copied-log-path=PATH  ruta de los volumenes de registro copiados; mostrar informacion de registro copiado \n\
  -p, --pageid=ID             id de pagina; estandar : 0(pagina activa)\n\
  -v, --verbose               habilitar mensajes de estado verbose; estandar : inhabilitar\n\
  -i, --interval=S            imprimir informacion casa S segundos\n

$set 45 MSGCAT_UTIL_SET_ACLDB
59 acldb no puede ejecutar como modo independiente.\n
60 \
acldb: Mostrar o recargar ajuste de control de acceso de la base de datos.\n\
uso: %s acldb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -r, --reload       recargar archivo de control ip\n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
genlocale: Genera biblioteca local compartida utilizando el archivo de entrada LDML.\n\
uso: %s genlocale [OPTION] [language-string]\n\
\n\
language-string	      string de idioma local para cual generar biblioteca compartida.\n\
		      Si no hay ningun string de idioma dado, todos los idiomas en 'cubrid_locales.txt'\n\
		      van a ser procesados.\n\
opciones validas:\n\
  -v, --verbose	              habilitar mensajes de estado verbose; estandar : inhabilitar\n\
  -i, --input-ldml-file       ruta para archivo de entrada LDML. Solo cuando el string de idioma es dado.\n
61 Argumento de nombre local no valido.\n

$set 47 MSGCAT_UTIL_SET_DUMPLOCALE
57 No se pueden utilizar simultaneamente -i/--input-file y el identificador local.\n
58 Rango codepoint no valido (-s -e).\n
59 Argumento de nombre local no valido.\n
60 \
dumplocale: Abandona el contenido de la biblioteca local compartida generada anteriormente utilizando el archivo de entrada LDML.\n\
uso: %s dumplocale [OPTION] [language-string]\n\
\n\
language-string	 string de idiomas local para cual imprimir biblioteca compartida local.\n\
		         Si no hay ningun string de idioma dado, todos los idiomas en 'cubrid_locales.txt'\n\
		         van a ser procesados.\n\
opciones validas:\n\
  -i, --input-file    ruta para biblioteca local compartida de entrada (generada anteriormente).\n\
                      Puede ser utilizada solo cuando no hay ningun string de idioma dado.\n\
  -d, --calendar      imprimir calendar y la informacion fecha/tiempo; estandar : inhabilitar\n\
  -n, --numeric		  imprimir informacion numerica; estandar : inhabilitar\n\
  -a, --alphabet=l|lower|u|upper|both	     imprimir alfabeto y informacion de carcasa; estandar : inhabilitar\n\
      --identifier-alphabet=l|lower|u|upper	 imprimir alfabeto y informacion de carcasapara identificadores; estandar : inhabilitar\n\
  -c, --codepoint-order	 imprimir informacion de colacion ordenada basado en valores codepoint\n\
                         (cp, char, weight, next-cp, char y weight);\n\
                         estandar : inhabilitar\n\
  -w, --weight-order  imprimir informacion de colacion ordenada basado en valores weight\n\
                      (weight, cp, char); estandar : disable\n\
  -s, --start-value	  opcion de filtrar; inicializando codepoint para\n\
                      -a, --identificador-alfabeto, -c, -w; estandar : 0\n\
  -e, --end-value     opcion de filtrar; terminando codepoint para \n\
                      -a, --identificador-alfabeto, -c, -w; \n\
                      estandar : valor maximo leido de la biblioteca compartida local\n\
  -k, --console-conversion  imprimir datos de la conversion de consola; estandar : inhabilitar\n\
  -z, --normalization   imprimir datos de normalizacion; estandar : inhabilitar\n

$set 48 MSGCAT_UTIL_SET_SYNCCOLLDB
43 Particionamiento debe ser eliminado antes de cambiar la colacion actual '%s" de su atributo de paticionamiento.\nClass name\n
44 Claves externas en atributos teniendo colacion '%s' deben ser bajadas antes de cambiar la colacion de atributos.\nTable | Foreign Key\n
45 Clases teniendo colacion '%s'; deberian ser redefinidas:\nClass name\n
46 Indices de funcion teniendo expresion que contiene colacion '%s'; deberian ser redefinidas:\n\
Tabla | Indice | Expresion de funcion\n
47 Sincronizacion de colaciones no ha sido ejecutada. La base de datos no ha sido cambiada.\n
48 Contenido de '%s' tabla de sistema ha sido actualizado con nuevas colaciones de sistema.\n
49 Actualizar colaciones de sistema podria causar corrupcion de la base de datos. Desea continuar (y/n)?\n
50 Colacion '%s' (Id: %d) no encontrada el la base de datos o cambiada en la nueva configuracion de sistema.\n
51 Disparadores teniendo condicion que tiene colacion '%s'; deberian ser redefinidos:\nTrigger | Condition\n
52 Vistas de consulta que contienen colacion '%s'; deberian ser redefinidas:\nView | Query\n
53 Atributos teniendo colacion '%s'; deberian ser redefinidos.\nTable | Attribute Domain\n
54 Intervención manual en la base de datos se requiere.\n\
Por favor verificar archivo '%s' y ejecutarlo antes de forzar colaciones en la base de datos.\n
55 Sincronizacion no es necesaria.\n
56 Sincronizacion de colacion de sistema en la base de datos es requerida.\n\
Ejecutar 'cubrid synccolldb -f %s'\n
57 Hay %d colaciones nuevas en la configuracion de sistema comparado con la base de datos:\n
58 Todas colaciones en la base de datos '%s' son encontradas en la nueva configuracion de sistema.\n
59 Hay %d colaciones en la base de datos que no son configuradas o son cambiadas comparado con las colaciones del sistema.\n
60 \
synccolldb: Sincroniza las colaciones de la base de datos con la colaciones del sistema.\n\
uso: %s synccolldb [OPTION] database-name\n\
\n\
opciones validas:\n\
  -c, --check-only    Verifica las colaciones de la base de datos con las colaciones del sistema.\n\
		      Si se requiere sincronizacion de colaciones, codigo de error 1 es returnado.\n\
		      Muestra un informe de las colaciones incompatibles.\n\
  -f, --force-only    Actualiza las colaciones en la base de datos para que sean iguales con las colaciones de la nueva configuracion del sistema.\n\

$set 49 MSGCAT_UTIL_SET_TRANLIST
20 Introducir %s clave: 
22 Tran index          User name      Host name      Process id      Program name\n
23 -------------------------------------------------------------------------------\n
24 %4d%-10s %14s %14s %15d %17s\n
25 No hay transacciones\n
26 tranlist puede ser ejecutada solamente por el DBA or por un miembro del grupo DBA.\n
27 sort-key column number(%d) is invalid.\n
32 Tran index         Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
33 %4d%-10s %14d    %-25s %10.2f %12.2f    %28s     %13s   %s\n
34 --------------------------------------------------------------------------------------------------------------------------------------------------\n
42 Tran index          User name      Host name      Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
43 %4d%-10s %14s %14s %14d    %-25s %10.2f %12.2f    %28s     %13s   %s\n
44 ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 Tran index: %d
46 SQL_ID: %s\n
59 tranlist no puede ser ejecutado en modo standalone.\n
60 \
tranlist: Imprimir informacion de transaccion activa.\n\
uso: %s tranlist [OPTION] database-name\n\
\n\
opciones validas:\n\
  -u, --user=ID                ID de utilizador para acceso a la base de datos; estandar: utilizador DBA\n\
  -p, --password=PASS          clave del utilizador; estandar: ninguna\n\
  -s, --summary                imprimir resume de informacion de la transaccion\n\
      --sort-key=COLUMN_NUMBER column number to be sorted\n\
      --reverse                reverse the sort result\n
