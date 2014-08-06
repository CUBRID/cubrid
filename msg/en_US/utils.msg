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
$       50      prefetchlogdb

$set 1 MSGCAT_UTIL_SET_GENERIC
1 Invalid length or character '%1$c' in database name '%2$s'\n
2 Couldn't open output file '%1$s'\n
3 Missing or invalid utility name.\n
4 Invalid utility name '%1$s'\n
5 The following list contains the recognized utility names:\n
6 Invalid character '%1$c' in volume name '%2$s'\n
9 \
%1$s (CUBRID utilities)\n\
%2$s\n\
Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n
10 \
cubrid administrator utility, version %1$s\n\
usage: %2$s <utility-name> [args]\n\
Type '%3$s <utility-name>' for help on a specific utility.\n\
\n\
Available admin utility-name:\n\
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
    prefetchlogdb\n\
\n\
%4$s is a tool for DBMS.\n\
For additional information, see http://www.cubrid.org\n
12 '%1$s' utility-name is invalid.\nType 'cubrid --help' to see usage.\n
13 '%1$s' command is invalid.\nType 'cubrid --help' to see usage.\n
14 Could not load one or more mandatory properties in configuration files.\n
15 @ %1$s %2$s: %3$s\n
16 @ %1$s %2$s\n
17 ++ %1$s '%2$s' is not running.\n
18 ++ %1$s is not running.\n
19 ++ %1$s '%2$s' is running.\n
20 ++ %1$s is running.\n
21 ++ %1$s %2$s: %3$s\n
22 One or more arguments are missing.\nType 'cubrid --help' to see usage.\n
23 cubrid utility, version %1$s\n\
usage: %2$s <utility-name> [args]\n\
Type '%3$s <utility-name>' for help on a specific utility.\n\
\n\
Available service's utilities:\n\
    service\n\
    server\n\
    broker\n\
    manager\n\
    heartbeat\n\
\n\
Available administrator's utilities:\n\
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
    applyinfo\n\
    genlocale\n\
    dumplocale\n\
    synccolldb\n\
\n\
%4$s is a tool for DBMS.\n\
For additional information, see http://www.cubrid.org\n
24 cubrid utility, version %1$s\n\
usage: %2$s service <command>\n\
\n\
Available command:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 cubrid utility, version %1$s\n\
usage: %2$s server <command> [args]\n\
\n\
Available command:\n\
    start       [database-name]\n\
    stop        [database-name]\n\
    restart     [database-name]\n\
    status\n\
    acl         <status|reload> database-name\n
37 The server was configured for HA.\n
26 cubrid utility, version %1$s\n\
usage: %2$s broker <command> [args]\n\
\n\
Available command:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [-S] [-P] [-m] [-c] [<expr>]\n\
    on          broker-name\n\
    off         broker-name\n\
    reset       broker-name\n\
    acl         <status|reload> [broker-name]\n\
    getid       -b <broker-name> [-f] shard-key\n\
    test        broker-name [-D <database_name>] [-u <user_name>] [-p <user_password>] [-c <SQL_command>] [-i <input_file>] [-o <output_file>] [-v] [-s]\n
27 cubrid utility, version %1$s\n\
usage: %2$s manager <command>\n\
\n\
Available command:\n\
    start\n\
    stop\n\
    status\n
28 cubrid utility, version %1$s\n\
usage: %2$s heartbeat <command> [args]\n\
\n\
Available command:\n\
    start          [database-name]\n\
    stop           [-h <hostname>] [-i] [database-name]\n\
    copylogdb      <start|stop> database-name node-name\n\
    applylogdb     <start|stop> database-name node-name\n\
    prefetchlogdb  <start|stop> database-name node-name\n\
    replication    <start|stop> node-name\n\
    status         [-v]\n\
    reload\n
31 '%1$s' argument is not needed.\n
32 A database-name is missing.\n
33 %1$s: Such option is deprecated.\n
34 Invalid Parameter. %1$s %2$s\n
35 Cannot allocate memory.\n
36 The server was not configured for HA.\n
38 The server configured for HA is not listed in ha_db_list.\n
39 The server configured for HA is not listed in ha_node_list.\n
40 Invalid command.\n
41 cubrid manager server is not installed.\n
42 Invalid argument.\n
43 File(%1$s) open error.\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 WARNING: -t option is ignored in stand-alone mode execution.\n
31 ERROR: Destination-path does not exist or is not a directory.\n
60 \
backupdb: Backup a database.\n\
usage: %1$s backupdb [OPTION] database-name\n\
\n\
valid options:\n\
  -D, --destination-path=PATH  store backup volumes to directory PATH; default: database's log directory\n\
  -r, --remove-archive         delete unnecessary log-archives; use carefully and consult manual\n\
  -l, --level=LEVEL            backup level; default: full level\n\
                               LEVEL is allowed:\n\
                                   0 - full\n\
                                   1 - incremental 1\n\
                                   2 - incremental 2\n\
  -o, --output-file=FILE       print detailed backup messages to FILE\n\
  -S, --SA-mode                stand-alone mode execution\n\
  -C, --CS-mode                client-server mode execution\n\
      --no-check               don't check a consistency of the database; default: check\n\
  -t, --thread-count=COUNT     number of threads; default: 0(auto)\n\
  -z, --compress               compress backup volumes; default: uncompress\n\
  -e, --except-active-log      don't include an active log volume; default: include\n\
      --sleep-msecs=N          sleep N millisecond per 1M read; default: 0\n
71 Incompatible arguments, \"%1$s\", and \"%2$s\"\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 ERROR: Destination name is identical to source name.\n
31 ERROR: volext-pathname does not exist or is not writeable.\n
32 ERROR: vols-tofrom-paths_file does not exist or is not readable.\n
60 \
copydb: Copy a database.\n\
usage: %1$s copydb [OPTION] src-database-name dest-database-name\n\
\n\
valid options:\n\
      --server-name=HOST          server's HOST name; default: current host name\n\
  -F, --file-path=PATH            directory PATH for database volumes; default: current directory\n\
  -L, --log-path=PATH             directory PATH for log volumes; default: same as file-path\n\
  -B, --lob-base-path=PATH        base directory PATH for external LOB data; default: <file-path>/lob\n\
  -E, --extended-volume-path=PATH directory PATH for extended information volumes; default: same path as the database volumes or the given path in control-file\n\
  -i, --control-file=FILE         copying volumes over several locations written in the control file; default: none\n\
  -r, --replace                   replace an existing database; default: don't replace\n\
  -d, --delete-source             delete a source database; default: don't delete\n\
      --copy-lob-path             copy external storage PATH for LOB data from source database. cannot be used with -B option; default: don't copy\n


$set 4 MSGCAT_UTIL_SET_CREATEDB
40 Group '%1$s' does not exist\n
41 User '%1$s' does not exist\n
42 Unknown command '%1$s'\n
43 Couldn't open output file '%1$s'\n
44 Couldn't removing existing database.\n
45 Creating database with %1$s size using locale %2$s. The total amount of disk space needed is %3$s.\n
46 Couldn't create database.\n
47 Problem with user file '%1$s'\n
48 The given '%1$s', %2$s is out of range. Choose a size between %3$s and %4$s.\n
49 The value of %1$s '%2$s' is invalid.\n
60 \
createdb: Create a database.\n\
usage: %1$s createdb [OPTION] database-name database-locale\n\
\n\
database-locale format is <language>.<charset>; examples : en_US.iso88591, ko_KR.utf8\n\
valid options:\n\
      --db-volume-size=SIZE           size of first information volume\n\
                                      default: db_volume_size in cubrid.conf\n\
      --db-page-size=SIZE             database page SIZE in bytes; default: 16K\n\
      --log-volume-size=SIZE          size of log archives\n\
                                      default: log_volume_size in cubrid.conf\n\
      --log-page-size=SIZE            log page SIZE in bytes; default: database page size\n\
      --comment=COMMENT               COMMENT for a database; default: none\n\
  -F, --file-path=PATH                directory PATH for database volumes; default: current directory\n\
  -L, --log-path=PATH                 directory PATH for log volumes; default: same as file-path\n\
  -B, --lob-base-path=PATH            base directory PATH for external LOB data; default: <file-path>/lob\n\
      --server-name=HOST              server's HOST name; default: current host name\n\
  -r, --replace                       replace an existing database; default: don't replace\n\
      --more-volume-file=FILE         FILE contain specifications for creation of additional volumes; default: none\n\
      --user-definition-file=FILE     FILE contain user definitions; default: none\n\
      --csql-initialization-file=FILE FILE contain SQLs for an initialization of the database; default: none\n\
  -o, --output-file=FILE              redirect output messages to FILE; default: none\n\
  -v, --verbose                       enable verbose status messages; default: disabled\n


$set 6 MSGCAT_UTIL_SET_DELETEDB
60 \
deletedb: Delete a database.\n\
usage: %1$s deletedb [OPTION] database-name\n\
\n\
valid options:\n\
  -o, --output-file=FILE       redirect output messages to FILE; default: none\n\
  -d, --delete-backup          delete backup volumes; default: don't delete\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 ERROR: volext-pathname does not exist or is not writeable.\n
32 ERROR: vols-tofrom-paths_file does not exist or is not readable.\n
60 \
renamedb: Rename a database.\n\
usage: %1$s renamedb [OPTION] src-database-name dest-database-name\n\
\n\
valid options:\n\
  -E, --extended-volume-path=PATH directory PATH for extended information volumes; default: same path as the database volumes or the given path in control-file\n\
  -i, --control-file=FILE         renaming volumes over several locations written in the control file; default: none\n\
  -d, --delete-backup             delete backup volumes; default: don't delete\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 A %1$s process is already running.\n
12 MASTER SERVER starting. \n
13 MASTER SERVER exiting.\n
14 Error finding local host name\n
15 Could not find parameter file\n
16 Could not initialize master process\n
17 Server %1$s going down in %2$d minutes
18 Server %1$s notified of shutdown.\nThis may take several minutes. Please wait.
19 Server %1$s not found.
20 Master going down in %1$d minutes\n
21 Heartbeat failover was finished. Please wait until all shutdown processes of cub_servers are completed.\n


$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 Couldn't open output file '%1$s'\n
60 \
optimizedb: Optimize a database.\n\
usage: %1$s optimizedb [OPTION] database-name\n\
\n\
valid options:\n\
  -n, --class-name=NAME        update statistics for the NAME class; default: all classes\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 Invalid date format, valid format is dd-mm-yyyy:hh:mm:ss or 'backuptime'\n
20 Restoredb cancelled or an error occurred.\n
60 \
restoredb: Restore a database.\n\
usage: %1$s restoredb [OPTION] database-name\n\
\n\
valid options:\n\
  -d, --up-to-date=DATE             restore the database up to its condition at given DATE. DATE is given as follows: dd-mm-yyyy:hh:mm:ss or 'backuptime' keyword\n\
      --list                        display a list of backup volumes without restoring\n\
  -B, --backup-file-path=PATH       PATH is a directory of backup volumes to be restored\n\
  -l, --level=LEVEL                 LEVEL of backup to be restored; see backup usage; default: 0(full backup)\n\
  -p, --partial-recovery            perform partial recovery if any log archive is absent\n\
  -o, --output-file=FILE            redirect output messages to FILE; default: none\n\
  -u, --use-database-location-path  restore the database and log volumes to the path specified in the database location file\n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 The database name is missing.\n
20 '%1$s' file does not have %2$d lines.\n
21 interrupt ... \n
22 Interrupted load - current transaction committed\n
23 Interrupted load - current transaction aborted\n
24 Can't open file '%1$s'\n
25 \n*** Checking syntax ***\n\n
26 %1$d errors detected.\n
27 \n*** Inserting objects ***\n\n
28 Total %1$8d objects inserted.\n
29 Total %1$8d referenced objects inserted with default contents.\n
30 \n*** Committing the transaction ***\n
31 \n*** Closing the database ***\n
32 Line %1$d:
33 Error parsing '%1$s' as a '%2$s' value for attribute '%3$s' of '%4$s'\n
34 Missing domain for attribute %1$s of %2$s\n
35 For attribute %1$s of %2$s, type %3$s is not allowed in the domain of the set\n
36 For attribute %1$s of %2$s, expected type %3$s, got a set constant\n
37 For attribute %1$s of %2$s, expected type %3$s, got %4$s\n
38 No class named %1$s referenced by attribute %2$s of %3$s\n
39 No class named %1$s, ignoring tuples\n
40 No class with identifier %1$d referenced by attribute %2$s of %3$s\n
41 Class %1$s is not allowed in the loader input file\n
42 Stopped with error.\n
43 WARNING: updated existing instance !\n
44 WARNING: redefining Instance %1$d of class %2$s\n
45 Instance %1$d of class %2$s has already been defined\n
46 Instance %1$d of class %2$s has already been reserved\n
47 UNIQUE violation on attribute '%1$s', converted to NULL\n
48   %1$d instances\n
49 Class %1$s\n
50 Password:
51 \n*** Updating class statistics ***\n
52 %1$s near line %2$d (text was '%3$s')\n
53 Lexical error
54 line %1$d: syntax error at \"%2$s\"
55 missing
56 in %1$s
57 Incompatible arguments, \"%1$s\", and \"%2$s\"\n
58 %1$d instances committed\n
59 \nCannot invoke non-optimized version of loaddb.\n
60 Error parsing \"%1$s\", as %2$s type.\n
112   %1$d instances\r
113 Last committed line : %1$d
114 error-control-file
115 input file to control error(s) during loading
116 Total %1$8d object(s) inserted, %2$d object(s) failed.\n
117 Load failed.\n
118 Maximum length of class name is %1$d bytes.\n
120 \
loaddb: Import objects and schemas to the database.\n\
usage: %1$s loaddb [OPTION] database-name\n\
\n\
valid options:\n\
  -u, --user=ID                 user ID for database access; default: "public"\n\
  -p, --password=PASS           user password; default: none\n\
      --data-file-check-only    check syntax for data file without load; default: loading after checking\n\
  -l, --load-only               load data file without checking syntax; default: loading after checking\n\
      --estimated-size=NUMBER   estimated NUMBER of instances; default: 5000\n\
  -v, --verbose                 enable verbose status messages; default: disabled\n\
  -c, --periodic-commit=COUNT   insertion COUNT for periodic commit; default: no commit during loading\n\
      --no-oid                  don't use OID\n\
      --no-statistics           do not update the statistics while loading\n\
  -s, --schema-file=FILE[:LINE] create schema from LINE with FILE\n\
  -i, --index-file=FILE[:LINE]  create index from LINE with FILE\n\
  -d, --data-file=FILE          load data with FILE\n\
  -t, --table=TABLE             table name that is substituted for missing class header in data file\n\
      --error-control-file=FILE FILE to control error(s) during loading\n\
      --ignore-class-file=FILE  input file of class names that skip load\n
121 \
load_noopt: This utility is deprecated. Be careful to use.\n\
usage: load_noopt [OPTION] database-name\n\
\n\
valid options:\n\
  -u, --user=ID                 user ID for database access; default: "public"\n\
  -p, --password=PASS           user password; default: none\n\
      --data-file-check-only    check syntax for data file without load; default: loading after checking\n\
  -l, --load-only               load data file without checking syntax; default: loading after checking\n\
      --estimated-size=NUMBER   estimated NUMBER of instances; default: 5000\n\
  -v, --verbose                 enable verbose status messages; default: disabled\n\
  -c, --periodic-commit=COUNT   insertion COUNT for periodic commit; default: no commit during loading\n\
      --no-oid                  don't use OID\n\
  -s, --schema-file=FILE[:LINE] create schema from LINE with FILE\n\
  -i, --index-file=FILE[:LINE]  create index from LINE with FILE\n\
  -d, --data-file=FILE          load data with FILE\n\
      --error-control-file=FILE FILE to control error(s) during loading\n


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 cached-pages value is invalid.\n
42 cached-page-size value is invalid.\n
43 %1$d objects dumped.\n
44 Class %1$s
45 , %1$d instances.\n
46 Only %1$d out of %2$d objects dumped successfully.\n
47 output directory is too long.\n
48 LSA : %1$d %2$d \n
49 Password:
60 \
unloaddb: Export objects and schemas from the database.\n\
usage: %1$s unloaddb [OPTION] database-name\n\
\n\
valid options:\n\
  -u, --user=ID               user ID for database access; default: "dba"\n\
  -p, --password=PASS         user password; default: none\n\
  -i, --input-class-file=FILE input FILE of table names; default: dump all classes\n\
      --include-reference     include referenced tables; with -i option; default: disabled\n\
      --input-class-only      include specified class only; with -i option; default: disabled\n\
      --lo-count=COUNT        lo file COUNT per a directory; default: 0\n\
      --estimated-size=NUMBER estimated NUMBER of instances; default: auto computed\n\
      --cached-pages=NUMBER   NUMBER of cached pages; default: 100\n\
  -O, --output-path=PATH      PATH of output directory; default: working directory\n\
  -s, --schema-only           dump schema only; default: schema and objects\n\
  -d, --data-only             dump objects only; default: schema and objects\n\
      --output-prefix=PREFIEX PREFIX for output files; default: the database name\n\
      --hash-file=FILE        FILE for hash; default: generate by tmpnam()\n\
  -v, --verbose               enable verbose status messages; default: disabled\n\
      --use-delimiter         use '"' where an identifier begins and ends; default: don't use\n\
  -S, --SA-mode               stand-alone mode execution\n\
  -C, --CS-mode               client-server mode execution\n\
      --datafile-per-class    create a object file for each class; default: disabled\n



$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 \nPass 1\n\n
12 %1$d objects processed.\n
13 \nPass 2\n\n
14 Class %1$s\n
15 oid:  %1$d|%2$d|%3$d,\n
16 %1$d instances.\n
17 Updating object.\n
18 ref_oid:  %1$d.%2$d.%3$d, ref_class_oid:  %4$d.%5$d.%6$d\n
19 Couldn't transform object\n
20 Missing class heap file\n
21 Couldn't update object\n
22 Only %1$d out of %2$d objects processed successfully.\n
23 COMPACTDB already started.\n
24 The number of pages must be between %1$d and %2$d.\n
25 The instance lock timeout must be between %1$d and %2$d.\n
26 Total objects: %1$d\n
27 Failed objects: %1$d\n
28 Modified objects: %1$d\n
29 Big objects: %1$d\n
30 Old class representations deleted from catalog.\n
31 Old class representations preserved in catalog.\n
32 Can't set isolation level.\n
33 Couldn't compact database.\n
34 The class lock timeout must be between %1$d and %2$d.\n
35 Locked class... Incomplete processing...\n
36 Unprocessed class...\n
37 Process error... Incomplete processing...\n
38 Nothing to process...\n
39 Invalid parameters...\n
40 Unknown class name...\n
41 The unused class OIDs and pages have been reclaimed.\n
42 Reclaiming of OIDs and pages skipped.\n
43 Error while processing class, class skipped.\n
44 \nPass 3\n\n
45 Heap compact failed...\n
46 Heap compact succeeded...\n
60 \
compactdb: Free unused space in a database.\n\
usage: %1$s compactdb [OPTION] database-name [class_name1 class_name2 ...]\n\
\n\
valid options:\n\
  -v, --verbose                     enable verbose status messages; default: disabled\n\
  -i, --input-class-file=FILE       input FILE of table names\n\
                                    class_name list must be empty\n\
                                    default: compact all classes\n\
                                    enabled in CS-mode only\n\
  -p, --pages-commited-once=NUMBER  maximum number of pages to commit once\n\
                                    default: 10\n\
                                    enabled in CS-mode only\n\
  -S, --SA-mode                     stand-alone mode execution\n\
  -C, --CS-mode                     client-server mode execution\n\
  -d, --delete-old-repr             delete old class representations from catalog; default: disabled\n\
  class_name_list                   enabled in CS-mode only\n\
  -I, --Instance-lock-timeout       timeout for instance lock\n\
  -c, --class-lock-timeout          timeout for class lock\n

$set 15 MSGCAT_UTIL_SET_COMMDB
21 The CUBRID master running on host %1$s \nwas started at %2$s
22 has serviced %1$d requests. %2$d server(s) running.\n
23 SHUTTING DOWN in %1$s minutes\n
24 %1$s
25 Name of server to shutdown :
26 How many minutes until shutdown? :
27 \
cub_commdb: This utility is deprecated. Use 'cubrid' command instead.\n\
usage: cub_commdb [OPTION]\n\
\n\
valid options:\n\
  -P, --server-list                print server processes\n\
  -O, --all-list                   print all processes\n\
  -S, --shutdown-server=NAME       shutdown NAME database server\n\
  -A, --shutdown-all               shutdown all processes\n\
  -c, --server-mode=NAME           print NAME database server's HA mode\n\
  -N, --node-list                  list nodes managed by CUBRID heartbeat\n\
  -L, --process-list               list local process managed by CUBRID heartbeat\n\
  -p, --ping-host                  list ping hosts used for CUBRID heartbeat\n\
  -D, --dereg-process=PID          exit the target process and remove it from managed process entries\n\
  -F, --reconfig-node-list         apply CUBRID heartbeat configuration recently changed\n\
  -V, --verbose                    enable verbose status messages; default: disabled\n
28 Sorry, unknown command: %1$s\n
29 Could not get host name for local host - aborting\n
30 Could not find parameter file\n
31 Could not connect to master server on %1$s\n
32 How many minutes until shutdown? :
33 Master will shutdown in %1$d minutes\n
34 Error receiving data from client
35 the database server %1$s is not currently running
36 can't exec shell (%1$s) for %2$s, %3$s
37 process fork %1$s, returned %2$d, %3$s
38 can't execute %1$s
39 -i(--immediately) option must be used with --deact-stop-all option.\n


$set 16 MSGCAT_UTIL_SET_PATCHDB
60 \
emergency_patchlog: Patch a database in case of emergency.\n\
usage: %1$s emergency_patchlog [OPTION] database-name\n\
\n\
valid options:\n\
  -r, --recreate-log           recreate log files; use carefully\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 Wrong value %1$d for npages\n
21 Wrong value '%1$s' for volume storage purpose.\n
22 WARNING: --max-writesize-in-sec option is ignored in stand-alone mode execution.\n
60 \
addvoldb: Add a volume-file for the database.\n\
usage: %1$s addvoldb [OPTION] database-name\n\
\n\
valid options:\n\
      --db-volume-size=SIZE        size of additional volume; default: db_volume_size in cubrid.conf\n\
      --max-writesize-in-sec=SIZE  the amount of volume written per second; (ex. 512K, 1M, 10M); default: not used; minimum: 160K\n\
  -n, --volume-name=NAME           NAME of information volume; default: generate such as "db"_ext1)\n\
  -F, --file-path=PATH             PATH for adding volume file; default: working directory\n\
      --comment=COMMENT            COMMENT for adding volume file; default: none\n\
  -p, --purpose=PURPOSE            PURPOSE for adding volume file; allowed:\n\
                                           DATA - only for data\n\
                                           INDEX - only for indices\n\
                                           TEMP - only for temporary\n\
                                           GENERIC - for all purposes\n\
  -S, --SA-mode                    stand-alone mode execution\n\
  -C, --CS-mode                    client-server mode execution\n

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 Some inconsistencies were detected in your database.\n Please consult error_log_file %1$s for additional information.\n
21 %1$s: Cannot find the class. It will be omitted.\n
22 %1$s: Cannot find the index on class %2$s.\n
60 \
checkdb: Check consistency of the database.\n\
usage: %1$s checkdb [OPTION] database-name [class_name1 class_name2 ...]\n\
\n\
valid options:\n\
  -S, --SA-mode                stand-alone mode execution\n\
  -C, --CS-mode                client-server mode execution\n\
  -r, --repair                 repair when inconsistency is found\n\
  -i, --input-class-file=FILE  input FILE of class names\n\
                               default: check all classes\n\
  -I, --index-name             index name that will be checked only\n\
                               default: check all indices\n\
      --check-prev-link        check only previous links of indices\n\
      --repair-prev-link       repair previous links of indices\n\
      --check-file-tracker     check all allocated pages of file\n\
      --check-heap             check all pages of heap files\n\
      --check-catalog          check catalog consistency\n\
      --check-btree            check all btree indices\n\
      --check-class-name       check class name consistency\n\
      --check-btree-entries    check entries of btree consistency\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
15 Space description for database '%1$s' with pagesize %2$s. (log pagesize: %3$s)\n\n
16 %1$5d %2$9s  %3$s %4$s  %5$s\n
17 Couldn't open output file '%1$s'\n
18 Space description for temporary volumes for database '%1$s' with pagesize %2$s.\n\n
19 Volid  Purpose   total_pages  free_pages  Vol Name\n\n
20 Volid  Purpose    total_size   free_size  Vol Name\n\n
21 LOB space description %1$s \n\n
22 -------------------------------------------------------------------------------\n
30 Summarized space description for database '%1$s' with pagesize %2$s. (log pagesize: %3$s)\n\n
31 Purpose    total_pages  used_pages  free_pages  volume_count\n
32 Purpose     total_size   used_size   free_size  volume_count\n
33 %1$10s %2$s %3$s %4$s %5$10d\n
34 -------------------------------------------------------------\n
40 Volid  Purpose   total_pages  free_pages  data_pages index_pages  temp_pages  Vol Name\n\n
41 Volid  Purpose    total_size   free_size   data_size  index_size   temp_size  Vol Name\n\n
42 %1$5d %2$9s  %3$s %4$s %5$s %6$s %7$s  %8$s\n
43 ----------------------------------------------------------------------------------------------------\n
50 Purpose    total_pages  used_pages  free_pages  data_pages index_pages  temp_pages  volume_count\n
51 Purpose     total_size   used_size   free_size   data_size  index_size   temp_size  volume_count\n
52 %1$10s %2$s %3$s %4$s %5$s %6$s %7$s %8$10d\n
53 -------------------------------------------------------------------------------------------------\n
60 \
spacedb: Display usage/free space of the database.\n\
usage: %1$s spacedb [OPTION] database-name\n\
\n\
valid options:\n\
  -o, --output-file=FILE       redirect output messages to FILE; default: none\n\
  -S, --SA-mode                stand-alone mode execution\n\
  -C, --CS-mode                client-server mode execution\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               set the display unit (page, mega, giga, tera, human readable); default: H\n\
  -s, --summarize              display only a summary for each volume purpose\n\
  -p, --purpose                display the used size of each purpose(data/index/temp) of each volume\n



$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 Estimated Number of Pages = %1$d\n
60 \
estimate_data: Estimate size of data.\n\
usage: %1$s estimate_data number-of-instances average-instance-size number-of-attributes variable-number-of-attributes\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 Bad index key type specified = %1$s\n
16 Bad average key length specified = %1$d when precision = %2$d\n
17 Bad arguments specified\n
20 Estimated number of total index pages = %1$d\n (When index created, after instances have been populated.)\n
21 Estimated number of total index pages = %1$d\n (When index created, before instances have been populated (average case))\n
22 Estimated number of total index pages = %1$d\n (When index created, before instances have been populated (worst case))\n
23 For the given input...\n
24 Total number of instances: %1$d\n
25 Number of different keys : %1$d\n
26 Average key size         : %1$d\n
27 Key Type                 : %1$s\n\n
60 \
estimate_index: Estimate size of an index.\n\
usage: %1$s estimate_index number-of-instances number-of-different-keys average-key-size key-type\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: Install a database.\n\
usage: %1$s installdb [OPTION] database-name\n\
\n\
valid options:\n\
      --server-name=HOST       server HOST name; default: current host name\n\
  -F, --file-path=PATH         directory PATH for database volumes; default: current directory\n\
  -L, --log-path=PATH          directory PATH for log volumes; default: same as file-path\n

$set 23 MSGCAT_UTIL_SET_MIGDB
1 Temporary class OID\n
2 Can't print elo values\n
3 Error accessing long object for migration\n
4 Error opening output file for long object migration \n
5 Read error\n
6 Write error\n
7 Error opening ELO input file "%1$s"\n
8 CLASSES:
9 hash filename
10 Name:                         %1$s\n
11 Size:                         %1$d\n
12 Page size:                    %1$d\n
13 Data size:                    %1$d\n
14 Entry size:                   %1$d\n
15 Entries per page:             %1$d\n
16 Cached pages:                 %1$d\n
17 Number of entries:            %1$d\n
18 Number of collisions:         %1$d\n
19 Hash filename:                %1$s\n
20 Next overflow entry:          %1$d\n
21 Key type:                     %1$s\n
22 Address of page headers:      %1$x\n
23 Address of last page header:  %1$x\n
24 Address of free page header:  %1$x\n
25 Address of page bitmap:       %1$x\n
26 Page bitmap size:             %1$d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
60 \
diagdb: Dump a database.\n\
usage: %1$s diagdb [OPTION] database-name\n\
\n\
valid options:\n\
  -d, --dump-type=TYPE         TYPE of dump\n\
                               -1 dump all\n\
                                1 dump file tables\n\
                                2 dump file capacities\n\
                                3 dump heap capacities\n\
                                4 dump index capacities\n\
                                5 dump classnames\n\
                                6 dump disk bitmaps\n\
                                7 dump catalog\n\
                                8 dump log\n\
                                9 dump heap\n


$set 25 MSGCAT_UTIL_SET_LOCKDB
15 Couldn't open output file '%1$s'\n
59 lockdb cannot run as standalone mode.\n
60 \
lockdb: Display lock-status of the database.\n\
usage: %1$s lockdb [OPTION] database-name\n\
\n\
valid options:\n\
  -o, --output-file=FILE       redirect output messages to FILE; default: none\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 Only one argument must be given among: tran-index, user, host, progname\n
21 Enter DBA password:
26 There is not a transaction with the specified argument values.\n
27 Ready to kill the following transactions:\n\n
28 Do you wish to proceed ? (Y/N)
29 Killing transaction associated with transaction index %1$d\n
30 Killing of the following transactions has failed\n\n
31 The transaction does not finish yet, but it may be doing the rollback work.\n Please, wait a second and try it later if needed.\n
32 Invalid tranindex(%1$s).\n
59 killtran cannot run as standalone mode.\n
60 \
killtran: Manage transactions of the database.\n\
usage: %1$s killtran [OPTION] database-name\n\
\n\
valid options:\n\
  -i, --kill-transaction-index=INDEX kill transaction with transaction INDEX\n\
      --kill-user-name=ID            kill all transactions with user ID\n\
      --kill-host-name=HOST          kill all transactions with client HOST\n\
      --kill-program-name=NAME       kill all transactions with client program NAME\n\
      --kill-sql-id=SQL_ID           kill all transactions with SQL_ID\n\
  -p, --dba-password=PASS            password of the DBA user; will prompt if don't specify\n\
  -d, --display-information          display information about active transactions\n\
  -q, --query-exec-info              display information about query execution information of active transactions\n\
  -f, --force                        kill the transaction without a prompt for verification\n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1 database-name
2 required argument, no default
3 compatibility-check
4 compatibility check only
5 backup-destination
6 a directory path or device for the destination of the backup volumes
20 database '%1$s' does not exist.
21 '%1$s' can't be migrated to %2$s, because its DB version is too low.\n
22 '%1$s' don't need to be migrated.\n
23 '%1$s' needs to be migrated.\n
24 Backup is recommended after migration. Do you want to back up your database now? (y/n):
25 We recommend to back up your new database with backupdb command after migration.
26 \n*** WARNING: Backup your database before you start migrating to %1$s ***\n\nTo recover errors which might be occurred during migration, we strongly recommend to back up your database.\nHave you already backed up your database? (y/n):
27 \n1. Run backupdb \n2. Enter the new backup path for the new database.(current path: %1$s)\n3. Exit \n
28 \nThe new backup path for the database:
29 \nMigration and backup have been successfully completed.
30 Backup failed. Please, retry to back up your database.
31 \n\n*** WARNING : Once the migration is completed, you cannot restore the database with the former backup volumes.\nNotice that the previous version of backup files can be restored only on the corresponding version of restoredb.\nPlease, check the former backup volumes is securely kept.\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: Alter a host of the database.\n\
usage: %1$s alterdbhost [OPTION] database-name\n\
\n\
valid options:\n\
  -h, --host=HOST              new HOST name; default: current host name\n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 database-name
2 required argument, no default
3 java-class-file
4 required argument, no default
5 force-overwrite
6 overwrite class file if exist, default no

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 Cannot open output file '%1$s'\n
59 plandump cannot run as standalone mode.\n
60 \
plandump: Display or control cached query plans.\n\
usage: %1$s plandump [OPTION] database-name\n\
\n\
valid options:\n\
  -d, --drop                   drop all plans in the server's cache\n\
  -o, --output-file=FILE       redirect output messages to FILE; default: none\n

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 Cannot open output file '%1$s'\n
21 System parameters to be used by the client program.\n
22 System parameters loaded by the server '%1$s'\n
23 System parameters to be used in standalone mode.\n
60 \
paramdump: Dump system parameter settings.\n\
usage: %1$s paramdump [OPTION] database-name\n\
\n\
valid options:\n\
  -o, --output-file=FILE       redirect output message to FILE; default: none\n\
  -b, --both                   dump both client and server parameters\n\
  -S, --SA-mode                stand-alone mode execution\n\
  -C, --CS-mode                client-server mode execution\n

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 Wrong value \"%1$s\" for the server's HA running mode.\n
12 Cannot change the server's HA running mode.\n
21 Enter DBA password:
22 The server '%1$s''s current HA running mode is %2$s.\n
23 The server '%1$s''s HA running mode is changed to \"%2$s\".\n
24 The server was not configured for HA.\n
58 HA feature is not supported in this platform.\n
59 changemode cannot run as standalone mode.\n
60 \
changemode: Display or change the server's HA running mode.\n\
usage: %1$s changemode [OPTION] database-name\n\
\n\
valid options:\n\
  -m, --mode=MODE             change the server mode to MODE; default: none\n\
  -f, --force                 force to change\n\
  -t, --timeout=S             waiting S second while active transaction is terminated normally\n\
                              -t option is effective only when the user changes server mode to maintenance\n\
                              default: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 Wrong value \"%1$s\" for the copylogdb's running mode.\n
21 Enter DBA password:
22 The server was not configured for HA.\n
58 HA feature is not supported in this platform.\n
59 copylogdb cannot run as standalone mode.\n
60 \
copylogdb: Copy log pages from the server.\n\
usage: %1$s copylogdb [OPTION] database-name\n\
\n\
valid options:\n\
  -L, --log-path=PATH         directory PATH for copied log volumes\n\
  -m, --mode=MODE             operation mode; default: sync\n\
                              MODE: sync or semisync or async\n\
  -S, --start-page-id=ID      first log page to copy\n 

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 Enter DBA password:
22 The server was not configured for HA.\n
58 HA feature is not supported in this platform.\n
59 applylogdb cannot run as standalone mode.\n
60 \
applylogdb: Apply copied log to the server.\n\
usage: %1$s applylogdb [OPTION] database-name\n\
\n\
valid options:\n\
  -L, --log-path=PATH         directory PATH of copied log volumes\n\
      --max-mem-size=SIZE     maximum process SIZE in MB; max 500\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 Cannot open output file '%1$s'\n
59 statdump cannot run as standalone mode.\n
60 \
statdump: Dump global statistics.\n\
usage: %1$s statdump [OPTION] database-name\n\
\n\
valid options:\n\
  -o, --output-file=FILE       redirect output message to FILE; default: none\n\
  -c, --cumulative             print cumulative stats\n\
  -i, --interval=S             print stats every S secs\n\
  -s, --substr=STRING          print stat item only that match the substring\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 Enter DBA password:
22 The server was not configured for HA.\n
58 HA feature is not supported in this platform.\n
59 applyinfo cannot run as standalone mode.\n
60 \
applyinfo: display CUBRID HA Apply information.\n\
usage: %1$s applyinfo [OPTION] database-name\n\
\n\
valid options:\n\
  -r, --remote-host-name      remote host name; display remote node's active log information \n\
  -a, --applied-info 	      display applied information \n\
  -L, --copied-log-path=PATH  path of copied log volumes; display copied log information \n\
  -p, --page=ID               page id; default : 0\n\
  -v, --verbose               enable verbose status messages; default : disable\n\
  -i, --interval=S            print information every S secs\n

$set 45 MSGCAT_UTIL_SET_ACLDB
59 acldb cannot run as standalone mode.\n
60 \
acldb: Display or reload access control setting of the database.\n\
usage: %1$s acldb [OPTION] database-name\n\
\n\
valid options:\n\
  -r, --reload       reload ip control file\n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
genlocale: Generates locale shared library using LDML input file(s).\n\
usage: %1$s genlocale [OPTION] [language-string]\n\
\n\
language-string	      locale language string for which to generate shared library.\n\
		      If no language-string is given all languages in 'cubrid_locales.txt'\n\
		      will be processed.\n\
valid options:\n\
  -v, --verbose	              enable verbose status messages; default : disable\n\
  -i, --input-ldml-file       path for input LDML file. Only when language-string is given.\n
61 Invalid locale name argument.\n

$set 47 MSGCAT_UTIL_SET_DUMPLOCALE
57 Cannot simultaneously use -i/--input-file and locale identifier.\n
58 Invalid codepoint range (-s -e).\n
59 Invalid locale name argument.\n
60 \
dumplocale: Dumps the contents of locale shared library previously generated using LDML input file(s).\n\
usage: %1$s dumplocale [OPTION] [language-string]\n\
\n\
language-string	 locale language string for which to dump locale shared library.\n\
		         If no language-string is given all languages in 'cubrid_locales.txt'\n\
		         will be processed.\n\
valid options:\n\
  -i, --input-file    path for input locale shared library (previously generated).\n\
                      Can be used only when language-string is not given.\n\
  -d, --calendar      dump calendar and date/time information; default : disable\n\
  -n, --numeric		  dump numeric information; default : disable\n\
  -a, --alphabet=l|lower|u|upper|both	     dump alphabet and casing information; default : disable\n\
      --identifier-alphabet=l|lower|u|upper	 dump alphabet and casing information for identifiers; default : disable\n\
  -c, --codepoint-order	 dump collation info sorted based on codepoint values\n\
                         (cp, char, weight, next-cp, char and weight);\n\
                         default : disable\n\
  -w, --weight-order  dump collation info sorted based on weight values\n\
                      (weight, cp, char); default : disable\n\
  -s, --start-value	  filtering option; starting codepoint for\n\
                      -a, --identifier-alphabet, -c, -w; default : 0\n\
  -e, --end-value     filtering option; ending codepoint for \n\
                      -a, --identifier-alphabet, -c, -w; \n\
                      default : maximum value read from locale shared library\n\
  -k, --console-conversion  dump console conversion data; default : disable\n\
  -z, --normalization   dump normalization data; default : disable\n

$set 48 MSGCAT_UTIL_SET_SYNCCOLLDB
43 Partitioning must be removed before changing the current collation '%1$s' of their partitioning attribute.\nClass name\n
44 Foreign keys on attributes having collation '%1$s' must be dropped before changing collation of attributes.\nTable | Foreign Key\n
45 Classes having collation '%1$s'; they should be redefined :\nClass name\n
46 Function indexes having expression containing collation '%1$s'; they should be redefined :\n\
Table | Index | Function expression\n
47 Synchronization of collations was not performed. Database was not changed.\n
48 Contents of '%1$s' system table was updated with new system collations.\n
49 Updating system collations may cause corruption of database. Continue (y/n) ?\n
50 Collation '%1$s' (Id: %2$d) not found in database or changed in new system configuration.\n
51 Triggers having condition containing collation '%1$s'; they should be redefined :\nTrigger | Condition\n
52 Query views containing collation '%1$s'; they should be redefined :\nView | Query\n
53 Attributes having collation '%1$s'; they should be redefined.\nTable | Attribute Domain\n
54 Manual intervention on database is required.\n\
Please check file '%1$s' and execute it before forcing collations into database.\n
55 Synchronization is not needed.\n
56 Synchronization of system collation into database is required.\n\
Run 'cubrid synccolldb -f %1$s'\n
57 There are %1$d new collations in system configuration compared to database:\n
58 All collations in database '%1$s' are found in the new system configuration.\n
59 There are %1$d collations in database which are not configured or are changed compared to system collations.\n
60 \
synccolldb: Synchronizes the collations from database with collations of system.\n\
usage: %1$s synccolldb [OPTION] database-name\n\
\n\
valid options:\n\
  -c, --check-only    Checks the collations from databases with collation of system.\n\
		      If collations synchronization is required, error code 1 is returned.\n\
		      Displays a report of incompatible collations.\n\
  -f, --force-only    Updates the collations in databases to be same as the collations of new sytem configuration.\n\

$set 49 MSGCAT_UTIL_SET_TRANLIST
20 Enter %1$s password: 
22 Tran index          User name      Host name      Process id      Program name\n
23 -------------------------------------------------------------------------------\n
24 %1$4d%2$-10s %3$14s %4$14s %5$15d %6$17s\n
25 There are no transactions\n
26 tranlist can only be performed by the DBA or a DBA group member.\n
27 sort-key column number(%1$d) is invalid.\n
32 Tran index         Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
33 %1$4d%2$-10s %3$14d    %4$-25s %5$10.2f %6$12.2f    %7$28s     %8$13s   %9$s\n
34 --------------------------------------------------------------------------------------------------------------------------------------------------\n
42 Tran index          User name      Host name      Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
43 %1$4d%2$-10s %3$14s %4$14s %5$14d    %6$-25s %7$10.2f %8$12.2f    %9$28s     %10$13s   %11$s\n
44 ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 Tran index : %1$d
46 SQL_ID: %1$s\n
59 tranlist cannot run as standalone mode.\n
60 \
tranlist: Dump active transaction information.\n\
usage: %1$s tranlist [OPTION] database-name\n\
\n\
valid options:\n\
  -u, --user=ID                user ID for database access; default: DBA user\n\
  -p, --password=PASS          user password; default: none\n\
  -s, --summary                print summarized information of transaction\n\
      --sort-key=COLUMN_NUMBER column number to be sorted\n\
      --reverse                reverse the sort result\n

$set 50 MSGCAT_UTIL_SET_PREFETCHLOGDB
22 The server was not configured for HA.\n
23 Prefetch feature is disabled.\n
58 HA feature is not supported in this platform.\n
59 prefetchlogdb cannot run as standalone mode.\n
60 \
prefetchlogdb: Prefetch log pages from the server.\n\
usage: %1$s prefetchlogdb [OPTION] database-name\n\
\n\
valid options:\n\
  -L, --log-path=PATH         directory PATH for copied log volumes\n
