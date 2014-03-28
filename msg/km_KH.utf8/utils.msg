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
$ ចំនួនសំណុំខាងក្រោម ត្រូវបានកំណត់ដូចតទៅ:
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
1 ប្រវែងមិនសុពល ឬអក្សរ`%c' ក្នុងឈ្មោះដែលតាបេស`%s'\n
2 មិនអាចបើកបញ្ជី output `%1$s'\n
3 ឈ្មោះដែលប្រើមិនសុពលឬបាត់.\n
4 ឈ្មោះដែលប្រើមិនសុពល `%1$s'\n
5 តារាងខាងក្រោមមានឈ្មោះប្រើរួចដែលស្គាល់:\n
6 អក្សរមិនសុពល `%c' ក្នុងឈ្មោះvolume `%s'\n
9 \
%s (CUBRID utilities)\n\
%s\n\
Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n
10 \
cubrid administrator utility, version %s\n\
usage: %s <utility-name> [args]\n\
ប្រភេទ '%s <utility-name>' ជាជំនួយលើការប្រើប្រាស់ពិសេស។\n\
\n\
ឈ្មោះadmin utilityដែលមាន:\n\
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
%s ជាឧបករណ៍សម្រាប់ DBMS។\n\
សម្រាប់ពត៌មានបន្ថែម, សូមមើលhttp://www.cubrid.org\n
12 ឈ្មោះ'%1$s' utility មិនសុពល។\nType 'cubrid --help' ដើម្បីមើល usage។\n
13 '%1$s' command មិនសុពល។\nType 'cubrid --help' ដើម្បីមើលusage។\n
14 មិនអាចផ្ទុកpropertiesមួយឬច្រើនក្នុងបញ្ជីconfigurationទេ។\n
15 @ %s %s: %s\n
16 @ %s %s\n
17 ++ %s '%s' មិនដំណើរការ។\n
18 ++ %1$s មិនដំណើរការ។\n
19 ++ %s '%s' កំពុងដំណើរការ។\n
20 ++ %1$s កំពុងដំណើរការ។\n
21 ++ %s %s: %s\n
22 មិនឃើញពាក្យសំអាងខ្ទាស់មួយឬច្រើន។\nType 'cubrid --help' ដើម្បីមើល usage។ \n
23 cubrid utility, version %s\n\
usage: %s <utility-name> [args]\n\
ប្រភេទ '%s <utility-name>' ជាជំនួយលើការប្រើប្រាស់ពិសេស។\n\
\n\
service's utilitiesដែលមាន:\n\
    service\n\
    server\n\
    broker\n\
    manager\n\
    heartbeat\n\
\n\
administrator's utilitiesដែលមាន:\n\
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
%s ជាឧបករណ៍សម្រាប់ DBMS។\n\
សម្រាប់ពត៌មានបន្ថែម, សូមមើលhttp://www.cubrid.org\n
24 cubrid utility, version %s\n\
usage: %s service <command>\n\
\n\
commandដែលមាន:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 cubrid utility, version %s\n\
usage: %s server <command> [args]\n\
\n\
commandដែលមាន:\n\
    start       [ឈ្មោះដែលតាបេស]\n\
    stop        [ឈ្មោះដែលតាបេស]\n\
    restart     [ឈ្មោះដែលតាបេស]\n\
    status\n\
    acl         <status|reload> ឈ្មោះដែលតាបេស\n
37 កំព្យូទ័របំរើសេវាត្រូវកំណត់អោយHA.\n
26 cubrid utility, version %s\n\
usage: %s broker <command> [args]\n\
\n\
commandដែលមាន:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [-S] [-P] [-m] [-c] [<expr>]\n\
    on          ឈ្មោះbroker\n\
    off         ឈ្មោះbroker\n\
    reset       ឈ្មោះbroker\n\
    acl         <status|reload> [ឈ្មោះbroker]\n\
    getid       -b <ឈ្មោះbroker> [-f] shard-key\n\
    test        ឈ្មោះbroker [-D <database_name>] [-u <user_name>] [-p <user_password>] [-c <SQL_command>] [-i <input_file>] [-o <output_file>] [-v] [-s]\n
27 cubrid utility, version %s\n\
usage: %s manager <command>\n\
\n\
commandដែលមាន:\n\
    start\n\
    stop\n\
    status\n
28 cubrid utility, version %s\n\
usage: %s heartbeat <command> [args]\n\
\n\
commandដែលមាន:\n\
    start       [database-name]\n\
    stop        [-i|database-name]\n\
    copylogdb   <start|stop> database-name node-name\n\
    applylogdb  <start|stop> database-name node-name\n\
    replication <start|stop> node-name\n\
    status\n\
    reload\n
31 មិនត្រូវការពាក្យសំអាងខ្ទាស់'%1$s'។\n
32 មិនឃើញឈ្មោះដែលតាបេស។\n
33 %1$s: ជម្រើសបែបនេះមិនស្រុះស្រួលគ្នា។\n
34 ប៉ារ៉ាម៉ែត្រមិនសុពល %s %s\n
35 មិនអាចចែកចាយ memory។\n
36 The server was not configured for HA.\n
38 ៣៨កំព្យួទ័របំរើសេវាដែលបានត្រូវកំណត់អោយHAមិនចេញពីបញ្ជី ha_db_list.\n
39 ៣៩កំព្យួទ័របំរើសេវាដែលបានត្រូវកំណត់អោយHAមិនចេញពី ha_node_list.\n
40 Invalid command.\n
41 cubrid manager server is not installed.\n
42 Invalid argument.\n
43 File(%1$s) open error.\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 WARNING: ជម្រើស -t មិនស្គាល់ក្នុងប្រតិបត្តិការ stand-alone mode។\n
31 ៣១ កំហុសៈ ទីគោលដៅអត់មានទេឬមិនឋិតនៅក្នុងមាតិកាទេ.\n
60 \
backupdb: ថតចម្លងដែលតាបេសបន្ថែម។\n\
usage: %1$s backupdb [OPTION] ឈ្មោះដែលតាបេស\n\
\n\
valid options:\n\
  -D, --destination-path=PATH  រក្សាទុកសំណុំពត៌មាន volumes ក្នុង PATHបញ្ជីឈ្មោះ; default: បញ្ជីឈ្មោះlogរបស់ដែលតាបេស។ \n\
  -r, --remove-archive         ដកចេញ log-archivesដែលមិនចាំបាច់; ប្រើក្បួនពិគ្រោះដោយប្រុងប្រយ័ត្ន\n\
  -l, --level=LEVEL            ថតចម្លងបន្ថែមនូវ level; default: levelពេញ \n\
                               LEVEL ត្រូវបានតម្រូវជា:\n\
                                   0 - full\n\
                                   1 - incremental 1\n\
                                   2 - incremental 2\n\
  -o, --output-file=FILE       ព្រីន/ថតចម្លងសារbackupលំអិត ទៅកាន់FILE\n\
  -S, --SA-mode                ប្រតិបត្តិការstand-alone mode \n\
  -C, --CS-mode                ប្រតិបត្តិការclient-server mode \n\
      --no-check               កុំពិនិត្យមើលភាពជាប់លាប់នៃដែលតាបេស; default: ពិនិត្យ\n\
  -t, --thread-count=COUNT     ចំនួននៃ threads; default: 0(auto)\n\
  -z, --compress               បង្ហាប់សំណុំពត៌មាន volumes; default: មិនបង្ហាប់\n\
  -e, --except-active-log      មិនបញ្ជូលlog volumeសកម្ម; default: បញ្ជូល\n\
      --sleep-msecs=N          គេង N  1Mអានមីលីវិនាទី កំហុស: 0\n
71 Incompatible arguments, \"%s\", and \"%s\"\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 ERROR: ឈ្មោះតម្រូវជាមុន ដូចគ្នាសុទ្ធសាធទៅនឹងឈ្មោះប្រភព។\n
31 ERROR: ឈ្មោះvolext-path មិនមានឬក៏មិនអាចសរសេរ។\n
32 ERROR: vols-tofrom-paths_file មិនមានឬក៏មិនអាចដំណើរការបាន។\n
60 \
copydb: ថតចម្លងដែលតាបេស\n\
usage: %1$s copydb [OPTION] ឈ្មោះដែលតាបេសsrc ឈ្មោះដែលតាបេសdest\n\
\n\
ជម្រើសសុពល:\n\
      --server-name=HOST          ឈ្មោះHOSTរបស់server; default: ឈ្មោះhostបច្ចុប្បន្ន \n\
  -F, --file-path=PATH            PATHបញ្ជីឈ្មោះសម្រាប់volumesដែលតាបេស; default: បញ្ជីឈ្មោះបច្ចុប្បន្ន\n\
  -L, --log-path=PATH             PATHបញ្ជីឈ្មោះសម្រាប់log volumes; default: ដូចគ្នានឹង file-path\n\
  -B, --lob-base-path=PATH        PATHបញ្ជីឈ្មោះមូលដ្ឋានសម្រាប់ទិន្នន័យLOBខាងក្រៅ; default: <file-path>/lob\n\
  -E, --extended-volume-path=PATH PATHបញ្ជីឈ្មោះសម្រាប់volumesពត៌មានពង្រីក; default: ជាpathដែលដូចនឹងvolumesដែលតាបេស ឬpathដែលផ្តល់ឲ្យនៅក្នុងcontrol-file\n\
  -i, --control-file=FILE         ថតចម្លង volumes នៅលើទីតាំងមួយចំនួនដែលបានសរសេរនៅក្នុង control file; default: none\n\
  -r, --replace                   ដាក់ជំនួសដែលតាបេសដែលមាន; default: កុំជំនួស\n\
  -d, --delete-source             ដកចេញដែលតាបេសប្រភពមួយ; default: កុំដកចេញ\n\
      --copy-lob-path             ថតចម្លងPATHផ្ទុកខាងក្រៅសម្រាប់ទិន្នន័យLOBពីដែលតាបេសប្រភព។ មិនអាចត្រូវប្រើជាមួយនឹងជម្រើស -B; default: កុំថតចម្លង\n


$set 4 MSGCAT_UTIL_SET_CREATEDB
40 ក្រុម `%1$s' ពុំមាន\n
41 User `%1$s' ពុំមាន\n
42 commandដែលមិនស្គាល់ `%1$s'\n
43 មិនអាចបើក output file `%1$s'\n
44 មិនអាចផ្លាស់ប្តូរទីតាំងដែលតាបេសដែលមាន។\n
45 បង្កើតទិន្នន័យដែលមានទំហំ %1$s ហើយរក្សាទុកនៅកន្លែង %2$s. ទំហំថាសរឹងទាំងអស់ត្រូវការ %3$s.\n
46 មិនអាំចបង្កើតដែលតាបេស\n
47 បញ្ហាជាមួយ user file `%1$s'\n
48 ទំហំ '%s', %s ហួសកំណត់. សូមជ្រើសរើសទំហំរវាង %s និង %s.\n
49 តំលៃនៃ%s'%s'មោឃ\n
60 \
createdb: បង្កើតដែលតាបេស\n\
usage: %1$s createdb [OPTION] database-name database-locale\n\
\n\
ទ្រង់ទា្រយទិន្នន័យគឺជា <language>.<charset>; ឧទាហរណ៍ៈ  en_US.iso88591, ko_KR.utf8\n\
ជម្រើសសុពល:\n\
      --db-volume-size=SIZE           ទំហំនៃvolumeពត៌មានទីមួយ\n\
                                      default: db_volume_size ក្នុង cubrid.conf\n\
      --db-page-size=SIZE             ទំហំទំព័រដែលតាបេសក្នុង bytes; default: 16KB\n\
      --log-volume-size=SIZE          ទំហំនៃ log archives\n\
                                      default: log_volume_size ក្នុង cubrid.conf\n\
      --log-page-size=SIZE            ទំហំទំព័រlogក្នុង bytes; default: ទំហំទំព័រដែលតាបេស\n\
      --comment=COMMENT               COMMENT សម្រាប់ដែលតាបេស; default: none\n\
  -F, --file-path=PATH                PATHបញ្ជីឈ្មោះសម្រាប់ volumesដែលតាបេស; default: បញ្ជីឈ្មោះបច្ចុប្បន្ន\n\
  -L, --log-path=PATH                 PATHបញ្ជីឈ្មោះសម្រាប់ log volumes; default: ដូចគ្នានឹង file-path\n\
  -B, --lob-base-path=PATH            PATHបញ្ជីឈ្មោះមូលដ្ឋានសម្រាប់ទិន្នន័យLOBខាងក្រៅ; default: <file-path>/lob\n\
      --server-name=HOST              ឈ្មោះHOSTរបស់server; default: ឈ្មោះhostបច្ចុប្បន្ន\n\
  -r, --replace                       ដាក់ជំនួសដែលតាបេសដែលមាន; default: កុំដាក់ជំនួស\n\
      --more-volume-file=FILE         FILE មានចំណុចជាក់លាក់សម្រាប់បង្កើតvolumesបន្ថែម; default: none\n\
      --user-definition-file=FILE     FILE មាននិយមន័យ user; default: none\n\
      --csql-initialization-file=FILE FILE មានSQLs សម្រាប់ចាប់ផ្តើមដំណើរការដែលតាបេស; default: none\n\
  -o, --output-file=FILE              ប្តូរទិសដៅសារ outputទៅកាន់ FILE; default: none\n\
  -v, --verbose                       អាចដំណើការបាននូវសារ verbose status; default: មិនអាច\n


$set 6 MSGCAT_UTIL_SET_DELETEDB
60 \
deletedb: ដកចេញដែលតាបេស.\n\
usage: %1$s deletedb [OPTION] ឈ្មោះដែលតាបេស\n\
\n\
ជម្រើសសុពល:\n\
  -o, --output-file=FILE       ប្តូរទិសដៅសារ outputទៅកាន់ FILE; default: none\n\
  -d, --delete-backup          ដកចេញសំណុំពត៌មាន volumes; default: កុំដកចេញ\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 ERROR: ឈ្មោះvolext-path ពុំមានឬមិនបានសរសេរ\n
32 ERROR: vols-tofrom-paths_file ពុំមានឬមិនមានដំណើរការ\n
60 \
renamedb: ដាក់ឈ្មោះដែលតាបេសឡើងវិញ.\n\
usage: %1$s renamedb [OPTION] ឈ្មោះដែលតាបេសsrc ឈ្មោះដែលតាបេសdest\n\
\n\
ជម្រើសសុពល:\n\
  -E, --extended-volume-path=PATH PATHបញ្ជីឈ្មោះសម្រាប់volumesពត៌មានដែលពង្រីក; default: ជាpathដែលដូចនឹងvolumesដែលតាបេស ឬpathដែលផ្តល់ឲ្យក្នុងcontrol-file\n\
  -i, --control-file=FILE         មានvolumesលើទីតាំងមួយចំនួនត្រូវបានសរសេរក្នុងcontrol file; default: none\n\
  -d, --delete-backup             ដកចេញសំណុំពត៌មាន volumes; default: កុំដកចេញ\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 %1$s មានដំណើរការរួចហើយ។\n
12 ការចាប់ផ្តើមMASTER SERVER។ \n
13 MASTER SERVER មានហើយ។\n
14 Error កំពុងស្វែងរកឈ្មោះ local host\n
15 មិនអាចស្វែងរកfileប៉ារ៉ាម៉ែត្រ\n
16 មិនអាចផ្តើមដំណើរការ master\n
17 Server %s ធ្លាក់ចុះក្នុងនាទី %d 
18 Server %1$s បញ្ជាក់អំពីការបិទ។\nនេះអាចចំណាយពេលជាច្រើននាទី។ សូមរង់ចាំ។
19 រកមិនឃើញServer %1$s ។
20 Master ធ្លាក់ចុះក្នុងនាទី %1$d \n
21 Heartbeat failover was finished. Please wait until all shutdown processes of cub_servers are completed.\n

$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 មិនអាចបើក output file `%1$s'\n
60 \
optimizedb: សម្រួលដែលតាបេសឲ្យដំណើរការកាន់តែល្អ\n\
usage: %1$s optimizedb [OPTION] ឈ្មោះដែលតាបេស\n\
\n\
ជម្រើសសុពល:\n\
  -n, --class-name=NAME        ធ្វើទំនើបកម្មស្ថិតិសម្រាប់  NAME class; default: គ្រប់classes\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 ការរៀបចំសម្រួលទិន្នន័យមិនសុពល, ការរៀបចំសម្រួលគឺ dd-mm-yyyy:hh:mm:ss ឬ 'backuptime'\n
20 Restoredb ត្រូវបានបដិសេសធ ឬerrorបានលេចឡើង។\n
60 \
restoredb: ស្ថាបនាដែលតាបេសឡើងវិញ។\n\
usage: %1$s restoredb [OPTION] ឈ្មោះដែលតាបេស\n\
\n\
ជម្រើសសុពល:\n\
  -d, --up-to-date=DATE             ស្ថាបនាដែលតាបេសឡើងវិញផ្អែកតាមលក្ខខណ្ឌរបស់វាតាមកាលបរិច្ឆេទដែលកំណត់រួច។ កាលបរិច្ឆេទត្រូវកំណត់ដូចតទៅ: dd-mm-yyyy:hh:mm:ss or 'backuptime' keyword\n\
      --list                        បង្ហាញតារាងឈ្មោះសំណុំពត៌មាន volumesដោយពុំមានការស្ថាបនាឡើងវិញ។\n\
  -B, --backup-file-path=PATH       PATH គឺជាបញ្ជីឈ្មោះនៃសំណុំពត៌មាន volumesដែលនឹងត្រូវស្ថាបនាឡើងវិញ។\n\
  -l, --level=LEVEL                 កម្រិតនៃ backupដែលនឹងត្រូវស្ថាបនាឡើងវិញ; សូមមើលសំណុំពត៌មាន usage; default: 0(សំណុំពត៌មានពេញ)\n\
  -p, --partial-recovery            ធ្វើដំណើរការឡើងវិញដោយផ្លែក ប្រសិនបើ log archive ពុំមានវត្តមាន។\n\
  -o, --output-file=FILE            ប្តរទិសសារ output ទោះកាន់ FILE; default: none\n\
  -u, --use-database-location-path  ស្ថាបនាឡើងវិញនូវដែលតាបេស និងlog volumes ទៅកាន់pathដែលត្រូវបានកំណត់យ៉ាងជាក់លាក់ក្នុងfileទីតាំងដែលតាបេស\n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 មិនឃើញមានឈ្មោះដែលតាបេស។\n
20 '%s' file ពុំមាន %d lines.\n
21 ការរំខាន ... \n
22 ការផ្ទុកត្រូវបានរំខាន - កិច្ចសម្រួលដំណើរការបច្ចុប្បន្នត្រូវបានប្រតិបត្តិ\n
23 ការផ្ទុកត្រូវបានរំខាន - កិច្ចសម្រួលដំណើរការបច្ចុប្បន្នត្រូវបានទម្លាក់ចោល\n
24 មិនអាចបើក file `%1$s'\n
25 \n*** ពិនិត្យមើល syntax ***\n\n
26 %1$d errors ត្រូវបានដកចេញ\n
27 \n*** ដាក់បញ្ជូល objects ***\n\n
28 %1$8d objectsសរុបត្រូវបានដាក់បញ្ជូល\n
29 %1$8d objectsដែលត្រូវបញ្ជាក់ទាំងអស់ ត្រូវបានដាក់បញ្ជូលជាមួយនឹង default contents.\n
30 \n*** ប្រតិបត្តិកិច្ចសម្រួលដំណើរការ ***\n
31 \n*** បិទដែលតាបេស ***\n
32 Line %1$d:
33 Error ដកឃ្លា `%s' ជាតម្លៃ `%s' សម្រាប់ attribute `%s' of `%s'\n
34 ពុំឃើញ domain សម្រាប់ attribute %s នៃ %s\n
35 សម្រាប់ attribute %s នៃ %s, ប្រភេទ %s មិនត្រូវបានអនុញ្ញាតនៅក្នុង domain នៃសំណុំនេះទេ\n
36 សម្រាប់ attribute %s នៃ %s, ដែលតម្រូវតាមប្រភេទ %s, ទទួលបានចំនួនសំណុំមួយ\n
37 សម្រាប់ attribute %s នៃ %s, ដែលតម្រូវតាមប្រភេទ %s, ទទួលបាន %s\n
38  ពុំមាន class ដែលដាក់ឈ្មោះ %s ត្រូវសំអាងដោយ attribute %s នៃ %s​​​​​​​ ទេ \n
39 ពុំមាន class ដែលដាក់ឈ្មោះ %1$s, មិនស្គាល់ tuples\n
40 ពុំមាន class ដែលមាន identifier %d ត្រូវសំអាងដោយ attribute %s នៃ %s ទេ \n
41 Class %1$s មិនត្រូវអនុញ្ជាតនៅក្នុង loader input file\n
42 ត្រូវបានបញ្ឈប់ដោយសារ error។\n
43 WARNING: instanceដែលមានត្រូវបានធ្វើទំនើបកម្ម !\n
44 WARNING: កំណត់ឡើងវិញនូវ Instance %d នៃ class %s\n
45 Instance %d នៃ class %s ត្រូវបានកំណត់រួចហើយ\n
46 Instance %d នៃ class %s ត្រូវបានរក្សាបម្រុងទុករួចហើយ\n
47 UNIQUE violation on attribute `%1$s', ត្រូវបានប្តូរជា NULL\n
48   %1$d instances\n
49 Class %1$s\n
50 កំណត់សម្គាល់សម្ងាត់:
51 \n*** ការធ្វើទំនើបកម្មស្ថិតិclass ***\n
52 %s នៅក្បែរ line %d (text គឺជា '%s')\n
53 Lexical error
54 line %d: syntax error នៅត្រង់ \"%s\"
55 បាត់
56 ក្នុង %1$s
57 សេចក្តីសំអាងដែលមិនស៊ីគ្នា, \"%s\", និង \"%s\"\n
58 %1$d instances ត្រូវបានប្រតិបត្តិ\n
59 \nមិនអាចពន្លេចឲ្យប្រសើរនូវ versionដែលមិនអាចដំណើរការល្អនៃ loaddb។\n
60 ការដកឃ្លាError \"%s\", ជាប្រភេទ %s​៕\n
112   %1$d instances\r
113 lineដែលត្រូវបានប្រតិបត្តិចុងក្រោយ : %1$d
114 error-control-file
115 input file នឹងគ្រប់គ្រង error(s)ក្នុងអម្លុងដំណើរការផ្ទុក
116 Total %8d object(s) inserted, %d object(s) failed.\n
117 Load failed.\n
118 Maximum length of class name is %1$d bytes.\n
120 \
loaddb: នាំចូម objects និង schemas ទៅកាន់ដែលតាបេស\n\
usage: %1$s loaddb [OPTION] ឈ្មោះដែលតាបេស\n\
\n\
ជម្រើសសុពល:\n\
  -u, --user=ID                 user ID សម្រាប់សិទ្ធិចូលដែលតាបេស; default: "public"\n\
  -p, --password=PASS           កំណត់សម្គាល់សម្ងាត់user; default: none\n\
      --data-file-check-only    ពិនិត្យមើល syntaxសម្រាប់fileទិន្នន័យ ដោយពុំផ្ទុក; default: ការផ្ទុកបន្ទាប់ពីការពិនិត្យ\n\
  -l, --load-only               ផ្ទុកfileទិន្នន័យ ដោយពុំពិនិត្យមើលsyntax; default: ការផ្ទុកបន្ទាប់ពីការពិនិត្យ\n\
      --estimated-size=NUMBER   ចំនួនប់ាន់ប្រមាណនៃ instances; default: 5000\n\
  -v, --verbose                 បង្កើនសមត្ថភាពសារ verbose status; default: មិនអាច\n\
  -c, --periodic-commit=COUNT   ការបញ្ជូលCOUNT សម្រាប់ការប្រតិបត្តិទៀងពេល; default: ពុំមានការប្រតិបត្តិអម្លុងដំណើរការផ្ទុក\n\
      --no-oid                  កុំប្រើ OID\n\
      --no-statistics           កុំធ្វើទំនើបកម្មស្ថិតិអម្លុងដំណើរការផ្ទុក\n\
  -s, --schema-file=FILE[:LINE] បង្កើតschema ពី LINE ជាមួយ FILE\n\
  -i, --index-file=FILE[:LINE]  បង្កើត index ពី LINE ជាមួយ FILE\n\
  -d, --data-file=FILE          ផ្ទុកទិន្នន័យជាមួយ FILE\n\
  -t, --table=TABLE             table name that is substituted for missing class header in data file\n\
      --error-control-file=FILE FILE ដ្បើម្បីគ្រប់គ្រង error(s) អម្លុងដំណើរការផ្ទុក\n\
      --ignore-class-file=FILE  ដាក់បញ្ជូល file នៃឈ្មោះ classដែលរំលងការផ្ទុក\n
121 \
load_noopt: utilityនេះ ពុំមានតម្លៃ។ សូមប្រយ័ត្នក្នុងការប្រើប្រាស់\n\
usage: load_noopt [OPTION] ឈ្មោះទិន្នន័យ\n\
\n\
ជម្រើសសុពល:\n\
  -u, --user=ID                 user ID សម្រាប់សិទ្ធិចូលដែលតាបេស; default: "public"\n\
  -p, --password=PASS           កំណត់សម្គាល់សម្ងាត់user; default: none\n\
      --data-file-check-only    ពិនិត្យមើល syntaxសម្រាប់fileទិន្នន័យ ដោយពុំផ្ទុក; default: ការផ្ទុកបន្ទាប់ពីការពិនិត្យ\n\
  -l, --load-only               ផ្ទុកfileទិន្នន័យ ដោយពុំពិនិត្យមើលsyntax; default: ការផ្ទុកបន្ទាប់ពីការពិនិត្យ\n\
      --estimated-size=NUMBER   ចំនួនប់ាន់ប្រមាណនៃ instances; default: 5000\n\
  -v, --verbose                 បង្កើនសមត្ថភាពសារ verbose status; default: ដែលមិនអាច\n\
  -c, --periodic-commit=COUNT   ការបញ្ជូលCOUNT សម្រាប់ការប្រតិបត្តិទៀងពេល; default: ពុំមានការប្រតិបត្តិអម្លុងដំណើរការផ្ទុក\n\
      --no-oid                  កុំប្រើ OID\n\
  -s, --schema-file=FILE[:LINE] បង្កើតschema ពី LINE ជាមួយ FILE\n\
  -i, --index-file=FILE[:LINE]  បង្កើត index ពី LINE ជាមួយ FILE\n\
  -d, --data-file=FILE          ផ្ទុកទិន្នន័យជាមួយ FILE\n\
      --error-control-file=FILE FILE ដើម្បីគ្រប់គ្រង error(s)អម្លុងដំណើរការផ្ទុក\n


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 តម្លៃទំព័រផ្ទុកកំបាំង ពុំមានសុពលភាព។\n
42 តម្លៃទំហំទំព័រផ្ទុកកំបាំង ពុំមានសុពលភាព។\n
43 %1$d objects ត្រូវបានបោះចោល។\n
44 Class %1$s
45 , %1$d instances.\n
46 តែ %d ដែលពុំមាន %d objectsប៉ុណ្ណោះ ត្រូវបានបោះចោលដោយជោគជ័យ។\n
47 បញ្ជីឈ្មោះoutputវែងពេក។\n
48 LSA : %d %d \n
49 កំណត់សម្គាល់សម្ងាត់:
60 \
unloaddb: នាំចេញ objects និង schemas ពីដែលតាបេស។\n\
usage: %1$s unloaddb [OPTION] ឈ្មោះដែលតាបេស\n\
\n\
ជម្រើសសុពល:\n\
  -u, --user=ID               user ID សម្រាប់សិទ្ធិចូលដែលតាបេស; default: "publdba"\n\
  -p, --password=PASS         កំណត់សម្គាល់សម្ងាត់user; default: none\n\
  -i, --input-class-file=FILE បញ្ជូល FILE នៃឈ្មោះតារាង; default: បោះចោល classesទាំងអស់\n\
      --include-reference     មាន តារាងដែលត្រូវបានសំអាង; ជាមួយជម្រើស -i ; default: ដែលមិនអាច\n\
      --input-class-only      មានតែ classកំណត់ពិសេស; ជាមួយជម្រើស -i ; default: ដែលមិនអាច\n\
      --lo-count=COUNT        lo file COUNT per a directory; default: 0\n\
      --estimated-size=NUMBER ចំនួនប៉ាន់ប្រមាណនៃ instances; default: ស្វ័យគណនា\n\
      --cached-pages=NUMBER   ចំនួនទំព័រផ្ទុកកំបាំង; default: 100\n\
  -O, --output-path=PATH      PATH នៃបញ្ជីឈ្មោះoutput ; default: បញ្ជីឈ្មោះការងារ\n\
  -s, --schema-only           បោះចោលតែ schemaប៉ុណ្ណោះ; default: schema និង objects\n\
  -d, --data-only             បោះចោលតែ objectsប៉ុណ្ណោះ; default: schema និង objects\n\
      --output-prefix=PREFIEX ដាក់បុព្វបទសម្រាប់ output files; default: ឈ្មោះដែលតាបេស\n\
      --hash-file=FILE        FILE សម្រាប់ hash; default: ត្រូវបានបង្កើតឲ្យមានដោយ tmpnam()\n\
  -v, --verbose               បង្កើនសមត្ថភាពសារ verbose​ status; default: ដែលមិនអាច\n\
      --use-delimiter         ប្រើ '"' នៅត្រង់ identifier ចាប់ផ្តើមនិងចប់; default: កុំប្រើ\n\
  -S, --SA-mode               ការប្រតិបត្តិstand-alone mode \n\
  -C, --CS-mode               ការវ្រតិបត្តិclient-server mode \n\
      --datafile-per-class    បង្កើតobject file សម្រាប់ classនីមួយៗ; default: ដែលមិនអាច\n



$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 \nចម្លងទៅកាន់ 1\n\n
12 %1$d objects ត្រូវបានដំណើរការ។\n
13 \nចម្លងទៅកាន់  2\n\n
14 Class %1$s\n
15 oid:  %d|%d|%d,\n
16 %1$d instances.\n
17 ធ្វើទំនើបកម្ម object.\n
18 ref_oid:  %d.%d.%d, ref_class_oid:  %d.%d.%d\n
19 មិនអាចផ្ទេរ object\n
20 បាត់សំណុំ class file\n
21 មិនអាបធ្វើទំនើបកម្ម object\n
22 តែ %d ដែលពុំមាន %d objects ត្រូវបានដំណើរការដោយជោគជ័យ។\n
23 COMPACTDB បានចាប់ផ្តើមរួចហើយ។\n
24 ចំនួននៃទំព័រត្រូវតែជាចន្លោះរវាង  %d និង %d។\n
25 ពេលវេលាច្រើនបំផុតកំណត់ការបិទinstanceត្រូវតែជាចន្លោះរវាង %d និង %d។\n
26 objectsសរុប: %1$d\n
27 objectsបរាជ័យ: %1$d\n
28 objectsដែលផ្លាស់ប្តូរ: %1$d\n
29 objectsសំខានៗ: %1$d\n
30 តំណាងclassចាស់ ត្រូវបានដកចេញពី catalog។\n
31 តំណាងclassចាស់ ត្រូវបានការពារក្នុង catalog.\n
32 ពុំអាចកំណត់កម្រិតមិនអាចភ្ជាប់បាន។\n
33 មិនអាចបង្ហាប់ដែលតាបេស។\n
34 ពេលវេលាច្រើនបំផុតកំណត់ការបិទclass ត្រូវតែជាចន្លោះរវាង %d និង %d។\n
35 classត្រូវគាំង... ដំណើរការមិនបានពេញលេញ...\n
36 classមិនដំណោះរការ...\n
37 ដំណើរការ error... ដំណើរការមិនបានពេញលេញ...\n
38 ពុំមានអ្វីត្រូវដំណើរការ...\n
39 ប៉ារ៉ាម៉ែត្រអត់សុពល...\n
40 ឈ្មោះclassដែលមិនស្គាល់...\n
41 class OIDs និងទំព័រដែលមិនប្រើត្រូវបានធ្វើកំណែ។\n
42 កំណែនៃ OIDs និងទំព័រត្រូវបានរំលង។\n
43 Error អម្លុងពេលដំណើរការ class, class ត្រូវបានរំលង។\n
44 \nចម្លងទៅកាន់ 3\n\n
45 សំណុំបង្ហាប់ត្រូវបរាជ័យ ...\n
46 សំណុំបង្ហាប់បានជោគជ័យ...\n
60 \
compactdb: ទីចំហរសេរីដែលមិនប្រើក្នុងដែលតាបេស។\n\
usage: %1$s compactdb [OPTION] ឈ្មោះដែលតាបេស [class_name1 class_name2 ...]\n\
\n\
ជម្រើសសុពល:\n\
  -v, --verbose                     បង្កើនសមត្ថភាពសារ verbose status; default: ដែលមិនអាច\n\
  -i, --input-class-file=FILE       បញ្ជូល FILE of នៃឈ្មោះតារាង\n\
                                    តារាងឈ្មោះclass_name ត្រូវតែទទេ។\n\
                                    default: បង្ហាប់classesទាំងអស់\n\
                                    បង្កើនសមត្ថភាពតែក្នុង CS-mode\n\
  -p, --pages-commited-once=NUMBER  ចំនួនអតិបរមានៃទំព័រ ត្រូវប្រតិបត្តិម្តង។\n\
                                    default: 10\n\
                                    បង្កើនសមត្ថភាពតែក្នុង CS-mode\n\
  -S, --SA-mode                     ការប្រតិបត្តិstand-alone mode \n\
  -C, --CS-mode                     ការប្រតិបត្តិclient-server mode \n\
  -d, --delete-old-repr             ដកចេញតំណាង classចាស់ពី catalog; default: ដែលមិអាច\n\
  class_name_list                    បង្កើនសមត្ថភាពតែក្នុង CS-mode\n\
  -I, --Instance-lock-timeout       ពេលវេលាច្រើនបំផុតសម្រាប់កំណត់ការបិទ instance \n\
  -c, --class-lock-timeout          ពេលវេលាច្រើនបំផុតសម្រាប់កំណត់ការបិទ class \n

$set 15 MSGCAT_UTIL_SET_COMMDB
21 The CUBRID master កំពុងដំណើរការនៅលើ host %s \n បានចាប់ផ្តើមត្រង់ %s
22 បានបម្រើតម្រូវការ %d ។ %d server(s) កំពុងដំណើរការ។ \n
23 បិទក្នុងនាទី %1$s \n
24 %1$s
25 ឈ្មោះ server ត្រូវបិទ :
26 តើប៉ុន្មាននាទីរហូតដល់បិទ? :
27 \
cub_commdb: utilityនេះពុំមានតម្លៃ។ ប្រើ 'cubrid' command ជាជំនួស។ \n\
usage: cub_commdb [OPTION]\n\
\n\
ជម្រើសសុពល:\n\
  -P, --server-list                ព្រីន/ថតចម្លងដំណើរការ server \n\
  -O, --all-list                   ព្រីន/ថតចម្លងដំណើរការទាំងអស់\n\
  -S, --shutdown-server=NAME       បិទNAME​ serverដែលតាបេស\n\
  -A, --shutdown-all               បិទដំណើរការទាំងអស់\n\
  -c, --server-mode=NAME           ព្រីន/ថតចម្លង HA mode​ របស់ NAME serverដែលតាបេស \n\
  -N, --node-list                  រៀបតារាង nodes ដែលរៀបចំគ្រប់គ្រងដោយ CUBRID heartbeat\n\
  -L, --process-list               រៀបតារាងដំណើរការlocal ដែលរៀបចំគ្រប់គ្រងដោយ CUBRID heartbeat\n\
  -p, --ping-host                  list ping hosts used for CUBRID heartbeat\n\
  -D, --dereg-process=PID          បិទដំណើរការជាគោលដៅ​ និងផ្លាស់វាចេញពីរបៀបចូលដំណើរការដែលបានរៀបចំគ្រប់គ្រងរួច\n\
  -F, --reconfig-node-list         ដាក់ដំណើរការCUBRID heartbeat configurationដែលត្រូវបានផ្លាស់ប្តូរថ្មីៗ\n\
  -V, --verbose                    បង្កើនសមត្ថភាពសារ verbose status ; default: ដែលមិនអាច\n
28 សុំអធ្យាស្រ័យ, commandដែលមិនស្គាល់: %1$s\n
29 មិនអាចទទួលយកឈ្មោះ host សម្រាប់local host - ទម្លាក់ចោល\n
30 មិនអាចស្វែងរក fileប៉ារ៉ាម៉ែត្រ \n
31 មិនអាចភ្ជាប់ទៅនឹង master server នៅលើ %1$s\n
32 តើប៉ុន្មាននាទីរហូតដល់បិទ? :
33 Master នឹងបិទក្នុងនាទី in %1$d \n
34 Error ទទួលទិន្នន័យពី client
35 ដែលតាបេស server %1$s ថ្មីៗមិនដំណើរការទេ។
36 មិនអាចប្រតិបត្តិ shell (%s) សម្រាប់ %s, %s
37 ដំណើរការ fork %s, %d ត្រូវត្រឡប់, %s
38 មិនអាចប្រតិបត្តិ %1$s
39 -i(--immediately) option must be used with --deact-stop-all option.\n

$set 16 MSGCAT_UTIL_SET_PATCHDB
60 \
emergency_patchlog: ភ្ជាប់ដែលតាបេសមួយក្នុងករណីបន្ទាន់។ \n\
usage: %1$s emergency_patchlog [OPTION] ឈ្មោះដែលតាបេស\n\
\n\
ជម្រើសសុពល:\n\
  -r, --recreate-log           បង្កើតឡើងវិញនូវ log files; ប្រើដោយប្រុងប្រយ័ត្ត\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 តម្លៃខុស %1$d សម្រាប់ npages\n
21 តម្លៃខុស '%1$s' សម្រាប់បំណងរក្សាទុក volume ។ \n
22 ព្រមមាន: --max-writesize-in-sec ប្រការនេះមិនអនុវត្តក្រោមទំរង់ឯករាជ្យទេ.\n
60 \
addvoldb: បន្ថែម volume-fileមួយសម្រាប់ដែលតាបេសនេះ។ \n\
usage: %1$s addvoldb [OPTION] ឈ្មោះដែលតាបេស \n\
\n\
ជម្រើសសុពល:\n\
      --db-volume-size=SIZE        ទំហំនៃvolumeបន្ថែម; default: db_volume_size ក្នុង cubrid.conf\n\
      --max-writesize-in-sec=SIZE  the amount of volume written per second; (ex. 512K, 1M, 10M); default: not used; minimum: 160K\n\
  -n, --volume-name=NAME           ឈ្មោះនៃ volumeពត៌មាន; default: បង្កើត "db"_ext1)បែបនេះ \n\
  -F, --file-path=PATH             PATH សម្រាប់បន្ថែម volume file; default: បញ្ជីឈ្មោះការងារ\n\
      --comment=COMMENT            COMMENT សម្រាប់បន្ថែម volume file; default: none\n\
  -p, --purpose=PURPOSE            PURPOSE សម្រាប់បន្ថែម volume file; ត្រូវបានអនុញ្ញាត:\n\
                                           DATA - សម្រាប់តែទិន្នន័យ\n\
                                           INDEX - សម្រាប់តែ indices\n\
                                           TEMP - សម្រាប់តែភាពបណ្តោះអាសន្ន\n\
                                           GENERIC - សម្រាប់បំណងទាំងអស់\n\
  -S, --SA-mode                    ការប្រតិបត្តិ stand-alone mode \n\
  -C, --CS-mode                    ការប្រតិបត្តិ client-server mode \n

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 ភាពមិនជាប់លាប់មួយចំនួយត្រូវបានរកឃើញក្នុងដែលតាបេសរបស់អ្នក។ \n សូមពិគ្រោះជាមួយ error_log_file %1$s សម្រាប់ពត៌មានបន្ថែម។​ \n
21 %1$s: មិនអាចរកឃើញ classនេះ។ វានឹងត្រូវប្រតិបត្តិ។ \n
22 %1$s: Cannot find the index on class %2$s.\n
60 \
checkdb: ពិនិត្យមើលភាពជាប់លាប់នៃដែលតាបេស។ \n\
usage: %1$s checkdb [OPTION] ឈ្មោះដែលតាបេស [class_name1 class_name2 ...]\n\
\n\
ជម្រើសសុពល:\n\
  -S, --SA-mode                ការប្រតិបត្តិstand-alone mode \n\
  -C, --CS-mode                ការប្រតិបត្តិclient-server mode \n\
  -r, --repair                 ជួសជុលនៅពេលភាពជាប់លាប់ត្រូវបានរកឃើញ។ \n\
  -i, --input-class-file=FILE  ដាកបញ្ជូល FILE នៃឈ្មោះ class \n\
                               default: ពិនិត្យមើលclassesទាំងអស់\n\
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
15 ការពិពណ៌នាអំពីទីទំនេរសម្រាប់ដែលតាបេស  '%s' ជាមួយទំហំទំព័រ %s. ( ទំហំទំព័រlog : %s)\n\n
16 %5d %9s  %s %s  %s\n
17 មិនអាចបើក output file `%1$s'\n
18 ការពិពណ៌នាអំពីទីទំនេរសម្រាប់volumesបណ្តោះអាសន្ន សម្រាប់ដែលតាបេស '%s' ជាមួយទំហំទំព័រ %s។ \n\n
19 បំណងសុពល  total_pages  free_pages  ឈ្មោះVol \n\n
20 បំណងសុពល   total_size   free_size  ឈ្មោះVol \n\n
21 ការពិពណ៌នាអំពីទីទំនេរLOB  %1$s \n\n
22 -------------------------------------------------------------------------------\n
30 ការពិពណ៌នាអំពីទីទំនេរដែលត្រូវបានសង្ខេបសម្រាប់ ដែលតាបេស '%s' ជាមួយទំងំទំព័រ %s. ( ទំហំទំព័រlog : %s)\n\n
31 បំណង total_pages  used_pages  free_pages  volume_count\n
32 បំណង total_size   used_size   free_size  volume_count\n
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
spacedb: បង្ហាញ usage/ទីទំនេរសេរីនៃដែលតាបេស ។ \n\
usage: %1$s spacedb [OPTION] ឈ្មោះដែលតាបេស \n\
\n\
ជម្រើសសុពល:\n\
  -o, --output-file=FILE       ប្តូរទិសសារ output ទៅកាន់ FILE; default: none\n\
  -S, --SA-mode                ការប្រតិបត្តិ stand-alone mode \n\
  -C, --CS-mode                ការប្រតិបត្តិ client-server mode \n\
      --size-unit={PAGE|M|G|T|H}\n\
                               set the display unit (page, mega, giga, tera, human readable); default: H\n\
  -s, --summarize              បង្ហាញតែសេចក្តីសង្ខេបមួយសម្រាប់បំណង volumeនីមួយៗ \n\
  -p, --purpose                បង្ហាញនូវបំណងនៃទំហំដែលបានប្រើប្រាស់រួចហើយ(data/index/temp) ក្នុងវ៉ូលូមនិមួយៗ\n


$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 ចំនួនប៉ាន់ប្រមាណនៃទំព័រ  = %1$d\n
60 \
estimate_data: ប៉ាន់ប្រមាណទំហំទិន្នន័យ។ \n\
usage: %1$s estimate_data ចំនួននៃinstances ទំហំមធ្យមនៃinstance ចំនួននៃattributes ចំនួនអថេរនៃattributes\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 ប្រភេទindex keដែលអន់ ត្រូវបានកំណត់ជាពិសេស  = %1$s\n
16 ប្រវែងaverage keyដែលអន់ ត្រូវបានកំណត់ជាពិសេស = %d នៅពេលដែលភាពជាក់លាក់ = %d\n
17 សេចក្តីសំអាងខ្ទាស់ដែលអន់ ត្រូវបានកំណត់ជាពិសេស\n
20 ចំនួនប៉ាន់ប្រមាណនៃទំព័រ index សរុប = %1$d\n (នៅពេល index ត្រូវបានបង្កើត, បន្ទាប់ពី instances ត្រូវបានស្វ័យបន្ថែមពត៌មានក្នុងតារាង៕)\n
21 ចំនួនប៉ាន់ប្រមាណនៃទំព័រ index សរុប = %1$d\n (នៅពេល index ត្រូវបានបង្កើត, បន្ទាប់ពី instances ត្រូវបានស្វ័យបន្ថែមពត៌មានក្នុងតារាង (ករណីមធ្យម))\n
22 ចំនួនប៉ាន់ប្រមាណនៃទំព័រ index សរុប = %1$d\n (នៅពេល index ត្រូវបានបង្កើត, បន្ទាប់ពី instances ត្រូវបានស្វ័យបន្ថែមពត៌មានក្នុងតារាង (ករណីអន់បំផុត))\n
23 សម្រាប់ការបញ្ជូលដែលផ្តល់ឲ្យ...\n
24 ចំនួនសរុបនៃ instances: %1$d\n
25 ចំនួននៃ keysខុសៗគ្នា : %1$d\n
26 ទំហំ key មធ្យម   : %1$d\n
27 ប្រភេទKey                 : %1$s\n\n
60 \
estimate_index: ប៉ាន់ប្រមាណទំហំនៃ indexមួយ។ \n\
usage: %1$s estimate_index ចំនួននៃinstances ចំនួននៃkeysខុសៗគ្នា  ទំហំមធ្យមនៃkey ប្រភេទkey\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: តម្លើងដែលតាបេសមួយ\n\
usage: %1$s installdb [OPTION] ឈ្មោះដែលតាបេស\n\
\n\
ជម្រើសសុពល:\n\
      --server-name=HOST       ឈ្មោះserver HOST ; default: ឈ្មោះhostបច្ចុប្បនុ្ន \n\
  -F, --file-path=PATH         បញ្ជីឈ្មោះPATH សម្រាប់volumesដែលតាបេស; default: បញ្ជីឈ្មោះបច្ចុប្បនុ្ន \n\
  -L, --log-path=PATH          បញ្ជីឈ្មោះPATH សម្រាប់log volumes; default: ដូចគ្នានឹង file-path\n

$set 23 MSGCAT_UTIL_SET_MIGDB
1 class OID​បណ្តោះអាសន្ន\n
2 មិនអាចព្រីន/ថតចម្លង elo values\n
3 Error ដែលចូលទៅដល់ objectវែង សម្រាប់ការពាក់ព័ន្ធដែលប្រើដោយដែលតាបេស \n
4 Error ដែលបើក output file សម្រាប់ការពាក់ព័ន្ធដែលប្រើដោយobjectវែងនេះ \n
5 អាន error\n
6 សរសេរ error\n
7 Error ដែលបើក ELO input file "%1$s"\n
8 CLASSES:
9 hash filename
10 ឈ្មោះ:                       %1$s\n
11 ទំហំ:                          %1$d\n
12 ទំហំទំព័រ:                      %1$d\n
13 ទំហំទិន្នន័៍យ:                 %1$d\n
14 ទំហំEntry:                     %1$d\n
15 Entries ក្នុង១ទំព័រ:            %1$d\n
16 ទំព័រលាក់កំបាំង:               %1$d\n
17 ចំនួននៃentries:               %1$d\n
18 ចំនួននៃcollisions:            %1$d\n
19 Hash filename:                %1$s\n
20 overflow entryបន្ទាប់:        %1$d\n
21 ន្រភេទKey:                     %1$s\n
22 អាសយដ្ឋាននៃheadersទំព័រ:      %1$x\n
23 អាសយដ្ឋាននៃheaderទំព័រចុងក្រោយ: %1$x\n
24 អាសយដ្ឋាននៃheaderទំព័រសេរី:      %1$x\n
25 អាសយដ្ឋាននៃbitmapទំព័រ:          %1$x\n
26 ទំហំbitmapទំព័រ:                  %1$d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
60 \
diagdb: Dump ដែលតាបេស\n\
usage: %1$s diagdb [OPTION] ឈ្មោះដែលតាបេស\n\
\n\
valid options:\n\
  -d, --dump-type=TYPE         ប្រភេទនៃ dump\n\
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
15 មិនអាចបើក output file `%1$s'\n
59 lockdb មិនអាចដំណើរការជា standalone mode។ \n
60 \
lockdb: បង្ហាញ lock-status នៃដែលតាបេស។ \n\
usage: %1$s lockdb [OPTION] ឈ្មោះដែលតាបេស \n\
\n\
ជម្រើសសុពល\n\
  -o, --output-file=FILE   បន្តូរទិសសារ output ទៅកាន់FILE; default: none\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 សេចក្តីសំអាងខ្ទាស់តែមួយគត់ ត្រូវបានផ្តល់ឲ្យ: tran-index, user, host, progname\n
21 វាយបញ្ជូលកំណត់សម្ងាត់ DBA :
26 ពុំមានកិច្ចសម្រួលដំណើរការជាមួយ argument valuesដែលត្រូវបានកំណត់ពិសេសទេ។ \n
27 ត្រៀមរួចជាស្រេចដើម្បីបំផ្លាញកិច្ចសម្រួលដំណើរការជាបន្តរបន្ទាប់:\n\n
28 តើអ្នកចង់ធ្វើប្រតិបត្តិការទេ ? (Y/N)
29 ការបំផ្លាញកិច្ចសម្រួលដំណើរការដែលទាក់ទងនឹងindexនៃកិច្ចសម្រួលដំណើរការ %1$d\n
30 ការបំផ្លាញកិច្ចសម្រួលដំណើរការជាបន្តរបន្ទាប់ត្រូវបរាជ័យ។ \n\n
31 កិច្ចសម្រួលដំណើរការនេះ មិនទាន់ចប់ទេ, ប៉ុន្តែវាអាចបន្តដំណើរការត្រឡប់វិញ។ \n សូមរងចាំសិននិងព្យាយាមនៅពេលក្រោយបើចាំបាច់។ \n
32 អត់មានប្រសិទ្ធិភាព tranindex(%1$s).\n
59 killtran មិនអាចដំណើរការជា standalone mode។ \n
60 \
killtran: គ្រប់គ្រងកិច្ចសម្រួលដំណើរការនៃដែលតាបេស។ \n\
usage: %1$s killtran [OPTION] ឈ្មោះដែលតាបេស\n\
\n\
ជម្រើសសុពល:\n\
  -i, --kill-transaction-index=INDEX បំផ្លាញកិច្ចសម្រួលដំណើរការ ជាមួយនឹងINDEXនៃកិច្ចសម្រួលដំណើរការ \n\
      --kill-user-name=ID            បំផ្លាញកិច្ចសម្រួលដំណើរការទាំងអស់ ជាមួយនឹងuser ID\n\
      --kill-host-name=HOST          បំផ្លាញកិច្ចសម្រួលដំណើរការទាំងអស់ ជាមួយនឹងclient HOST\n\
      --kill-program-name=NAME       បំផ្លាញកិច្ចសម្រួលដំណើរការទាំងអស់ ជាមួយនឹងclient program NAME\n\
      --kill-sql-id=SQL_ID           លុបចោលការជួញដូរទាំងអស់ដោយសារ SQL_ID\n\
  -p, --dba-password=PASS            កំណត់សម្គាល់សម្ងាត់នៃDBA user; នឹងកើតមានប្រសិនបើ មិនជាក់លាក់\n\
  -d, --display-information          បង្ហាញពត៌មានអំពីកិច្ចសម្រួលដំណើរការសកម្ម \n\
  -q, --query-exec-info              បង្ហាញនូវព័ត៌មានឆែកឆេរការជួញដូរដែលកំពុងធ្វើ\n\
  -f, --force                        បំផ្លាញកិច្ចសម្រួលដំណើរការជាមួយនឹងការចាំបាច់សម្រាប់ការពិនិត្យបញ្ជាក់\n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1 ឈ្មោះដែលតាបេស
2 សេចក្តីសំអាងខ្ទាស់ដែលតម្រូវ, គ្មានdefault
3 ការពិនិត្យករណីដែលមាន
4 ការពិនិត្យតែករណីដែលមាន
5 គោលបំណងbackup
6 path ឬ deviceនៃបញ្ជីឈ្មោះមួយសម្រាប់គោលបំណងនៃ backup volumes
20 ដែលតាបេស '%1$s'ពុំមានទេ។
21 '%s' មិនអាចផ្លាស់ទៅកាន់ %s, ពីព្រោះ DB versionរបស់វាទាបពេក។ \n
22 '%1$s' មិនត្រូវការបម្លាស់។ \n
23 '%1$s' ត្រូវការបម្លាស់។ \n
24 Backupត្រូវបានបញ្ជាក់បន្ទាប់ពីបម្លាស់។  តើអ្នកចង់ចម្លងពត៌មានដែលតាបេសបន្ថែមឥឡូវនេះទេ? (y/n):
25 យើងផ្តល់អនុសាសដើម្បីចម្លងពត៌មានដែលតាបេសបន្ថែមជាមួយនឹង backupdb commandបន្ទាប់ពីបម្លាស់។
26 \n*** WARNING: ចម្លងពត៌មានដែលតាបេសរបស់អ្នកបន្ថែម មុនពេលអ្នកចាប់ផ្តើមផ្លាស់ទៅកាន់%1$s ***\n\n ដើម្បីបិទerrorsដែលអាចកើតឡើងអម្លុងពេលបម្លាស់, យើងត្រូវផ្តល់អនុសាសដើម្បីចម្លងពត៌មានដែលតាបេសបន្ថែម។ \nតើអ្នកបានចម្លងពត៌មានដែលតាបេសរបស់អ្នកបន្ថែមហើយឬនៅ? (y/n):
27 \n1. ដំណើរការ backupdb \n2. វាយបញ្ជូល backup pathថ្មីសម្រាប់ដែលតាបេសថ្មី។(pathបច្ចុប្បន្ន: %1$s)\n3. Exit \n
28 \nThe backup pathថ្មីសម្រាប់ដែលតាបេសនេះ :
29 \nបម្លាស់និង backupត្រូវបានបញ្ចប់ដោយជោគជ័យ។
30 Backupបរាជ័យ។ សូមព្យាយាមម្តងទៀតដើម្បីចម្លងពត៌មានដែលតាបេសបន្ថែម។
31 \n\n*** WARNING : នៅពេលបម្លាស់ត្រូវបានបំពេញ, អ្នកមិនអាចផ្ទុកដែលតាបេសជាមួយនឹង backup volumesចុងក្រោយបានទេ។\nចាំថា versionមុនៗនៃ backup files អាចត្រូវរក្សាផ្ទុកតែលើversionត្រូវគ្នានៃrestoredb.\n សូមពិនិត្យមើលbackup volumesចុងក្រោយដែលត្រូវបានរក្សាទុកបច្ចុប្បន្ន។\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: ផ្លាស់ប្តូរhostនៃដែលតាបេស។ \n\
usage: %1$s alterdbhost [OPTION] ឈ្មោះដែលតាបេស \n\
\n\
ជម្រើសសុពល:\n\
  -h, --host=HOST  ឈ្មោះHOSTថ្មី; default: ឈ្មោះ hostបច្ចុប្បន្ន \n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 ឈ្មោះដែលតាបេស
2 សេចក្តីសំអាងខ្ទាស់តម្រូវ, គ្មានdefault
3 java-class-file
4 សេចក្តីសំអាងខ្ទាស់តម្រូវ, គ្មានdefault
5 សរសេរជាន់លើforce
6 សរសេរជាន់លើclass file បើមាន, គ្មានdefault 

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 មិនអាចបើក output file `%1$s'\n
59 plandump មិនអាចដំណើរការជា standalone mode.\n
60 \
plandump: បង្ហាញឬគ្រប់គ្រង query plansលាក់កំបាំង។ \n\
usage: %1$s plandump [OPTION] ឈ្មោះដែលតាបេស \n\
\n\
ជម្រើសសុពល:\n\
  -d, --drop                   ទម្លា់ចោលplansទាំងអស់ក្នុងទីកំបាំងរបស់server \n\
  -o, --output-file=FILE       ប្តូរទិសសារoutput ទៅកាន់ FILE; default: none\n

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 មិនអាចបើកoutput file `%1$s'\n
21 ប៉ារ៉ាម៉ែត្រប្រព័ន្ធនឹងត្រូវបានប្រើដោយកម្មវិធី client។ \n
22 ប៉ារ៉ាម៉ែត្រប្រព័ន្ធត្រូរក្សាផ្ទុកដោយ server '%1$s'\n
23 ប៉ារ៉ាម៉ែត្រប្រព័ន្ធនឹងត្រូវប្រើនៅក្នុង standalone mode។ \n
60 \
paramdump: លែងកំណត់ប៉ារ៉ាម៉ែត្រប្រព័ន្ធ.\n\
usage: %1$s paramdump [OPTION] ឈ្មោះដែលតាបេស \n\
\n\
ជម្រើសសុពល:\n\
  -o, --output-file=FILE       ប្តូរទិសសារ output ទៅកាន់ FILE; default: none\n\
  -b, --both                   ទម្លាក់ចោលទាំង client ទាំងប៉ារ៉ាម៉ែត្រ server \n\
  -S, --SA-mode                ការប្រតិបត្តិstand-alone mode \n\
  -C, --CS-mode                ការប្រតិបត្តិclient-server mode \n

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 តម្លៃខុស \"%1$s\" សម្រាប់HA modeដែលកំពុងដំណើរការរបស់ server។ \n
12 មិនអាចផ្លាស់ប្តូរបានទេនូវ HA modeដែលកំពុងដំណើរការរបស់ server។ \n
21 វាយបញ្ជូលកំណត់សម្ងាត់ DBA :
22 HA modeដែលកំពុងដំណើរការរបស់ server `%s' គឺ %s។ \n
23 HA modeដែលកំពុងដំណើរការរបស់ server `%s' ត្រូវបានផ្លាស់ប្តូរទៅកាន់  \"%s\"។ \n
24 serverនេះមិនទាន់ត្រូវបានកំណត់ទៅកាន់ HA modeទេ។ \n
58 HA feature មិនទាន់ត្រូវបានគាំទ្រនៅក្នុងទម្រង់ការនេះទេ។ \n
59 changemode មិនអាចដំណើរការជា standalone modeទេ។ \n
60 \
changemode: បង្ហាញឬផ្លាស់ប្តូរHA modeដែលកំពុងដំណើរការរបស់ server។ \n\
usage: %1$s changemode [OPTION] ឈ្មោះដែលតាបេស​ \n\
\n\
ជម្រើសសុពល:\n\
  -m, --mode=MODE             ផ្លាស់ប្តូរserver mode ទៅកាន់ MODE; default: none\n\
  -f, --force                 force ត្រូវផ្លាស់ប្តូរ\n\
  -t, --timeout=S             រង់ចាំវិនាទី S second នៅពេលកិច្ចសម្រួលដំណើរការត្រូវបានបញ្ចប់ជាធម្មតា។ \n\
                              ជម្រើស-t មានប្រសិទិ្ធភាពតែនៅពេល userផ្លាស់ប្តូរserver mode ទៅកាន់maintenance \n\
                              default: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 តម្លៃខុស \"%1$s\" សម្រាប់modeកំពុងដំណើរការរបស់ copylogdb។ \n
21 វាយបញ្ជូលកំណត់សម្ងាត់ DBA :
22 serverនេះ ត្រូវបានកំណត់ទៅកាន់ HA mode។ \n
58 HA feature មិនត្រូវបានគាំទ្រនៅក្នុងទម្រង់ការនេះទេ។ \n
59 copylogdb មិនអាចដំណើរការជា standalone modeទេ។ \n
60 \
copylogdb: ថតចម្លងទំព័រ log ពី server។ \n\
usage: %1$s copylogdb [OPTION] ឈ្មោះដែលតាបេស \n\
\n\
ជម្រើសសុពល:\n\
  -L, --log-path=PATH         PATHបញ្ជីឈ្មោះសម្រាប់log volumesដែលត្រូវបានថតចម្លងរួច។ \n\
  -m, --mode=MODE             ប្រតិបត្តិការmode; default: sync\n\
                              MODE: sync ឬ semisync ឬ async\n

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 វាយបញ្ជូលកំណត់សម្ងាត់ DBA:
22 serverនេះ ត្រូវបានកំណត់ទៅកាន់ HA mode។ \n
58 HA feature មិនត្រូវបានគាំទ្រនៅក្នុងទម្រង់ការនេះទេ។ \n
59 copylogdb មិនអាចដំណើរការជា standalone modeទេ។ \n
60 \
applylogdb: ដាក់ដំណើរការlogដែលត្រូវបានថតចម្លងរួចទៅកាន់ server។ \n\
usage: %1$s applylogdb [OPTION] ឈ្មោះដែលតាបេស \n\
\n\
ជម្រើសសុពល:\n\
  -L, --log-path=PATH         PATHបញ្ជីឈ្មោះនៃlog volumesដែលត្រូវបានថតចម្លងរួច \n\
      --max-mem-size=SIZE     ទំហំដំណើរការអតិបរមាក្នុង MB; max 1000\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 មិនអាចបើកoutput file `%1$s'\n
59 statdump មិនអាចដំណើរការជា standalone modeទេ។ \n
60 \
statdump: ទម្លាក់ចោលស្ថិតិសកល។ \n\
usage: %1$s statdump [OPTION] ឈ្មោះដែលតាបេស \n\
\n\
ជម្រើសសុពល:\n\
  -o, --output-file=FILE       ប្តូរទិសសារ output ទៅកាន់ FILE; default: none\n\
  -c, --cumulative             ព្រីន/ថតចម្លង cumulative stats\n\
  -i, --interval=S             ព្រីន/ថតចម្លងនូវរាល់stats S secs\n\
  -s, --substr=STRING          ព្រីន/ថតចម្លង stat item ដែលត្រូវគ្នានឹង substring\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 វាយបញ្ជូលកំណត់សម្ងាត់ DBA:
22 serverនេះ ត្រូវបានកំណត់ទៅកាន់ HA mode។ \n
58 HA feature មិនត្រូវបានគាំទ្រនៅក្នុងទម្រង់ការនេះទេ។ \n
59 applyinfo មិនអាចដំណើរការជា standalone modeទេ។ \n
60 \
applyinfo: បង្ហាញពត៌មានដាកដំណើរការ CUBRID HA Apply។ \n\
usage: %1$s applyinfo [OPTION] ឈ្មោះដែលតាបេស \n\
\n\
ជម្រើសសុពល:\n\
  -r, --remote-host-name      ឈ្មោះremote host; បង្ហាញពត៌មានlogសកម្មរបស់ remote node។ \n\
  -a, --applied-info 	                 បង្ហាញពត៌មានសម្រេច \n\
  -L, --copied-log-path=PATH  path នៃlog volumesដែលថតចម្លងរួច; បង្ហាញពត៌មានlogដែលថតចម្លងរួច \n\
  -p, --page=ID               ទំព័រ id; default : 0\n\
  -v, --verbose               បង្កើនសមត្ថភាពសារ verbose status ; default : មិនអាច\n\
  -i, --interval=S            បោះពុម្ពព័ត៌មានអំពី S secs\n

$set 45 MSGCAT_UTIL_SET_ACLDB
59 acldb មិនអាចដំណើរការជា standalone modeទេ។ \n
60 \
acldb: បង្ហាញ ឬរក្សាផ្ទុកការកំណត់ការគ្រប់គ្រងសិទ្ធិចូលនៃដែលតាបេស។ \n\
usage: %1$s acldb [OPTION] ឈ្មោះដែលតាបេស\n\
\n\
ជម្រើសសុពល:\n\
  -r, --reload       រក្សាផ្ទុកfileគ្រប់គ្រង ip \n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
genlocale::បណ្ណាល័យដែលចែកចាយទីកន្លែងសំរាប់បង្កើត បណ្ណាល័យនេះប្រើLDMLបញ្ចូលសំណុំឯកសារ(s).\n\
ទំលាប់ប្រើ: %1$s genlocale [ជម្រើស] [ភាសា-ខ្សែអក្សរ]\n\
\n\
ភាសា-ខ្សែអក្សរ      ខ្សែអក្សរភាសាទីតាំង សំរាប់បង្កើតបណ្ណាល័យដែលត្រូវចែកចាយ\n\
                     ប្រសិនបើមិនអោយខ្សែអក្សរភាសា\n\
		     'cubrid_locales.txt' cubrid_នឹងត្រូវកែច្នៃ\n\
ជម្រើសមានប្រសិទ្ធិភាព:\n\
  -v, --verbose	              អាចប្រព្រឺត្តពត៍មានភាពយឺតយូរ កំហុស មិនបានទេ\n\
  -i, --input-ldml-file       បញ្ចូលផ្លូវនៃ សំណុំឯកសារLDML ពេលអោយខ្សែអក្សរភាសា.\n
61 ការពិភាក្សាអំពីឈ្មោះទីកន្លែងដែលគ្មានប្រសិទ្ធិភាព.\n

$set 47 MSGCAT_UTIL_SET_DUMPLOCALE
57 មិនអាចប្រើនៅពេលព្រមៗគ្នា -i/--input-file រសំគាស់អត្តសញ្ញាសំណុំឯកសារនិងទីកន្លែង\n
58 ក្របខ័ណ្ខអក្សរកូដទីតាំងមោឃ(-s -e).\n
59 ការពិភាក្សាអំពីឈ្មោះទីកន្លែងដែលគ្មានប្រសិទ្ធិភាព.\n
60 \
dumplocale:បោះចោលខ្លឹមសារនៃបណ្ណល័យដែលចែកចាយទីតាំង ប្រើLDMLបញ្ចូលសំណុំឯកសារបង្កើតបញ្ហានេះពេលមុន\n\
ទំលាប់ប្រើ: %1$s dumplocale [ជម្រើស] [ភាសា-ខ្សែអក្សរ]\n\
\n\
language-string ខ្សែអក្សរភាសាទីតាំង បោះចោលបណ្ណល័យដែលចែកចាយទីតាំង\n\
                 មនុស្សមិនអោយខ្សែអក្សរភាសា  'cubrid_databases.txt' \n\
		 ត្រូវកែច្នៃ\n\
ជម្រើសមានប្រសិទ្ធិភាព:\n\
  -i, --input-file ណុំឯកសារ បញ្ចូលផ្លូវនែបណ្ណល័យដែលចែកចាយទីតាំង(ត្រូវបង្កើតពេលមុន).\n\
                   គ្រាន់តែចំពោះពេលខ្សែអក្សរភាសាមិនទាន់ចេញ\n\
  -d, --calendar      បោះចោល ប្រតិទិននិងកាលបរិច្ឆេទ/ពត៍មានពេលវេលា កំហុស មិនបាទទេ\n\
  -n, --numeric		  បោះចោលពត៍មានលេខ កំហុស មិនបាទទេ\n\
  -a, --alphabet=l|lower|u|upper|both	     បោះចោល អក្ខរក្រម កើតពត៍មាន កំហុស មិនបាទទេ\n\
      --identifier-alphabet=l|lower|u|upper	 បោះចោល អក្ខរក្រម កើតពត៍មានឧបករសំគាល់អត្តសញ្ញា កំហុស មិនបាទទេ\n\
  -c, --codepoint-order	 បោះចោលការពិនិត្យដែលនៅលើមូលដ្ឋានតំលៃ ទីតាំងអក្សរកូដ\n\
                         (cp, char,ទម្ងន់ បន្ទាប់-cp, charនិងទម្ងន់);\n\
                         កំហុស មិនបាទទេ\n\
  -w, --weight-order  បោះចោលការពិនិត្យដែលនៅលើមូលដ្ឋានទម្ងន់\n\
                      (ទម្ងន់, cp, char); កំហុស មិនបាទទេ\n\
  -s, --start-value	  ជម្រើសឧបករត្រង បើក ទីតាំងអក្សរកូដនៃ\n\
                      -a, --identifier-alphabet, -c, -w;កំហុស: 0\n\
  -e, --end-value     ជម្រើសឧបករត្រង បញ្ចប់ទីតាំងអក្សរកូដនៃ\n\
                      -a, --identifier-alphabet, -c, -w; \n\
                      កំហុស:អានតំលៃអតិបរិមាណនៃលេខដែលចែកចាយទីតាំង\n\
  -k, --console-conversion  ចោលលេខដែលត្រូវបំលែងដោយទូបញ្ជា កំហុស មិនបាទទេ\n\
  -z, --normalization   បោះចោលលេខដែលធ្វើអោយទៅជាធម្មតា កំហុស មិនបាទទេ\n

$set 48 MSGCAT_UTIL_SET_SYNCCOLLDB
43 ការបែងចែកថាសត្រូវលុបចោលមុនពេលកែប្រែលក្ខខ័ណ្ឌនៃការរៀចំទិន្នន័យដែលមាន.\nClass name\n
44 លក្ខខ័ណ្ឌនៃពាក្យសំខាន់ដែលរៀបចំដោយ'%1$s' ត្រូវលុបចោលមុនពេលកែលក្ខខ័ណ្ឌនៃការរៀបចំ.\nTable | Foreign Key\n
45 ប្រភេទដែលរៀបចំដោយ'%1$s'; ត្រូវកំណត់ជាថ្មីទៀត :\nClass name\n
46 ការស្វែងរកពាក្យសំខាន់ដែលបានប្រើប្រាស់និងរៀបចំដោយ'%1$s'; ត្រូវកំណត់ជាថ្មីទៀត :\n\
Table | Index | Function expression\n
47 បើការរៀបចំទិន្នន័យមិនស៊ីគ្នានោះទិន្នន័យមិនអាចកែប្រែទេ.\n
48 ខ្លឹមសារអំពី '%1$s'នៅក្នុងតារាងប្រព័ន្ធអាចប្តូរថ្មីជាមួយការរៀបចំទិន្នន័យដោយប្រព័ន្ធថ្មី.\n
49 ផ្តល់នូវការរៀបចំទិន្នន័យថ្មីបំផុតនៅក្នុងប្រព័ន្ធប្រហាក់ប្រហែលខូចទិន្នន័យ. បន្ត (y/n) ?\n
50 ការរៀបចំដែលមាន '%s' (Id: %d) មិនអាចរកឃើញបាននៅក្នុងទិន្នន័យឬមានការប្រែប្រួលតាមទំរង់ប្រព័ន្ធថ្មី.\n
51 លក្ខខ័ណ្ឌនៃការរៀបចំដែលមាន '%1$s'; ត្រូវកំណត់ជាថ្មីទៀត :\nTrigger | Condition\n
52 នៅក្នុងដំណើរការឆែកឆេរទិន្នន័យដែលមាន '%1$s'; ការឆែកឆេរត្រូវកំណត់ជាថ្មីទៀត :\nView | Query\n
53 លក្ខខ័ណ្ឌនៃការរៀបំចំដែលមាន '%1$s'; ត្រូវកំណត់ជាថ្មីទៀត.\nTable | Attribute Domain\n
54 ការរៀបចំទិន្និន័យដោយមនុស្សគឺចាំបាច់.\n\
សូមមើលមើល៍ឯកសារ'%1$s'រួចហើយអនុវត្តបានមុនពេលរៀបចំទិន្នន័យ.\n
55 ការស៊ីគ្នាមិនបាច់ទេ.\n
56 ការរៀបចំទិន្នន័យដោយប្រព័ន្ធត្រូវចាំបាច់ស៊ីគ្នា.\n\
រត់(ចាប់ដំណើរការ) 'cubrid synccolldb -f %1$s'\n
57 អំពី %1$d មានការរៀបចំថ្មីតាមទំរង់ប្រព័ន្ធចំពោះទិន្នន័យ:\n
58 ការរៀបចំទិន្នន័យទាំងអស់'%1$s' អាចរកឃើញបាននៅក្នុងទំរង់ប្រព័ន្ធថ្មី.\n
59 បើមាន %1$d នៅក្នុងការរៀបចំទិន្នន័យនោះមិនអាចកំណត់ឬមិនអាចកែប្រែទេបើប្រៀបធៀបទៅនឹងការរៀបចំដោយប្រព័ន្ធ.\n
60 \
synccolldb: ការរៀបចំទិន្នន័យត្រូវស៊ីគ្នាជាមួយការរៀបចំដោយប្រព័ន្ធ.\n\
usage: %1$s synccolldb [OPTION] database-name\n\
\n\
ប្រការមានប្រសិទ្ធិភាព:\n\
  -c, --check-only    ត្រួតពិនិត្យការរៀបចំទិន្នន័យតាមទំរង់ការរៀបចំដោយប្រព័ន្ធ.\n\
		      បើការរៀបចំត្រូវការស៊ីគ្នា, កូដកំហុស១ត្រូវត្រឡប់មកវិញ.\n\
		      បង្ហាញនូវរបាយការណ៍អំពីការរៀបចំដែលមិនស៊ីគ្នា.\n\
  -f, --force-only     ផ្តល់នូវការរៀបចំទិន្នន័យថ្មីបំផុតនិងការរៀបចំតាមទំរង់ប្រព័ន្ធថ្មីដូចគ្នា.\n\

$set 49 MSGCAT_UTIL_SET_TRANLIST
20 បញ្ចូល %1$s លេខសំងាត់: 
22 Tran index          User name      Host name      Process id    Program name \n
23 -------------------------------------------------------------------------------\n
24 %4d%-10s %14s %14s %15d %17s\n
25 អត់មានការជួញដូរទេ\n
26 tranlist អាចអនុវត្តតាមតែ DBA ឬសមាជិកក្រុម DBA.\n
27 sort-key ចំនួនវ៉ូលូម(%1$d) ត្រូវចាត់ទុកថា មោឃៈ.\n
32 Tran index         Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
33 %4d%-10s %14d    %-25s %10.2f %12.2f    %28s     %13s   %s\n
34 --------------------------------------------------------------------------------------------------------------------------------------------------\n
42 Tran index          User name      Host name      Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
43 %4d%-10s %14s %14s %14d    %-25s %10.2f %12.2f    %28s     %13s   %s\n
44 ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 Tran index : %1$d
46 SQL_ID: %1$s\n
59 tranlist មិនអាចចាប់ដំណើរការក្រោម standalone mode។ \n
60 \
tranlist: ទាត់ចោលព័ត៌មានអំពីការជួញដូរ.\n\
usage: %1$s tranlist [OPTION] database-name\n\
\n\
ប្រការមានប្រសិទ្ធិភាព:\n\
  -u, --user=ID                ID របស់អ្នកប្រើប្រាស់សំរាប់ឆែកឆេរទិន្នន័យ; default: DBA user\n\
  -p, --password=PASS          លេខសំងាត់នៃអ្នកប្រើប្រាស់; default: none\n\
  -s, --summary                ចេញក្រដាស(បោះពុម្ព)ព័ត៌មានអំពីការជួញដូរ\n\
      --sort-key=COLUMN_NUMBER ចំនួនវ៉ូលូមត្រូវដាក់ទៅតាមប្រភេទ\n\
      --reverse                ដាក់បញ្ច្រាសលទ្ធផលតាមប្រភេទ\n
