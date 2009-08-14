
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
$       35      repl_server
$       36      repl_agent
$       37      plandump
$       38      paramdump
$       39      changemode
$       40      copylogdb
$       41      appylogdb

$set 1 MSGCAT_UTIL_SET_GENERIC
1 Invalid length or character `%c' in database name `%s'\n
2 Couldn't open output file `%s'\n
3 Missing or invalid utility name.\n
4 Invalid utility name `%s'\n
5 The following list contains the recognized utility names:\n
6 Invalid character `%c' in volume name `%s'\n
9 \
%s (cubrid utilities) %s\n\
Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n
10 \
cubrid administrator utility, version %s\n\
usage: %s <utility-name> [args]\n\
Type '%s <utility-name>' for help on a specific utility.\n\
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
    killtran\n\
    optimizedb\n\
    compactdb\n\
    diagdb\n\
    emergency_patchlog\n\
    checkdb\n\
    alterdbhost\n\
    plandump\n\
    loaddb\n\
    unloaddb\n\
    paramdump\n\
    changemode\n\
    copylogdb\n\
    applylogdb\n\
\n\
%s is a tool for DBMS.\n\
For additional information, see http://www.cubrid.com\n
12 '%s' utility-name is invalid.\nType 'cubrid --help' to see usage.\n
13 '%s' command is invalid.\nType 'cubrid --help' to see usage.\n
14 Could not load service properties in cubrid.conf.\n
15 @ %s %s: %s\n
16 @ %s %s\n
17 ++ %s '%s' is not running.\n
18 ++ %s is not running.\n
19 ++ %s '%s' is running.\n
20 ++ %s is running.\n
21 ++ %s %s: %s\n
22 One or more arguments are missing.\nType 'cubrid --help' to see usage.\n
23 cubrid utility, version %s\n\
usage: %s <utility-name> [args]\n\
Type '%s <utility-name>' for help on a specific utility.\n\
\n\
Available service's utilities:\n\
    service\n\
    server\n\
    broker\n\
    manager\n\
    repl_server\n\
    repl_agent\n\
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
    emergency_patchlog\n\
    installdb\n\
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
    changemode\n\
    copylogdb\n\
    applylogdb\n\
\n\
%s is a tool for DBMS.\n\
For additional information, see http://www.cubrid.com\n
24 cubrid utility, version %s\n\
usage: %s service <command>\n\
\n\
Available command:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 cubrid utility, version %s\n\
usage: %s server <command> [args]\n\
\n\
Available command:\n\
    start       [database-name]\n\
    stop        [database-name]\n\
    restart     [database-name]\n\
    status\n
26 cubrid utility, version %s\n\
usage: %s broker <command> [args]\n\
\n\
Available command:\n\
    start\n\
    stop\n\
    restart\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [<expr>]\n\
    on          broker-name\n\
    off         broker-name\n
27 cubrid utility, version %s\n\
usage: %s manager <command>\n\
\n\
Available command:\n\
    start\n\
    stop\n\
    status\n
28 cubrid utility, version %s\n\
usage: %s repl_server <command> [args]\n\
\n\
Available command:\n\
    start       master-database-name server-network-port [-a max-agent-num] [-e error-file]\n\
    stop        master-database-name\n\
    status\n
29 cubrid utility, version %s\n\
usage: %s repl_agent <command> [args]\n\
\n\
Available command:\n\
    start       dist-database-name [dba-password]\n\
    stop        dist-database-name\n\
    status\n
30 '%s' argument is not needed.\n
31 A database-name is missing.\n
32 Replication is not supported in this platform

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 WARNING: -t option is ignored in stand-alone mode execution.\n
60 \
backupdb: Backup a database.\n\
usage: %s backupdb [OPTION] database-name\n\
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
  -e, --except-active-log      don't include an active log volume; default: include\n
71 Incompatible arguments, \"%s\", and \"%s\"\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 ERROR: Destination name is identical to source name.\n
31 ERROR: volext-pathname does not exist or is not writeable.\n
32 ERROR: vols-tofrom-paths_file does not exist or is not readable.\n
60 \
copydb: Copy a database.\n\
usage: %s copydb [OPTION] src-database-name dest-database-name\n\
\n\
valid options:\n\
      --server-name=HOST          server's HOST name; default: current host name\n\
  -F, --file-path=PATH            directory PATH for database volumes; default: current directory\n\
  -L, --log-path=PATH             directory PATH for log volumes; default: same as file-path\n\
  -E, --extended-volume-path=PATH directory PATH for copying/renaming extended information volumes; default: same place as the original volumes or the specifications given in control-file\n\
  -i, --control-file=FILE         FILE to control copying/renaming information volumes over several locations\n\
  -r, --replace                   replace an existing database; default: don't replace\n\
  -d, --delete-source             delete a source database; default: don't delete\n


$set 4 MSGCAT_UTIL_SET_CREATEDB
40 Group `%s' does not exist\n
41 User `%s' does not exist\n
42 Unknown command `%s'\n
43 Couldn't open output file `%s'\n
44 Couldn't removing existing database.\n
45 Creating database with %d pages.\n
46 Couldn't create database.\n
47 Problem with user file `%s'\n
48 Number of page must be greater than or equal to 100\n
49 Database `%s' was successfully created at `%s'.\nIf you want to start this database, run command `start_server %s'.\nIf you want to execute query, run command `sqlx %s'.\n
60 \
createdb: Create a database.\n\
usage: %s createdb [OPTION] database-name\n\
\n\
valid options:\n\
  -p, --pages=NUMBER                  initial NUMBER of page for first information volume; default: 5000\n\
      --comment=COMMENT               COMMENT for a database; default: none\n\
  -F, --file-path=PATH                directory PATH for database volumes; default: current directory\n\
  -L, --log-path=PATH                 directory PATH for log volumes; default: same as file-path\n\
      --server-name=HOST              server's HOST name; default: current host name\n\
  -r, --replace                       replace an existing database; default: don't replace\n\
      --more-volume-file=FILE         FILE contain specifications for creation of additional volumes; default: none\n\
      --user-definition-file=FILE     FILE contain user definitions; default: none\n\
      --csql-initialization-file=FILE FILE contain SQLs for an initialization of the database; default: none\n\
  -o, --output-file=FILE              redirect output messages to FILE; default: none\n\
  -v, --verbose                       enable verbose status messages; default: disabled\n\
  -l, --log-page-count=NUMBER         NUMBER of pages for log archives; default: same as a first generic volume\n\
  -s, --page-size=SIZE                database page SIZE in bytes; default: an unit of I/O in operating system\n


$set 6 MSGCAT_UTIL_SET_DELETEDB
60 \
deletedb: Delete a database.\n\
usage: %s deletedb [OPTION] database-name\n\
\n\
valid options:\n\
  -o, --output-file=FILE       redirect output messages to FILE; default: none\n\
  -d, --delete-backup          delete backup volumes; default: don't delete\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
60 \
renamedb: Rename a database.\n\
usage: %s renamedb [OPTION] src-database-name dest-database-name\n\
\n\
valid options:\n\
  -E, --extended-volume-path=PATH directory PATH for copying/renaming extended information volumes; default: same place as the original volumes or the specifications given in control-file\n\
  -i, --control-file=FILE         FILE to control copying/renaming information volumes over several locations\n\
  -d, --delete-backup             delete backup volumes; default: don't delete\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 A %s process is already running.\n
12 MASTER SERVER starting. \n
13 MASTER SERVER exiting.\n
14 Error finding local host name\n
15 Could not find parameter file\n
16 Could not initialize master process\n
17 Server %s going down in %d minutes
18 Server %s notified of shutdown.\nThis may take several minutes. Please wait.
19 Server %s not found.
20 Master going down in %d minutes\n
21 repl_server %s notified of shutdown.\nThis may take several minutes. Please wait.
22 repl_agent %s notified of shutdown.\nThis may take several minutes. Please wait.


$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 Couldn't open output file `%s'\n
60 \
optimizedb: Optimize a database.\n\
usage: %s optimizedb [OPTION] database-name\n\
\n\
valid options:\n\
  -n, --class-name=NAME        update statistics for the NAME class; default: all classes\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 Invalid date format, valid format is dd-mm-yyyy:hh:mm:ss or 'backuptime'\n
20 Restoredb cancelled or an error occurred.\n
60 \
restoredb: Restore a database.\n\
usage: %s restoredb [OPTION] database-name\n\
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
19 Tha database name is missing.\n
20 '%s' file does not have %d lines.\n
21 interrupt ... \n
22 Interrupted load - current transaction committed\n
23 Interrupted load - current transaction aborted\n
24 Can't open file `%s'\n
25 \n*** Checking syntax ***\n\n
26 %d errors detected.\n
27 \n*** Inserting objects ***\n\n
28 Total %8d objects inserted.\n
29 Total %8d referenced objects inserted with default contents.\n
30 \n*** Committing the transaction ***\n
31 \n*** Closing the database ***\n
32 Line %d:
33 Error parsing `%s' as a `%s' value for attribute `%s' of `%s'\n
34 Missing domain for attribute %s of %s\n
35 For attribute %s of %s, type %s is not allowed in the domain of the set\n
36 For attribute %s of %s, expected type %s, got a set constant\n
37 For attribute %s of %s, expected type %s, got %s\n
38 No class named %s referenced by attribute %s of %s\n
39 No class named %s, ignoring tuples\n
40 No class with identifier %d referenced by attribute %s of %s\n
41 Class %s is not allowed in the loader input file\n
42 Stopped with error.\n
43 WARNING: updated existing instance !\n
44 WARNING: redefining Instance %d of class %s\n
45 Instance %d of class %s has already been defined\n
46 Instance %d of class %s has already been reserved\n
47 UNIQUE violation on attribute `%s', converted to NULL\n
48   %d instances\n
49 Class %s\n
50 Password:
51 \n*** Updating class statistics ***\n
52 %s near line %d (text was '%s')\n
53 Lexical error
54 line %d: syntax error at \"%s\"
55 missing
56 in %s
57 Incompatible arguments, \"%s\", and \"%s\"\n
58 %d instances committed\n
59 \nCannot invoke non-optimized version of loaddb.\n
60 Error parsing \"%s\", as %s type.\n
112   %d instances\r
113 Last committed line : %d
114 error-control-file
115 input file to control error(s) during loading
120 \
loaddb: Import objects and schemas to the database.\n\
usage: %s loaddb [OPTION] database-name\n\
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
      --error-control-file=FILE FILE to control error(s) during loading\n\
      --ignore-class-file=ARG  input file of class names that skip load\n
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
43 %d objects dumped.\n
44 Class %s
45 , %d instances.\n
46 Only %d out of %d objects dumped successfully.\n
47 output directory is too long.\n
48 LSA : %d %d \n
60 \
unloaddb: Export objects and schemas from the database.\n\
usage: %s unloaddb [OPTION] database-name\n\
\n\
valid options:\n\
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
  -C, --CS-mode               client-server mode execution\n


$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 Pass 1\n\n
12 %d objects processed.\n
13 Pass 2\n\n
14 Class %s\n
15 oid:  %d|%d|%d,\n
16 %d instances.\n
17 Updating object.\n
18 ref_oid:  %d.%d.%d, ref_class_oid:  %d.%d.%d\n
19 Couldn't transform object\n
20 Missing class heap file\n
21 Couldn't update object\n
22 Only %d out of %d objects processed successfully.\n
60 \
compactdb: Free unused space in a database.\n\
usage: %s compactdb [OPTION] database-name\n\
\n\
valid options:\n\
  -v, --verbose               enable verbose status messages; default: disabled\n\


$set 15 MSGCAT_UTIL_SET_COMMDB
21 The CUBRID master running on host %s \nwas started at %s
22 has serviced %d requests. %d server(s) running.\n
23 SHUTTING DOWN in %s minutes\n
24 %s
25 Name of server to shutdown :
26 How many minutes until shutdown? :
27 \
cub_commdb: This utility is deprecated. Use 'cubrid' command instead.\n\
usage: cub_commdb [OPTION]\n\
\n\
valid options:\n\
  -P, --server-list                print server processes\n\
  -R, --repl-list                  print replication processes\n\
  -O, --all-list                   print all processes\n\
  -S, --shutdown-server=NAME       shutdown NAME database server\n\
  -K, --shutdown-repl-server=NAME  shutdown NAME replication server\n\
  -k, --shutdown-repl-agent=NAME   shutdown NAME replication agent\n\
  -A, --shutdown-all               shutdown all processes\n\
  -h, --host=HOST                  connect to the master server on the given HOST\n\
  -c, --server-mode=NAME           print NAME database server's HA mode\n
28 Sorry, unknown command: %s\n
29 Could not get host name for local host - aborting\n
30 Could not find parameter file\n
31 Could not connect to master server on %s\n
32 How many minutes until shutdown? :
33 Master will shutdown in %d minutes\n
34 Error receiving data from client
35 the database server %s is not currently running
36 can't exec shell (%s) for %s, %s
37 process fork %s, returned %d, %s
38 can't execute %s


$set 16 MSGCAT_UTIL_SET_PATCHDB
60 \
emergency_patchlog: Patch a database in case of emergency.\n\
usage: %s emergency_patchlog [OPTION] database-name\n\
\n\
valid options:\n\
  -r, --recreate-log           recreate log files; use carefully\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 Wrong value %d for npages\n
21 Wrong value '%s' for volume storage purpose.\n
60 \
addvoldb: Add a volume-file for the database.\n\
usage: %s addvoldb [OPTION] database-name number-of-pages\n\
\n\
valid options:\n\
  -n, --volume-name=NAME       NAME of information volume; default: generate such as "db"_ext1)\n\
  -F, --file-path=PATH         PATH for adding volume file; default: working directory\n\
      --comment=COMMENT        COMMENT for adding volume file; default: none\n\
  -p, --purpose=PURPOSE        PURPOSE for adding volume file; allowed:\n\
                                       DATA - only for data\n\
                                       INDEX - only for indices\n\
                                       TEMP - only for temporary\n\
                                       GENERIC - for all purposes\n\
  -S, --SA-mode                stand-alone mode execution\n\
  -C, --CS-mode                client-server mode execution\n

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 Some inconsistencies were detected in your database.\n Please consult error_log_file %s for additional information.\n
60 \
checkdb: Check consistency of the database.\n\
usage: %s checkdb [OPTION] database-name\n\
\n\
valid options:\n\
  -S, --SA-mode                stand-alone mode execution\n\
  -C, --CS-mode                client-server mode execution\n\
  -r, --repair                 repair when inconsistency is found\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
15 Space description for database '%s' with pagesize %d.\n\n
16 %5d %8s  %s %s  %s\n
17 Couldn't open output file `%s'\n
18 Space description for temporary volumes for database '%s' with pagesize %d.\n\n
19 Volid  Purpose  total_pages  free_pages  Vol Name\n\n
20 Volid  Purpose   total_size   free_size  Vol Name\n\n
60 \
spacedb: Display usage/free space of the database.\n\
usage: %s spacedb [OPTION] database-name\n\
\n\
valid options:\n\
  -o, --output-file=FILE       redirect output messages to FILE; default: none\n\
  -S, --SA-mode                stand-alone mode execution\n\
  -C, --CS-mode                client-server mode execution\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               set the display unit (page, mega, giga, tera, human readable);default : PAGE\n


$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 Estimated Number of Pages = %d\n
60 \
estimate_data: Estimate size of data.\n\
usage: %s estimate_data number-of-instances average-instance-size number-of-attributes variable-number-of-attributes\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 Bad index key type specified = %s\n
16 Bad average key length specified = %d when precision = %d\n
17 Bad arguments specified\n
20 Estimated number of total index pages = %d\n (When index created, after instances have been populated.)\n
21 Estimated number of total index pages = %d\n (When index created, before instances have been populated (average case))\n
22 Estimated number of total index pages = %d\n (When index created, before instances have been populated (worst case))\n
23 For the given input...\n
24 Total number of instances: %d\n
25 Number of different keys : %d\n
26 Average key size         : %d\n
27 Key Type                 : %s\n\n
60 \
estimate_index: Estimate size of an index.\n\
usage: %s estimate_index number-of-instances number-of-different-keys average-key-size key-type\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: Install a database.\n\
usage: %s installdb [OPTION] database-name\n\
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
7 Error opening ELO input file "%s"\n
8 CLASSES:
9 hash filename
10 Name:                         %s\n
11 Size:                         %d\n
12 Page size:                    %d\n
13 Data size:                    %d\n
14 Entry size:                   %d\n
15 Entries per page:             %d\n
16 Cached pages:                 %d\n
17 Number of entries:            %d\n
18 Number of collisions:         %d\n
19 Hash filename:                %s\n
20 Next overflow entry:          %d\n
21 Key type:                     %s\n
22 Address of page headers:      %x\n
23 Address of last page header:  %x\n
24 Address of free page header:  %x\n
25 Address of page bitmap:       %x\n
26 Page bitmap size:             %d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
60 \
diagdb: Dump a database.\n\
usage: %s diagdb [OPTION] database-name\n\
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
                                8 dump log\n


$set 25 MSGCAT_UTIL_SET_LOCKDB
15 Couldn't open output file `%s'\n
59 lockdb cannot run as standlone mode.\n
60 \
lockdb: Display lock-status of the database.\n\
usage: %s lockdb [OPTION] database-name\n\
\n\
valid options:\n\
  -o, --output-file=FILE       redirect output messages to FILE; default: none\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 Only one argument must be given among: tran-index, user, host, progname\n
21 Enter DBA password:
22 Tran index      User name      Host name      Process id      Program name\n
23 -------------------------------------------------------------------------------\n
24 %7d(%1c) %14s %14s %15d %17s\n
25 There are no transactions\n
26 There is not a transaction with the specified argument values.\n
27 Ready to kill the following transactions:\n\n
28 Do you wish to proceed ? (Y/N)
29 Killing transaction associated with transaction index %d\n
30 Killing of the following transactions has failed\n\n
31 The transaction does not finish yet, but it may be doing the rollback work.\n Please, wait a second and try it later if needed.\n
59 killrtan cannot run as standlone mode.\n
60 \
killtran: Manage transactions of the database.\n\
usage: %s killtran [OPTION] database-name\n\
\n\
valid options:\n\
  -i, --kill-transaction-index=INDEX kill transaction with transaction INDEX\n\
      --kill-user-name=ID            kill all transactions with user ID\n\
      --kill-host-name=HOST          kill all transactions with client HOST\n\
      --kill-program-name=NAME       kill all transactions with client program NAME\n\
  -p, --dba-password=PASS            password of the DBA user; will prompt if don't specify\n\
  -d, --display-information          display information about active transactions\n\
  -f, --force                        kill the transaction without a prompt for verification\n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1 database-name
2 required argument, no default
3 compatibility-check
4 compatibility check only
5 backup-destination
6 a directory path or device for the destination of the backup volumes
20 database '%s' does not exist.
21 '%s' can't be migrated to %s, because its DB version is too low.\n
22 '%s' don't need to be migrated.\n
23 '%s' needs to be migrated.\n
24 Backup is recommended after migration. Do you want to back up your database now? (y/n):
25 We recommend to back up your new database with backupdb command after migration.
26 \n*** WARNING: Backup your database before you start migrating to %s ***\n\nTo recover errors which might be occurred during migration, we strongly recommend to back up your database.\nHave you already backed up your database? (y/n):
27 \n1. Run backupdb \n2. Enter the new backup path for the new database.(current path: %s)\n3. Exit \n
28 \nThe new backup path for the database:
29 \nMigration and backup have been successfully completed.
30 Backup failed. Please, retry to back up your database.
31 \n\n*** WARNING : Once the migration is completed, you cannot restore the database with the former backup volumes.\nNotice that the previous version of backup files can be restored only on the corresponding version of restoredb.\nPlease, check the former backup volumes is securely kept.\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: Alter a host of the database.\n\
usage: %s alterdbhost [OPTION] database-name\n\
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

$set 35 MSGCAT_UTIL_SET_REPLSERVER
1 database-name
2 port-number
3 error log file
4 the number of agents
11 Internal Error(%d): communication error
12 Internal Error(%d): can't allocate memory
13 Internal Error(%d): IO error
14 Internal Error(%d): General
21 ERROR(%d): can't open the active log file of the master - %s.
22 ERROR(%d): can't open the databases.txt file.
23 ERROR(%d): %s is not exists in databases.txt.
24 ERROR(%d): can't open the catalog file.
25 ERROR(%d): Invalid Argument. Please provide correct arguments.
26 ERROR(%d): can't connect to master. Please check if the repl_server is already running, or the master is running.
27 ERROR(%d): The port id of the repl_server is already used. Please check if the repl_server using same TCP port id is already run.
28 WARNING(%d): excess of the maximum number of agent connections.Ignoring client connect request.
29 WARNING(%d): close the connection of repl_agent.
30 WARNING(%d): Request queue is full. Ignore the request.
31 ERROR(%d): Can't open the archive log log - %s.
32 ERROR(%d): Can't read the archive log log - %s.

$set 36 MSGCAT_UTIL_SET_REPLAGENT
1 database-name
2 password
3 create archive
4 log dump
5 page size
11 Internal Error(%d): communication error
12 Internal Error(%d): can't allocate memory
13 Internal Error(%d): IO error
14 Internal Error(%d): General
15 Internal Error(%d): can't unzip log data
16 Internal Error(%d): query error:%s
21 ERROR(%d): can't connect to the repl_server. Please check the portnum and state of the repl_server process.
22 ERROR(%d): can't open the copy log file - %s.
23 ERROR(%d): can't create the archive file of the copy log - %s
24 ERROR(%d): can't open the catalog file.
25 ERROR(%d): error during access the trail log file. - %s
26 ERROR(%d): can't connect to master. Please check if the repl_agent is already running, or the master is running.
27 ERROR(%d): can't login to the slave. - %s
28 ERROR(%d): can't connect to the slave. - %s
29 ERROR(%d): Slave DB stopped - %s
30 ERROR(%d): Replication is broken - %s
31 ERROR(%d): Can't get agent id from the repl_server. Please check the state of the repl_server.
32 ERROR(%d): Can't get log header from the repl_server. Please check the state of the repl_server.
33 ERROR(%d): Can't get log page from the repl_server. Please check the state of the repl_server.
34 ERROR(%d): Can't get the parameters from the distributor DB.
35 ERROR(%d): can't login to the distributor DB. - %s
36 ERROR(%d): can't connect to the distributor DB. - %s
37 ERROR(%d): There are too many slaves registered in slave_info class of distributor db
38 ERROR(%d): There is no master info for the target slave. Please check the trail_info class of distributor db.
39 ERROR(%d): There are too many masters registered in master_info class of distributor db
40 ERROR(%d): Invalid trail info. Please check the trail_info class of distributor DB
41 ERROR(%d): Unilaterally aborted by the system. Try to increase the value of the max_block_size and max_block_count(cubrid.conf).
51 LOG(%d): The repl_agent process is restarting.
52 LOG(%d): An update log record of the replication was converted into insert log.

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 Cannot open output file `%s'\n
59 plandump cannot run as standlone mode.\n
60 \
plandump: Display or control cached query plans.\n\
usage: %s plandump [OPTION] database-name\n\
\n\
valid options:\n\
  -d, --drop                   drop all plans in the server's cache\n\
  -o, --output-file=FILE       redirect output messages to FILE; default: none\n

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 Cannot open output file `%s'\n
21 System parameters to be used by the client program.\n
22 System parameters loaded by the server '%s'\n
23 System parameters to be used in standalone mode.\n
60 \
paramdump: Dump system parameter settings.\n\
usage: %s paramdump [OPTION] database-name\n\
\n\
valid options:\n\
  -o, --output-file=FILE       redirect output message to FILE; default: none\n\
  -b, --both                   dump both client and server parameters\n\
  -S, --SA-mode                stand-alone mode execution\n\
  -C, --CS-mode                client-server mode execution\n

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 Worng value \"%s\" for the server's HA running mode.\n
12 Cannot change the server's HA running mode.\n
21 Enter DBA password:
22 The server `%s''s current HA running mode is %s.\n
23 The server `%s''s HA running mode is changed to \"%s\".\n
24 The server was not set to HA mode.\n
59 changemode cannot run as standlone mode.\n
60 \
changemode: Display or change the server's HA running mode.\n\
usage: %s changemode [OPTION] database-name\n\
\n\
valid options:\n\
  -m, --mode=MODE             change the server mode to MODE; default; none\n\
  -w, --wait                  wait util the mode is changed\n\
  -f, --force                 force to change\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 Worng value \"%s\" for the copylogdb's running mode.\n
21 Enter DBA password:
22 The server was not set to HA mode.\n
59 copylogdb cannot run as standlone mode.\n
60 \
copylogdb: Copy log pages from the server.\n\
usage: %s copylogdb [OPTION] database-name\n\
\n\
valid options:\n\
  -L, --log-path=PATH         directory PATH for copied log volumes\n\
  -m, --mode=MODE             operation mode; default: sync\n\
                              MODE: sync or async\n

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 Enter DBA password:
22 The server was not set to HA mode.\n
59 applylogdb cannot run as standlone mode.\n
60 \
applylogdb: Apply copied log to the server.\n\
usage: %s applylogdb [OPTION] database-name\n\
\n\
valid options:\n\
  -L, --log-path=PATH         directory PATH of copied log volumes\n\
      --max-mem-size=SIZE     maximum process SIZE in MB; max 1000\n
