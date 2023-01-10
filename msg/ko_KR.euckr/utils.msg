
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

$set 1 MSGCAT_UTIL_SET_GENERIC
1 ũ�Ⱑ �߸��Ǿ��ų�, �߸��� ���� '%1$c'��(��) �����ͺ��̽� �̸� '%2$s'�� ���ԵǾ� �ֽ��ϴ�\n
2 ��� ���� '%1$s'��(��) �� �� �����ϴ�.\n
3 ��ƿ��Ƽ �̸��� ���� �ְų� Ȥ�� �߸� �Ǿ� �ֽ��ϴ�.\n
4 �߸��� ��ƿ��Ƽ �̸� '%1$s'\n
5 ���� ����Ʈ�� �ùٸ� ��ƿ��Ƽ �̸��Դϴ�:\n
6 �߸��� ���� '%1$c'��(��) ���� �̸� '%2$s'�� ���ԵǾ� �ֽ��ϴ�.\n
9 \
%1$s (CUBRID ��ƿ��Ƽ)\n\
%2$s\n\
Copyright (C) 2008 Search Solution Corporation.\nCopyright (C) 2016 CUBRID Corporation.\n
10 \
cubrid ������ ��ƿ��Ƽ, ���� %1$s\n\
����: %2$s <��ƿ��Ƽ �̸�> [�ɼ�]\n\
�� ��ƿ��Ƽ�� ���� ������ ���ϸ� '%3$s <��ƿ��Ƽ �̸�>'  ������ ���� �ϼ���.\n\
\n\
������ ��ƿ��Ƽ �̸�:\n\
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
%4$s (��)�� DBMS ���� �����Դϴ�.\n\
�߰����� ������ ���� ����Ʈ�� �����ϼ���. http://www.cubrid.org\n
12 '%1$s' �� ���� ��ƿ��Ƽ �̸��Դϴ�.\n'cubrid --help' ���ɾ�� ������ �˾ƺ�����.\n
13 '%1$s' �� �߸��� ���ɾ��Դϴ�.\n'cubrid --help' ���ɾ�� ������ �˾ƺ�����.\n
14 ���� ���Ͽ��� �ϳ� �̻��� �ʿ��� �Ӽ��� �о� ���� �� �����ϴ�.\n
15 @ %1$s %2$s: %3$s\n
16 @ %1$s %2$s\n
17 ++ %1$s '%2$s' (��)�� ����ǰ� ���� �ʽ��ϴ�.\n
18 ++ %1$s (��)�� ����ǰ� ���� �ʽ��ϴ�.\n
19 ++ %1$s '%2$s' (��)�� ����ǰ� �ֽ��ϴ�.\n
20 ++ %1$s (��)�� ����ǰ� �ֽ��ϴ�.\n
21 ++ %1$s %2$s: %3$s\n
22 ���ڰ� �����մϴ�. \n'cubrid --help' ���ɾ�� ������ �˾ƺ�����.\n
23 cubrid ��ƿ��Ƽ, ���� %1$s\n\
����: %2$s <��ƿ��Ƽ �̸�> [����]\n\
'%3$s <utility-name>' ���ɾ�� �� ��ƿ��Ƽ�� ���� ������ ���� �� �ֽ��ϴ�.\n\
\n\
������ ���� ��ƿ��Ƽ �̸�:\n\
    service\n\
    server\n\
    broker\n\
    gateway\n\
    manager\n\
    heartbeat\n\
    javasp\n\
\n\
������ ������ ��ƿ��Ƽ �̸�:\n\
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
%4$s (��)�� DBMS  �����Դϴ�.\n\
�߰����� ������ ���� ����Ʈ�� �����ϼ���. http://www.cubrid.org\n
24 cubrid ��ƿ��Ƽ, ���� %1$s\n\
����: %2$s service <���ɾ�>\n\
\n\
������ ���ɾ�:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 cubrid ��ƿ��Ƽ, ���� %1$s\n\
����: %2$s server <���ɾ�> [����]\n\
\n\
������ ���ɾ�:\n\
    start       [�����ͺ��̽� �̸�]\n\
    stop        [�����ͺ��̽� �̸�]\n\
    restart     [�����ͺ��̽� �̸�]\n\
    status\n\
    acl         <status|reload> �����ͺ��̽� �̸�\n
37 ������ HA ���� �����Ǿ� �ֽ��ϴ�.\n
26 cubrid ��ƿ��Ƽ, ���� %1$s\n\
����: %2$s broker <���ɾ�> [����]\n\
\n\
������ ���ɾ�:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [-S] [-P] [-m] [-c] [-u] [<expr>]\n\
    on          <���Ŀ �̸�>\n\
    off         <���Ŀ �̸�>\n\
    reset       <���Ŀ �̸�>\n\
    acl         <status|reload> [���Ŀ �̸�]\n\
    getid       -b <���Ŀ �̸�> [-f] shard-key\n\
    test        <���Ŀ �̸�> [-D <database_name>] [-u <user_name>] [-p <user_password>] [-c <SQL_command>] [-i <input_file>] [-o <output_file>] [-v] [-s]\n
27 cubrid ��ƿ��Ƽ, ���� %1$s\n\
����: %2$s manager <���ɾ�>\n\
\n\
������ ���ɾ�:\n\
    start\n\
    stop\n\
    status\n
28 cubrid ��ƿ��Ƽ, ���� %1$s\n\
����: %2$s heartbeat <���ɾ�> [����]\n\
\n\
������ ���ɾ�:\n\
    start         [�����ͺ��̽� �̸�]\n\
    stop          [-h <ȣ��Ʈ �̸�>] [-i] [�����ͺ��̽� �̸�]\n\
    copylogdb     <start|stop> [-h <ȣ��Ʈ �̸�>] <�����ͺ��̽� �̸�> <��� �̸�>\n\
    applylogdb    <start|stop> [-h <ȣ��Ʈ �̸�>] <�����ͺ��̽� �̸�> <��� �̸�>\n\
    replication   <start|stop> <��� �̸�>\n\
    status         [-v] [-h <ȣ��Ʈ �̸�>]\n\
    reload\n
31 '%1$s' ���ڰ� �ʿ� �����ϴ�.\n
32 �����ͺ��̽� �̸��� �����ƽ��ϴ�.\n
33 %1$s: �� �ɼ��� ��� ���� �� ���Դϴ�.\n
34 ������ ������ �ֽ��ϴ�. %1$s %2$s\n
35 �޸� �Ҵ翡 �����Ͽ����ϴ�.\n
36 ������ HA ���� �������� �ʾҽ��ϴ�.\n
38 ������ HA ���� �����Ǿ� ������, ha_db_list ��Ͽ��� �����Ǿ� ���� �ʽ��ϴ�.\n
39 ������ HA ���� �����Ǿ� ������, ha_node_list ��Ͽ��� �����Ǿ� ���� �ʽ��ϴ�.\n
40 �߸��� ���ɾ� �Դϴ�.\n
41 CUBRID �޴����� ��ġ���� �ʾҽ��ϴ�.\n
42 ���ڰ� �߸��Ǿ����ϴ�.\n
43 ����(%1$s)�� ���� ���߽��ϴ�.\n
44 cubrid ��ƿ��Ƽ, ���� %1$s\n\
����: %2$s javasp <���ɾ�> [����]\n\
\n\
������ ���ɾ�:\n\
    start       [�����ͺ��̽� �̸�]\n\
    stop        [�����ͺ��̽� �̸�]\n\
    restart     [�����ͺ��̽� �̸�]\n\
    status      [�����ͺ��̽� �̸�]\n
45 cubrid ��ƿ��Ƽ, ���� %1$s\n\
����: %2$s gateway <���ɾ�> [����]\n\
\n\
������ ���ɾ�:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [-c] [-u] [<expr>]\n\
    on          <����Ʈ���� �̸�>\n\
    off         <����Ʈ���� �̸�>\n\
    reset       <����Ʈ���� �̸�>\n\
    acl         <status|reload> [����Ʈ���� �̸�]\n
46 owner_name�� ���̴� %1$d �̸��̾�� �ϰ� class_name�� ���̴� %2$d ����Ʈ �̸��̾�� �մϴ�.\n
47 ���̺� �̸�('%1$s')�� owner_name.class_name �������� �Է��Ͻʽÿ�.\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 ���: -t �ɼ��� ���� ���� ��忡���� ���� �˴ϴ�.\n
31 ����: ��� ��ΰ� �������� �ʰų� ���丮�� �ƴմϴ�.\n
32 --separate-keys �ɼ����� Ű ���� (_keys)�� ��������� �и��մϴ�.\n\
�ش� Ű ������ �н��� ����������� �ִ��� ���� (restoredb)�� �ùٸ��� ������� ���� �� �ֽ��ϴ�.\n
33 Ű ���� (_keys)�� ��� ������ �����մϴ�.\n\
���� TDE ������� ��ȣȭ�� ���̺��� �������� ������ ���, ���Ȼ��� ������ ���� �� �ֽ��ϴ�.\n\
Ű ������ �и��� ���ؼ� --serperate-keys �ɼ��� ����ϼ���.\n
34 ����: --separate-key �ɼ� ���� FIFO ������ ��� ��η� ������ �� �����ϴ�.\n
60 \
backupdb: �����ͺ��̽��� ����մϴ�.\n\
����: %1$s backupdb [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
�ɼ�:\n\
  -D, --destination-path=<���> ������ ����� ���͸� ���. �������� ���� ��쿡�� �α� ���͸�.\n\
  -r, --remove-archive         ���ʿ��� �α� ������ ����. �����ؼ� ����ؾ� �մϴ�. ������ �Ŵ����� �����ϼ���.\n\
  -l, --level=LEVEL            ��� ����. �⺻ ������ ��ü�Դϴ�.\n\
                               ������ ���� ��� ������ ������ �� �ֽ��ϴ�.\n\
                                   0 - ��ü\n\
                                   1 - ���� 1\n\
                                   2 - ���� 2\n\
  -o, --output-file=<����> �ڼ��� ��� �޽����� ���Ͽ� ����մϴ�.\n\
  -S, --SA-mode                ���� ��� ����\n\
  -C, --CS-mode                Ŭ���̾�Ʈ ���� ��� ����\n\
      --no-check               �����ͺ��̽��� ���ռ� Ȯ�� �۾��� �������� �ʽ��ϴ�; �⺻��: ����\n\
  -t, --thread-count=COUNT     ����� �����ϴ� ������ ����; �⺻��: �ڵ�\n\
      --no-compress            ��� ������ �������� �ʽ��ϴ�; �⺻��: ����\n\
      --sleep-msecs=N          �� 1M �б� ���� N �и��ʸ� ���ϴ�; �⺻��: 0\n\
  -k, --separate-keys          TDE�� ���Ǵ� Key File (_keys)�� ��������� �������� �ʰ� ������ ���Ϸ� �и��մϴ�.\n
71 \"%1$s\" �� \"%2$s\" (��)�� �Բ� ����� �� �����ϴ�.\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 ����: ��� �̸��� ���� �̸��� �����մϴ�.\n
31 ����: extended-volume-path �� �������� �ʰų� ���� �����Ǿ� �ֽ��ϴ�.\n
32 ����: control-file�� ���ų� ���� �� �����ϴ�.\n
60 \
copydb: �����ͺ��̽� ����.\n\
����: %1$s copydb [�ɼ�] <���� �����ͺ��̽� �̸�> <��� �����ͺ��̽� �̸�>\n\
\n\
�ɼ�:\n\
      --server-name=HOST          ���� ȣ��Ʈ �̸�; �⺻��: ���� ȣ��Ʈ �̸�\n\
  -F, --file-path=PATH            �����ͺ��̽��� ����Ǵ� ���͸� ���; �⺻��: ���� ���͸�\n\
  -L, --log-path=PATH             �α� ������ ����Ǵ� ���͸� ���; �⺻��: <file-path>\n\
  -B, --lob-base-path=PATH        LOB ������ ����Ǵ� ���丮 ���; �⺻��: <file-path>/lob\n\
  -E, --extended-volume-path=PATH Ȯ�� ������ ����Ǵ� ���͸� ���; �⺻��: �����ͺ��̽� ���� ���͸� ��� �Ǵ� control-file�� �־��� ���\n\
  -i, --control-file=FILE         ���� ���� �������� ���� ����Ǵ� ���͸� ��θ� �����ϴ� ���� ����; �⺻��: ����\n\
  -r, --replace                   ���� �̸��� �����ͺ��̽��� �����ϸ� �����; �⺻��: ����� ����\n\
  -d, --delete-source             ���� �� ���� �����ͺ��̽� ����; �⺻��: ���� ����\n\
      --copy-lob-path             ���� �����ͺ��̽��� LOB ���� ���͸� ��θ� ����. -B �ɼǰ� ���� ����� �� ����; �⺻��: ���� ����\n


$set 4 MSGCAT_UTIL_SET_CREATEDB
40 �׷� '%1$s'�� �������� �ʽ��ϴ�\n
41 ����� '%1$s'�� �������� �ʽ��ϴ�\n
42 �� �� ���� ���ɾ� '%1$s'\n
43 ��� ���� '%1$s' ��(��) �� �� �����ϴ�.\n
44 ���� �����ͺ��̽��� ������ �� �����ϴ�.\n
45 %1$s ũ���� �����ͺ��̽��� ������ "%2$s"���� �����մϴ�. �ʿ��� ��ũ�� �뷮�� %3$s �Դϴ�.\n
46 �����ͺ��̽��� ������ �� �����ϴ�.\n
47 user file '%1$s'�� ������ �ֽ��ϴ�.\n
48 The given '%1$s', %2$s is out of range. Choose a size between %3$s and %4$s.\n
49 %1$s�� �� '%2$s'�� ��ȿ���� �ʽ��ϴ�.\n
60 \
createdb: �����ͺ��̽� ����.\n\
����: %1$s createdb [�ɼ�] <�����ͺ��̽� �̸�> <�����ͺ��̽� ������>\n\
\n\
<�����ͺ��̽� ������>�� ������ <language>.<charset> �Դϴ�. ��: en_US.iso88591, ko_KR.utf8\n\
�ɼ�:\n\
      --db-volume-size=SIZE           ó�� �����Ǵ� ����(generic)�� ũ��\n\
                                      �⺻��: cubrid.conf�� db_volume_size�� ��\n\
      --db-page-size=SIZE             ����Ʈ ������ �����ͺ��̽� ������ ũ��; �⺻��: 16K\n\
      --log-volume-size=SIZE          �α� ������ ũ��\n\
                                      �⺻��: cubrid.conf�� log_volume_size�� ��\n\
      --log-page-size=SIZE            ����Ʈ ������ �α� ������ ũ��; �⺻��: �����ͺ��̽� ������ ũ��\n\
      --comment=COMMENT               �����ͺ��̽��� ���� ����; �⺻��: ����\n\
  -F, --file-path=PATH                �����ͺ��̽��� �����Ǵ� ���͸� ���; �⺻��: ���� ���͸�\n\
  -L, --log-path=PATH                 �α� ������ ����Ǵ� ���͸� ���; �⺻��: <file-path>\n\
  -B, --lob-base-path=PATH            LOB ������ ����Ǵ� ���͸� ���; �⺻��: <file-path>/lob\n\
      --server-name=HOST              ���� ȣ��Ʈ �̸�; �⺻��: ���� ȣ��Ʈ\n\
  -r, --replace                       ���� �̸��� �����ͺ��̽��� �����ϸ� �����; �⺻��: ����� ����\n\
      --more-volume-file=FILE         �߰��� Ȯ��� ������ ���� ���� ����\n\
      --user-definition-file=FILE     �����ͺ��̽� ����ڸ� �����ϴ� ����; �⺻��: ����\n\
      --csql-initialization-file=FILE �����ͺ��̽� ���� �� CSQL���� FILE�� ����� ���ǹ��� ����; �⺻��: ����\n\
  -o, --output-file=FILE              ��� �޽����� ���Ͽ� ����; �⺻��: ����\n\
  -v, --verbose                       ���� ���� ������ ȭ�� ���; �⺻��: ������� ����\n


$set 6 MSGCAT_UTIL_SET_DELETEDB
60 \
deletedb: �����ͺ��̽� ����.\n\
����: %1$s deletedb [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
�ɼ�:\n\
  -o, --output-file=FILE       ��� �޽����� ����� ����; �⺻��: ����\n\
  -d, --delete-backup          ��� ������ ����; �⺻��: ������ ����\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 ����: extended-volume-path �� �������� �ʰų� ���� �����Ǿ� �ֽ��ϴ�.\n
32 ����: control-file�� ���ų� ���� �� �����ϴ�.\n
60 \
renamedb: �����ͺ��̽� �̸� ����.\n\
����: %1$s renamedb [�ɼ�] <���� �����ͺ��̽� �̸�> <��� �����ͺ��̽� �̸�>\n\
\n\
�ɼ�:\n\
  -E, --extended-volume-path=PATH Ȯ�� ������ ����Ǵ� ���͸� ���; �⺻��: �����ͺ��̽� ���� ���͸� ��� �Ǵ� control-file�� �־��� ���\n\
  -i, --control-file=FILE         ���� ���� �������� ���� ����Ǵ� ���͸� ��θ� �����ϴ� ���� ����; �⺻��: ����\n\
  -d, --delete-backup             ��� ������ ����; �⺻��: ������ ����\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 %1$s ���μ����� �̹� ���� ���Դϴ�\n
12 ������ ���� ����\n
13 ������ ���� ����\n
14 ���� ȣ��Ʈ �̸��� ã�� �� �����ϴ�\n
15 ���� ������ ã�� �� �����ϴ�\n
16 ������ ���μ����� �ʱ�ȭ�� �� �����ϴ�\n
17 ���� %1$s�� %2$d �� ���� shutdown �մϴ�
18 ���� %1$s�� shutdown�˴ϴ�.\n�� �۾��� �� �� �ɸ� �� �ֽ��ϴ�. ��� ��ٸ��ʽÿ�.
19 ���� %1$s�� ã�� �� �����ϴ�
20 ������ ������ %1$d �� ���� shutdown �մϴ�\n
21 Heartbeat ����� ��Ȱ��ȭ �Ǿ����ϴ�. ��� ���� ���μ������� ��������� �Ϸ�ɶ� ���� ��� ��ٸ��ʽÿ�.\n

$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 ��� ���� '%1$s' ��(��) �� �� �����ϴ�\n
60 \
optimizedb: �����ͺ��̽� ����ȭ.\n\
����: %1$s optimizedb [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
�ɼ�:\n\
  -n, --class-name=NAME        �ش� Ŭ������ ��� ������ ������; �⺻��: ��ü Ŭ����\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 ��¥ ���� ����, ��¥ ����: ��-��-��:��:��:��(dd-mm-yyyy:hh:mm:ss) �Ǵ� 'backuptime'\n
20 ���� ��ҵǾ��ų� ���� �߻�\n
60 \
restoredb: �����ͺ��̽� ����\n\
����: %1$s restoredb [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
�ɼ�:\n\
  -d, --up-to-date=DATE             �־��� �ð����� �����ͺ��̽� ���¸� ����. ��¥ ����: dd-mm-yyyy:hh:mm:ss �Ǵ� 'backuptime'\n\
      --list                        ������ ���� �ʰ� ��� ��� ������ ����\n\
  -B, --backup-file-path=PATH       ����� ������ �����ϴ� ���͸� ���\n\
  -l, --level=LEVEL                 ������ ���� ����� ����; �⺻��: 0(Full backup)\n\
  -p, --partial-recovery            archive �αװ� �������� ������ ������ partial recovery ������\n\
  -o, --output-file=FILE            ��� �޽����� ����� ����; �⺻��: ����\n\
  -u, --use-database-location-path  �����ͺ��̽� ��ġ ���Ͽ� ������ ��ο� �����ͺ��̽� ������ �α� ������ ����\n\
  -k, --keys-file-path              ������ ���� Ű ���� (_keys)�� ��� \n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 �����ͺ��̽� �̸��� �����Ǿ����ϴ�.\n
20 ���� '%1$s' �� %2$d ��ŭ�� ������ �����ϴ�.\n
21 ���ͷ�Ʈ... \n
22 �ε尡 ���ͷ�Ʈ �� - ���� Ʈ������� Ŀ�Ե˴ϴ�\n
23 �ε尡 ���ͷ�Ʈ �� - ���� Ʈ������� öȸ�˴ϴ�\n
24 ���� '%1$s'�� �� �� �����ϴ�.\n
25 \n*** ���� �˻� ***\n\n
26 %1$d ���� ������ ����Ǿ����ϴ�.\n
27 \n*** ��ü ���� �� ***\n\n
28 �� %1$ld ���� ��ü�� ���ԵǾ����ϴ�.\n
29 �� %1$8d ���� ������ ��ü�� �⺻ ���� ������ �ԷµǾ����ϴ�.\n
30 \n*** Ʈ����� Ŀ�� �� ***\n
31 \n*** �����ͺ��̽� �ݴ� �� ***\n
32 ���� %1$d:
33 '%4$s'�� �Ӽ� '%3$s'�� ���� '%1$s'��(��) '%2$s' ������ �Ľ� �� ����\n
34 %2$s�� �Ӽ� '%1$s'�� ���� ������ ����\n
35 %2$s�� �Ӽ� %1$s�� ���� Ÿ�� %3$s��(��) set�� ���������� ������ �ʽ��ϴ�.\n
36 %2$s�� �Ӽ� %1$s�� ���� Ÿ�� %3$s��(��) ���������� set�� �ԷµǾ����ϴ�.\n
37 %2$s�� �Ӽ� %1$s�� ���� Ÿ�� %3$s��(��) ���������� %4$s��(��) �ԷµǾ����ϴ�.
38 �������� �ʴ� Ŭ���� %1$s��(��) %3$s�� �Ӽ� %2$s�� ���� �����Ǿ����ϴ�.
39 %1$s Ŭ������ �������� �ʽ��ϴ�. Ʃ�õ��� ���õǾ����ϴ�.\n
40 �ĺ��� %1$d�� �������� �ʴ� Ŭ������ %3$s�� �Ӽ� %2$s�� ���� �����Ǿ����ϴ�.\n
41 Ŭ���� %1$s�� �δ� �Է� ���� ������ ������ �ʽ��ϴ�.\n
42 ������ ���� �ߴ�.\n
43 ���: ���� �ν��Ͻ��� ����.\n
44 ���: Ŭ���� %2$s�� �ν��Ͻ� %1$d��(��) ������.\n
45 Ŭ���� %2$s�� �ν��Ͻ� %1$d��(��) �̹� ���ǵǾ����ϴ�.\n
46 Ŭ���� %2$s�� �ν��Ͻ� %1$d��(��) �̹� ���Ǿ����ϴ�.\n
47 �Ӽ� '%1$s'���� UNIQUE ����, NULL�� ��ȯ �Ǿ����ϴ�.\n
48 %1$s %2$ld �ν��Ͻ�\n
49 Ŭ���� %1$s\n
50 �н�����:
51 \n*** Ŭ���� ��� ���� ���� �� ***\n
52 %1$s ������ ���� %2$d���� �߻�('%3$s')\n
53 Lexical ����\n
54 ���� %1$d: ���� ���� at \"%2$s\"
55 missing
56 in %1$s
57 �Բ� ����� �� ���� ���� \"%1$s\", �� \"%2$s\"\n
58 %1$s %2$ld �ν��Ͻ��� Ŀ�� �Ǿ����ϴ�\n
59 \n���� ������ loaddb�� ������ �� �����ϴ�.\n
60 ���� parsing \"%1$s\", as %2$s type.\n
61 ������Ʈ ������ CS ��忡�� �������� �ʽ��ϴ�.\n
62 ���̺� %1$s�� ������� ������Ʈ.\n
112   %1$ld ���� �ν��Ͻ�\r
113 ���������� Ŀ�Ե� ���� : %1$d\n
114 ���� ���� ����
115 �ε� �ÿ� ���� ��� ���� �Է� ����
116 �� %1$ld ���� ��ü�� ���ԵǾ�����, %2$ld ���� ��ü�� �����Ͽ����ϴ�.\n
117 �ε� ����.\n
118 Ŭ���� �̸��� �ִ� ���̴� %1$d ����Ʈ �Դϴ�.\n
119 ��ü %1$8d ������Ʈ �˻�, %2$d ������Ʈ ����.\n
120 ������ ������ '%%class'�� �����ϰų� '--table' �ɼ��� �־����� �մϴ�.\n
121 ���õ� ���̺� %1$s.\n
122 ����� �̸��� �ִ� ���̴� %1$d ����Ʈ �Դϴ�.\n
123 \
loaddb: �����ͺ��̽��� ��ü �� ��Ű�� ����\n\
����: %1$s loaddb [�ɼ�] �����ͺ��̽� �̸�[@ȣ��Ʈ]\n\
\n\
�ɼ�:\n\
  -S, --SA-mode                  ���� ��� ����\n\
  -C, --CS-mode                  Ŭ���̾�Ʈ ���� ��� ����\n\
  -u, --user=ID                  �����ͺ��̽� ���ٿ� ���� ����� �̸�; �⺻��: "public"\n\
  -p, --password=PASS            ����� �н�����; �⺻��: ����\n\
      --data-file-check-only     ������ ������ �ε����� �ʰ� ���ý��� �˻�; �⺻��: �ƴϿ�\n\
  -l, --load-only                ���ý� üũ���� ������ ���� �ε�; SA_MODE�� ����\n\
      --estimated-size=NUMBER    �ν��Ͻ��� �뷫���� ����; �⺻��: 5000\n\
  -v, --verbose                  ���� ���� ������ ���; �⺻��: ������� ����\n\
  -c, --periodic-commit=COUNT    �ֱ����� Ŀ�Կ� ���� ī��Ʈ; �⺻��: ���� �� Ŀ������ ����(SA_MODE)\n\
      --no-oid                   OID ��� �� ��\n\
      --no-statistics            �ε� �Ŀ� ��� ���� �������� ����\n\
  -s, --schema-file=FILE[:LINE]  ����[:����] ������ ��Ű�� ����\n\
  -i, --index-file=FILE[:LINE]   ����[:����] ������ �ε��� ����\n\
      --trigger-file=FILE[:LINE] ����[:����] ������ Ʈ���� ����\n\
  -d, --data-file=FILE           ������ ������ ����\n\
  -t, --table=TABLE              �����͸� ������ ���̺� �̸�; ������ ���Ͽ� ���̺� ������ ���� ��� ���\n\
      --error-control-file=FILE  ���� �� �߻��ϴ� ������ ���� ���� ����\n\
      --ignore-class-file=FILE   �������� ���� Ŭ���� �̸��� �ִ� ����\n\
      --no-user-specified-name   ������ �̸� ���� ��ü �̸����� CLASS, SERIAL �� TRIGGER�� ã��.\n\
                                 CUBRID 11.2 �� �������� ������ ������ �ҷ����� ����.\n


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 cached-pages ���� ��ȿ���� �ʽ��ϴ�.\n
42 cached-page-size ���� ��ȿ���� �ʽ��ϴ�.\n
43 %1$ld ���� ��ü�� ���� �Ǿ����ϴ�.\n
44 Ŭ���� %1$s
45 , %1$d ���� �ν��Ͻ�.\n
46 %1$ld ���� ��ü�� (��ü %2$ld ��ü) ���������� ���� �Ǿ����ϴ�.\n
47 ��� ���͸� �̸��� �ʹ� ��ϴ�.\n
48 LSA : %1$d %2$d \n
49 �н�����:
50 Ŭ���� �̸��� �ִ� ���̴� %1$d ����Ʈ �Դϴ�.\n
51 ����� �̸��� �ִ� ���̴� %1$d ����Ʈ �Դϴ�.\n
52 Ŭ���� %1$s�� ã�� �� �����ϴ�.\n
60 \
unloaddb: �����ͺ��̽����� ��Ű���� ��ü�� ��ε�\n\
����: %1$s unloaddb [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
�ɼ�:\n\
  -u, --user=ID               �����ͺ��̽� ���ٿ� ���� ����� �̸�; �⺻��: "dba"\n\
  -p, --password=PASS         ����� �н�����; �⺻��: ����\n\
  -i, --input-class-file=FILE ���̺� �̸��� ���Ե� ����; �⺻��: ��� Ŭ����\n\
      --include-reference     �����Ǵ� ���̺� ����; �⺻��: ������� ����(-i�� ���� �Ǿ� ��)\n\
      --input-class-only      ������ Ŭ������ ó��; �⺻��: ������� ����(-i�� ���� �Ǿ� ��)\n\
      --lo-count=COUNT        �ϳ��� ���͸��� ������ lo ���� ����; �⺻��: 0\n\
      --estimated-size=NUMBER �ν��Ͻ��� �뷫���� ����; �⺻��: ����\n\
      --cached-pages=NUMBER   cached ������ ����; �⺻��: ����\n\
  -O, --output-path=PATH      ��� ���͸� ���; �⺻��: ���� �۾� ���͸�\n\
  -s, --schema-only           ��Ű���� ó��; �⺻��: ��Ű���� ������Ʈ ó��\n\
  -d, --data-only             ������Ʈ�� ó��; �⺻��: ��Ű���� ������Ʈ ó��\n\
      --output-prefix=PREFIEX ��� ������ prefix; �⺻��: �����ͺ��̽� �̸� ���\n\
      --hash-file=FILE        �ؽ� �����̸�; �⺻��: ������ �����ϱ� ���� tmpnam()�� �����\n\
      --latest-image          ������Ʈ (��ü)�� ��ε��� ��, ������ ������ �ֽ� �̹����� ��ε��մϴ�. �� �������� �α� ������ �������� ������, MVCC�� ���õ˴ϴ�.\n\
                              Ŀ�Ե������� �����Ϳ� ���ŵǾ����� ��Ũ���� ���� �����Ͱ� ���Ե� �� �ֽ��ϴ�; �⺻ ��: ��� �� ��\n\
  -v, --verbose               ���� ���� �޽����� ���; �⺻��: ��� �� ��\n\
      --use-delimiter         �ĺ��� ó���� ���� '"' ���; �⺻��: ��� �� ��\n\
  -S, --SA-mode               ���� ��� ����\n\
  -C, --CS-mode               Ŭ���̾�Ʈ ���� ��� ����\n\
      --datafile-per-class    �� Ŭ������ ������Ʈ ���� ����; �⺻��:�� ���� ������Ʈ ���ϻ���\n



$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 \n�н� 1\n\n
12 %1$d ������Ʈ�� ó���Ǿ����ϴ�.\n
13 \n�н� 2\n\n
14 Ŭ���� %1$s\n
15 oid:  %1$d|%2$d|%3$d,\n
16 %1$d �ν��Ͻ�.\n
17 ��ü ���� ��\n
18 ref_oid:  %1$d.%2$d.%3$d, ref_class_oid:  %4$d.%5$d.%6$d\n
19 ��ü�� ��ȯ�� �� �����ϴ�\n
20 Ŭ������ �� ������ �����ϴ�\n
21 ��ü�� �������� ���߽��ϴ�\n
22 %1$d ���� ��ü(��ü %2$d ��ü)�� ���������� ó���Ǿ����ϴ�.\n
23 COMPACTDB�� �̹� ���۵Ǿ����ϴ�.\n
24 ������ ������ %1$d�� %2$d ���̰� �Ǿ�� �մϴ�.\n
25 �ν��Ͻ� ��� Ÿ�Ӿƿ��� %1$d�� %2$d ���̰� �Ǿ�� �մϴ�.\n
26 ��ü ������Ʈ ��: %1$d\n
27 ������ ������Ʈ ��: %1$d\n
28 ������ ������Ʈ ��: %1$d\n
29 ū ������Ʈ ��: %1$d\n
30 īŻ�α׿��� ���� Ŭ���� ǥ���� �����Ǿ����ϴ�.\n
31 īŻ�α׿� ���� Ŭ���� ǥ���� �����Ǿ� �ֽ��ϴ�.\n
32 Ʈ����� ���� ������ ������ �� �����ϴ�.\n
33 �����ͺ��̽��� ������ ������ �� �����ϴ�.\n
34 Ŭ���� ��� Ÿ�Ӿƿ��� %1$d�� %2$d ���̰� �Ǿ�� �մϴ�.\n
35 ��� Ŭ����... �ҿ����� ó��...\n
36 ó������ ���� Ŭ����...\n
37 ó�� ����... �ҿ����� ó��...\n
38 ó���� ���� ����...\n
39 ��ȿ���� ���� �Ķ����...\n
40 �� �� ���� Ŭ���� �̸�...\n
41 ������ ���� Ŭ���� OID�� �������� ȸ���Ǿ����ϴ�.\n
42 OID�� �������� ���� ȸ���� �����Ǿ����ϴ�.\n
43 Ŭ������ ó���ϴ� ���� ������ ���� Ŭ������ ó���� �����Ǿ����ϴ�.\n
44 Pass 3\n\n
45 �� ���� ������ �����߽��ϴ�.\n
46 �� ���� ������ �����߽��ϴ�.\n
47 Ŭ���� �̸��� �ִ� ���̴� %1$d ����Ʈ �Դϴ�.\n
48 ����� �̸��� �ִ� ���̴� %1$d ����Ʈ �Դϴ�.\n
60 \
compactdb: �����ͺ��̽����� ������ �ʴ� ������ ����.\n\
����: %1$s compactdb [�ɼ�] <�����ͺ��̽� �̸�> [���̺�1 ���̺�2 ...]\n\
\n\
�ɼ�:\n\
  -v, --verbose                     ���� ���� ���� ���; �⺻��: ��� �� ��\n\
  -i, --input-class-file=FILE       ���̺� �̸��� �����ϴ� FILE �̸�\n\
                                    �ش� �ɼ� ��� �� [���̺�1 ���̺�2 ...]�� �����Ǿ�� ��\n\
                                    �⺻��: ��� ���̺����� ������ ������\n\
  -p, --pages-commited-once=NUMBER  �� ���� Ŀ���� �� �ִ� �ִ� ������ ����\n\
                                    �⺻��: 10\n\
                                    Ŭ���̾�Ʈ ���� ���� ��忡���� ����\n\
  -S, --SA-mode                     ���� ��� ����\n\
  -C, --CS-mode                     Ŭ���̾�Ʈ ���� ��� ����\n\
  -d, --delete-old-repr             īŻ�α׿��� ���� ���̺� ǥ���� ����; �⺻��: ��� �� ��\n\
  -I, --Instance-lock-timeout       �ν��Ͻ� ��ݿ� ���� Ÿ�Ӿƿ�\n\
                                    Ŭ���̾�Ʈ ���� ��忡���� ����\n\
  -c, --class-lock-timeout          Ŭ���� ��ݿ� ���� Ÿ�Ӿƿ�\n\
                                    Ŭ���̾�Ʈ ���� ��忡���� ����\n\
  [���̺�1 ���̺�2 ...]             ������ ���̺� ����Ʈ\n

$set 15 MSGCAT_UTIL_SET_COMMDB
21 CUBRID master�� ȣ��Ʈ %1$s ���� ���� ���Դϴ�. %2$s �� ���۵Ǿ�
22 %1$d ���� ��û�� ó�� �Ͽ����ϴ�. \n���� %2$d ���� ������ ���� ���Դϴ�.\n
23 %1$s �� ���� SHUTDOWN �մϴ�!!!\n
24 %1$s
25 Shutdown �� ������ �̸� :
26 Shutdown �� �������� �ð�(��)? :
27 \
cub_commdb: �� ��ƿ��Ƽ�� �� �̻� ������� �ʽ��ϴ�. 'cubrid' ������ ����Ͻʽÿ�.\n\
����: cub_commdb [�ɼ�]\n\
\n\
�ɼ�:\n\
  -P, --server-list                ���� ���μ��� ���\n\
  -O, --all-list                   ��� ���μ��� ���\n\
  -S, --shutdown-server=NAME       �ش� �����ͺ��̽� ���� shutdown\n\
  -A, --shutdown-all               ��� ���μ��� shutdown\n\
  -c, --server-mode=NAME           �ش� �����ͺ��̽� ������ HA ��� ���\n\
  -N, --node-list                  CUBRID heartbeat�� ���� �����Ǵ� ��� ��ȸ\n\
  -L, --process-list               CUBRID heartbeat�� ���� �����Ǵ� ���� ���μ��� ���� ��ȸ\n\
  -p, --ping-host                  CUBRID heartbeat�� ���� ping ȣ��Ʈ ���� ��ȸ\n\
  -D, --dereg-process=PID          ��� ���μ����� �����ϰ� ���� ��� ��Ͽ��� ����\n\
  -F, --reconfig-node-list         CUBRID heartbeat ��� ���� ������ �� ����\n\
  -V, --verbose                    ���� ���� �޽��� ���; �⺻��: ������� ����\n
28 �� �� ���� �����Դϴ�: %1$s\n
29 ȣ��Ʈ �̸��� ã�� �� �����ϴ�-����\n
30 ���� ������ ã�� �� �����ϴ�\n
31 %1$s ���� ������ ������ ������ �� �����ϴ�\n
32 �� �Ŀ� shutdown �Ͻðڽ��ϱ�?\n
33 �����ʹ� %1$d �� �Ŀ� shutdown �˴ϴ�.\n
34 Ŭ���̾�Ʈ�κ����� ������ ���� ����
35 �ѱ� database ���� %1$s ��(��) ���� ���� ���� �ƴմϴ�.
36 �� (%1$s) �� ������ �� �����ϴ�. %2$s, %3$s
37 ���μ��� fork %1$s, ��ȯ %2$d, %3$s
38 %1$s��(��) ������ �� �����ϴ�
39 -i(--immediately)�ɼ��� --deact-stop-all �ɼ��� �����Ǿ���մϴ�.\n

$set 16 MSGCAT_UTIL_SET_PATCHDB
1 --recreate-log �ɼ� ���� �����ϰų� ��Ƽ�� �α� ������ ���� �������ּ���.\n
60 \
emergency_patchlog: ����� ��Ȳ���� �����ͺ��̽� �α� ��ġ\n\
usage: %1$s emergency_patchlog [OPTION] database-name [database-locale]\n\
\n\
�ɼ�:\n\
  -r, --recreate-log           recreate log files; use carefully (needs database-locale argument)\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 ������ ���� �� %1$d�� �߸��Ǿ����ϴ�\n
21 ���� ������̸� '%1$s'�� �߸��Ǿ����ϴ�\n
22 --max-writesize-in-sec �ɼ��� ���� ���� ��忡���� ���� �˴ϴ�.\n
60 \
addvoldb: �����ͺ��̽� ���� ������ �߰�\n\
����: %1$s addvoldb [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
�ɼ�:\n\
      --db-volume-size=SIZE        �߰��� ������ ũ��; �⺻��: �������� ����\n\
      --max-writesize-in-sec=SIZE  �ʴ� �߰��� ������ ũ��. (��. 512K, 1M, 10M); �⺻��: ������� ����; �ּҰ�: 160K\n\
  -n, --volume-name=NAME           ���� �̸�; �⺻��: "db"_ext1 �� ���� �̸��� ����\n\
  -F, --file-path=PATH             �����ͺ��̽� ���Ͽ� ���� ���� �н�; �⺻��: volume_extension_path\n\
      --comment=COMMENT            ���� �ּ�; �⺻��: ����\n\
  -p, --purpose=PURPOSE            ���� ��� �뵵; ������ ���� ���� ����\n\
                                           DATA - ������ ����\n\
                                           INDEX - �ε��� ����\n\
                                           TEMP - �ӽ� ������ ����\n\
                                           GENERIC - ���� ������ ����\n\
  -S, --SA-mode                    ���� ��� ����\n\
  -C, --CS-mode                    Ŭ���̾�Ʈ ���� ��� ����\n

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 �����ͺ��̽����� ���ϰ����� �߰ߵǾ����ϴ�.\n�߰����� ������ error_log_file %1$s ��(��) ���� �Ͻʽÿ�\n
21 %1$s: Ŭ������ ã�� �� ���� �����մϴ�.\n
22 %1$s: Cannot find the index on class %2$s.\n
60 \
checkdb: �����ͺ��̽� �ϰ��� �˻�.\n\
����: %1$s checkdb [�ɼ�] <�����ͺ��̽� �̸�> [class_name1 class_name2 ...]\n\
\n\
�ɼ�:\n\
  -S, --SA-mode                ���� ��� ����\n\
  -C, --CS-mode                Ŭ���̾�Ʈ ���� ��� ����\n\
  -r, --repair                 �� �ϰ��� �߰� �� ����\n\
  -i, --input-class-file=FILE  Ŭ���� �̸��� ������ �Է� ���� ����\n\
                               �⺻��: ��� Ŭ������ �˻�\n\
  -I, --index-name             index name that will be checked only\n\
                               default: check all indices\n\
      --check-prev-link        �ε��� ��ũ�� ������ �˻�\n\
      --repair-prev-link       �ε��� ��ũ�� �߸��� ��� ���� ����\n\
      --check-file-tracker     check all allocated pages of file\n\
      --check-heap             check all pages of heap files\n\
      --check-catalog          check catalog consistency\n\
      --check-btree            check all btree indices\n\
      --check-class-name       check class name consistency\n\
      --check-btree-entries    check entries of btree consistency\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
10 �����ͺ��̽� '%1$s'�� (������ ũ�� %2$s) ���� ���� (�α� ������ ũ�� %3$s)\n\n
11 ������� '%1$s' ��(��) �� �� �����ϴ�\n
12 LOB ���� ����� %1$s\n\n
20 type                purpose            volume_count        used_pages          free_pages          total_pages\n
21 type                purpose            volume_count         used_size           free_size           total_size\n
22 PERMANENT           PERMANENT DATA            %1$5d     %2$13s       %3$13s        %4$13s\n
23 PERMANENT           TEMPORARY DATA            %1$5d     %2$13s       %3$13s        %4$13s\n
24 TEMPORARY           TEMPORARY DATA            %1$5d     %2$13s       %3$13s        %4$13s\n
25 -                   -                         %1$5d     %2$13s       %3$13s        %4$13s\n
30 ��ü ������ ���� ���� ����:\n
31 volid               type                purpose            used_pages          free_pages          total_pages         volume_name\n
32 volid               type                purpose             used_size           free_size           total_size         volume_name\n
33 %1$5d               PERMANENT           PERMANENT DATA  %2$13s       %3$13s        %4$13s         %5$-64s\n
34 %1$5d               PERMANENT           TEMPORARY DATA  %2$13s       %3$13s        %4$13s         %5$-64s\n
35 %1$5d               TEMPORARY           TEMPORARY DATA  %2$13s       %3$13s        %4$13s         %5$-64s\n
40 ���Ͽ� ���� �� ���� ����:\n
41 data_type           file_count          used_pages         file_table_pages    reserved_pages      total_pages\n
42 data_type           file_count           used_size          file_table_size     reserved_size       total_size\n
43 %1$-10s               %2$5d       %3$13s            %4$13s     %5$13s    %6$13s\n
50 -----------------------------------------------------------------------------------------------------------------\n
60 \
spacedb: �����ͺ��̽� ��ũ ���� ���� ���\n\
����: %1$s spacedb [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
�ɼ�:\n\
  -o, --output-file=FILE       ���Ϸ� ����� ������; �⺻��: ������ ���� ����\n\
  -S, --SA-mode                ���� ��� ����\n\
  -C, --CS-mode                Ŭ���̾�Ʈ ���� ��� ����\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               ��� ����(page, mega, giga, tera, human readable) ����; �⺻��: H\n\
  -s, --summarize              ���� �뵵�� �հ� ��� ���\n\
  -p, --purpose                �� ������ ������ (������/�ε���/�ӽ�) ��뷮�� ���\n

$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 �����Ǵ� ������ ���� = %1$d\n
60 \
estimate_data: ������ ũ�� ����\n\
����: %1$s estimate_data number-of-instances average-instance-size number-of-attributes variable-number-of-attributes\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 �߸��� �ε��� Ű Ÿ���Դϴ�  = %1$s\n
16 ��� Ű ���� �߸��Ǿ����ϴ� length = %1$d, precision = %2$d\n
17 �߸��� ���ڰ� �ֽ��ϴ�\n
20 ���� ��ü �ε��� ������ �� = %1$d\n(�ν��Ͻ��� ������� ���� �ε����� �����Ǵ� ���)\n
21 ���� ��ü �ε��� ������ �� = %1$d\n(�ν��Ͻ��� ��������� ���� �ε����� �����Ǵ� ���(average case))\n
22 ���� ��ü �ε��� ������ �� = %1$d\n(�ν��Ͻ��� ��������� ���� �ε����� �����Ǵ� ���(worst case))\n
23 �־��� �Է¿� ���� ��...\n
24 �ν��Ͻ��� �� ����  : %1$d\n
25 ���� �ٸ� Ű�� ���� : %1$d\n
26 ��� Ű ũ��        : %1$d\n
27 Ű Ÿ��             : %1$s\n\n
60 \
estimate_index: �ε��� ũ�� ����\n\
����: %1$s estimate_index number-of-instances number-of-different-keys average-key-size key-type\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: �����ͺ��̽� ��ġ\n\
����: %1$s installdb [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
�ɼ�:\n\
      --server-name=HOST       ���� ȣ��Ʈ �̸�; �⺻��: ���� ȣ��Ʈ �̸�\n\
  -F, --file-path=PATH         �����ͺ��̽� ������ ���� ���͸� ���; �⺻��: ���� �۾� ���͸�\n\
  -L, --log-path=PATH          �α� ������ ���� ���͸� ���; �⺻��: file-path�� ����\n

$set 23 MSGCAT_UTIL_SET_MIGDB
1 �ӽ� Ŭ���� OID\n
2 ELO ���� ��� �� �� �����ϴ�\n
3 long object �׼��� ����\n
4 long object migration �� ���� ��� ���� ���� ����\n
5 �б� ����\n
6 ���� ����\n
7 ELO �Է� ���� "%1$s" ��(��) ���µ� ����\n
8 Ŭ����:
9 �ؽ� ���� �̸�
10 �̸�:                          %1$s\n
11 ũ��:                          %1$d\n
12 ������ ũ��:                   %1$d\n
13 ������ ũ��:                   %1$d\n
14 ��Ʈ�� ũ��:                   %1$d\n
15 �������� ��Ʈ����:             %1$d\n
16 Cached ������:                 %1$d\n
17 ��Ʈ���� ����:                 %1$d\n
18 collision ����:                %1$d\n
19 �ؽ� ���� �̸�:                %1$s\n
20 ���� �����÷ο� ��Ʈ��:        %1$d\n
21 Ű Ÿ��:                       %1$s\n
22 ������ ����� �ּ�:            %1$x\n
23 ������ ������ ����� �ּ�:     %1$x\n
24 ���� ������ ����� �ּ�:       %1$x\n
25 ������ ��Ʈ���� �ּ�:          %1$x\n
26 ������ ��Ʈ�� ũ��:            %1$d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
15 ��� ���� '%1$s'�� �� �� �����ϴ�.\n
60 \
diagdb: �����ͺ��̽� ���� ����\n\
����: %1$s diagdb [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
�ɼ�:\n\
  -o, --output-file=FILE       ��� �޽����� �������� ����; �⺻��: ����\n\
      --emergency              ���� �����; �⺻��: ����\n\
  -d, --dump-type=TYPE         TYPE of dump\n\
                               -1 ��� ����\n\
                                1 ���� ���̺� ����\n\
                                2 ���� �뷮 ����\n\
                                3 �� �뷮 ����\n\
                                4 �ε��� �뷮 ����\n\
                                5 Ŭ���� �̸� ����\n\
                                6 ��ũ bitmap ����\n\
                                7 īŻ�α� ����\n\
                                8 �α� ����\n\
                                9 �� ����\n


$set 25 MSGCAT_UTIL_SET_LOCKDB
15 ��� ���� '%1$s'�� �� �� �����ϴ�.\n
59 lockdb�� ���� ��忡�� ������ �� �����ϴ�.\n
60 \
lockdb: �����ͺ��̽��� lock ���� ���\n\
����: %1$s lockdb [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
�ɼ�:\n\
  -o, --output-file=FILE       ��� �޽����� �������� ����; �⺻��: ����\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 ���� ���� �� �ϳ��� ���: Ʈ�����-�ε���, �����, ȣ��Ʈ, ���α׷�\n
21 DBA ��� ��ȣ:
26 �־��� ���ڿ� �ش��ϴ� Ʈ������� �����ϴ�\n
27 ���� Ʈ����ǵ��� �����ų �غ� �Ǿ����ϴ�:\n\n
28 ����Ͻðڽ��ϱ�? (Y/N)
29 Ʈ����� �ε��� %1$d�� ������ Ʈ����ǵ��� �����ŵ�ϴ�\n
30 ���� Ʈ����ǵ��� ���ῡ �����߽��ϴ�\n\n
31 Ʈ������� ������� �ʾҽ��ϴ�. �ѹ� ���� ���� �� �ֽ��ϴ�.\n ��� ��ٷȴٰ� Ȯ���ϰ� ��õ��Ͻñ� �ٶ��ϴ�.\n
32 ��ȿ���� ���� Ʈ����� �ε����Դϴ�.(%1$s).\n
59 killtran�� ���� ��忡�� ������ �� �����ϴ�.\n
60 \
killtran: �����ͺ��̽� transaction ���� ����\n\
����: %1$s killtran [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
�ɼ�:\n\
  -i, --kill-transaction-index=INDEX�� �����Ǵ� �ش� Ʈ����� ����\n\
      --kill-user-name=ID            ID�� �����Ǵ� ������� ��� Ʈ����� ����\n\
      --kill-host-name=HOST          HOST�� �����Ǵ� Ŭ���̾�Ʈ�� ��� Ʈ����� ����\n\
      --kill-program-name=NAME       NAME���� �����Ǵ� ��� Ʈ����� ����\n\
      --kill-sql-id=SQL_ID           SQL_ID�� �ش��ϴ� ���Ǹ� �������� ��� Ʈ����� ����\n\
  -p, --dba-password=PASS            DBA �н�����; �������� ���� ��� ������Ʈ\n\
                                     (-i �Ǵ� --kill �ɼǰ� ��� ����, �׿ܴ� ���� �߻�)\n\
  -d, --display-information          ���� Ȱ��ȭ�� Ʈ����ǿ� ���� ���� ������\n\
  -q, --query-exec-info              ���� Ȱ��ȭ�� Ʈ����ǿ� ���� ���� ���������� ������\n\
  -f, --force                        ����� Ȯ�� ���� ������ Ʈ����� ����\n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1  �����ͺ��̽�-�̸�
2  �ʼ� ����; �⺻��: ����
3  ȣȯ�� �˻�
4  ȣȯ�� �˻縸 ����
5  ��� ���
6  ��� ������ ������ ���͸� ��γ� ����̽� �̸�
20 �����ͺ��̽� '%1$s'�� �������� �ʽ��ϴ�.
21 '%1$s'��(��) ������ �ʹ� ���� %2$s���� ���̱׷��̼� �� �� �����ϴ�.\n
22 '%1$s'��(��) ���̱׷��̼��� �ʿ� �����ϴ�.\n
23 '%1$s'��(��) ���̱׷��̼��� �ʿ��մϴ�.\n
24 ���̱׷��̼� �� ����ϴ� ���� �����մϴ�. ���� ����Ͻðڽ��ϱ�? (y/n):
25 ���̱׷��̼� �� ����ϴ� ���� �����մϴ�.
26 \n*** ���: %1$s ���� ���̱׷��̼� ���� ���� �����ͺ��̽��� ����Ͻʽÿ� ***\n\n���̱׷��̼� �� �߻��� �� �ִ� ��ָ� �����ϱ� ���ؼ��� �ݵ�� ����� �ʿ��մϴ�.\n��� �ϼ̽��ϱ�? (y/n):
27 \n1. backupdb ���� \n2. ���ο� �����ͺ��̽��� ���� ���ο� ��� ��� �Է�. (���� ���: %1$s) \n3. ���� \n
28 \n���ο� �����ͺ��̽��� ���� ��� ���:
29 \n���̱׷��̼� �� ����� ���������� �����߽��ϴ�.
30 ����� �����߽��ϴ�. �ٽ� ����Ͻʽÿ�.
31 \n\n*** ���: ���̱׷��̼� �Ϸ� ���Ŀ��� ���� ������ ��� ������ �̿��Ͽ� ������ �� �����ϴ�.\n���� ������ ��� ������ �ش� ���������� ������ �����մϴ�.\n���� ������ ��� ������ �����Ͻʽÿ�. ***\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: �����ͺ��̽��� ȣ��Ʈ ����\n\
����: %1$s alterdbhost [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
�ɼ�:\n\
  -h, --host=HOST              ȣ��Ʈ �̸�; �⺻��: ���� ȣ��Ʈ �̸�\n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 database-name
2 �ʼ� ����; �⺻��: ����
3 java-class-file
4 �ʼ� ����; �⺻��: ����
5 force-overwrite
6 Ŭ���� ������ �̹� �����ϸ� ���; �⺻��: ����� ����
7 java-package-name
8 java package name, empty if not specified

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 ��� ���� '%1$s'�� �� �� �����ϴ�\n
59 plandump�� ���� ��忡�� ������ �� �����ϴ�.\n
60 \
plandump: ���� �÷� ǥ�� �� ����\n\
����: %1$s plandump [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
�ɼ�:\n\
  -d, --drop                   ���� �ɽÿ� �ִ� ��� �÷� ����\n\
  -o, --output-file=FILE       ��� �޽����� �������� ����; �⺻��: ����\n

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 ��� ���� '%1$s'�� �� �� �����ϴ�.\n
21 Ŭ���̾�Ʈ ���α׷��� ���� ���Ǵ� �ý��� �Ķ����\n
22 ���� '%1$s'�� ���� �ε��� �ý��� �Ķ����\n
23 ���� ��忡�� ���Ǵ� �ý��� �Ķ����\n
60 \
paramdump: �ý��� �Ķ���� ������ ����\n\
����: %1$s paramdump [�ɼ�] database-name\n\
\n\
valid options:\n\
  -o, --output-file=FILE       ��� �޽����� �������� ����; �⺻��: ����\n\
  -b, --both                   dump both client and server parameters\n\
  -S, --SA-mode                ���� ��� ����\n\
  -C, --CS-mode                Ŭ���̾�Ʈ ���� ��� ����\n

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 ������ HA ��忡 ���� �߸��� �� \"%1$s\"�Դϴ�.\n
12 ������ HA ��带 �ٲ� �� �����ϴ�.\n
21 DBA ��ȣ �Է�:
22 ���� '%1$s'�� ���� HA ���� %2$s�Դϴ�.\n
23 ���� '%1$s'�� HA ���� \"%2$s\"�� �ٲ�����ϴ�.\n
24 ������ HA�� ����� �� �ֵ��� �������� �ʾҽ��ϴ�.\n
58 HA ����� �������� �ʴ� �÷����Դϴ�.\n
59 changemode�� ���� ��忡�� ������ �� �����ϴ�.\n
60 \
changemode: ������ HA ��� ��� �Ǵ� ����\n\
����: %1$s changemode [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
valid options:\n\
  -m, --mode=MODE             ������ HA ��带 ������ MODE�� ����; �⺻��: ����\n\
  -f, --force                 ���� ����\n\
  -t, --timeout=S             Ʈ������� ���� ����� ������ S�� ���\n\
                              -t �ɼ��� ����ڰ� ������ HA ��带 maintenance�� ������ ���� ��ȿ\n\
                              �⺻��: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 copylogdb�� ���� ��忡 ���� �߸��� �� \"%1$s\"\n
21 DBA ��ȣ �Է�:
22 ������ HA ���� �������� �ʾҽ��ϴ�.\n
58 HA ����� �������� �ʴ� �÷����Դϴ�.\n
59 copylogdb�� ���� ��忡�� ������ �� �����ϴ�.\n
60 \
copylogdb: �������� �α� ������ ����\n\
����: %1$s copylogdb [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
valid options:\n\
  -L, --log-path=PATH         ����� �α� ������ ���� ���͸� PATH\n\
  -m, --mode=MODE             ���� ���; �⺻��: sync\n\
                              MODE: sync or semisync or async\n\
  -S, --start-page-id=ID      ���� ���� �α� ������\n

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 DBA ��ȣ �Է�:
22 ������ HA ���� �������� �ʾҽ��ϴ�.\n
58 HA ����� �������� �ʴ� �÷����Դϴ�.\n
59 applylogdb�� ���� ��忡�� ������ �� �����ϴ�.\n
60 \
applylogdb: ����� �α׸� ������ �ݿ�\n\
����: %1$s applylogdb [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
valid options:\n\
  -L, --log-path=PATH         ����� �α� ������ ���͸� PATH\n\
      --max-mem-size=SIZE     MB ������ �ִ� ���μ��� ũ��(SIZE); �ִ밪 500\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 ��� ���� '%1$s'�� �� �� �����ϴ�.\n
59 statdump�� ���� ��忡�� ������ �� �����ϴ�.\n
60 \
statdump: ���� ��踦 ���.\n\
����: %1$s statdump [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
valid options:\n\
  -o, --output-file=FILE       ��� �޽����� �������� ����; �⺻��: ����\n\
  -c, --cumulative             �������� ���\n\
  -i, --interval=S             �� S �ʸ��� ��踦 ���\n\
  -s, --substr=STRING          ������ ���ڿ��� ���Ե� �׸� ���\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 DBA ��ȣ �Է�:
22 ������ HA ���� �������� �ʾҽ��ϴ�.\n
57 -L �ɼ��� ��ΰ� �߸��Ǿ��ų� db_ha_apply_info īŻ�α� ���̺��� ���� ������ ���� ������ ���� �ݿ� ������ ������ �� �����ϴ�.\n
58 HA ����� �������� �ʴ� �÷����Դϴ�.\n
59 applyinfo�� ���� ��忡�� ������ �� �����ϴ�.\n
60 \
applyinfo: display CUBRID HA ���� ���� ���\n\
����: %1$s applyinfo [�ɼ�] database-name\n\
\n\
valid options:\n\
  -r, --remote-host-name      ���� ȣ��Ʈ �̸�; ���� ����� Ȱ�� �α� ���� ���\n\
  -L, --copied-log-path=PATH  ����� �α� ������ ���; ����� �α� ���� ���\n\
  -a, --applied-info          �ݿ��� ���� ���; L�ɼ��� �ʿ�\n\
  -p, --page=ID               ������ id; L�ɼ��� �ʿ�; �⺻��: 0\n\
  -v, --verbose               �� ���� ���� ���; �⺻��: ������� ����\n\
  -i, --interval=S            �� S �ʸ��� �α� ���� ���\n

$set 45 MSGCAT_UTIL_SET_ACLDB
59  acldb�� ���� ��忡�� ������ �� �����ϴ�.\n
60 \
acldb: ������ ���ٱ��� ����� ����ϰų� �ٽ� �н��ϴ�.\n\
����: %1$s acldb [�ɼ�] <�����ͺ��̽� �̸�>\n\
\n\
valid options:\n\
  -r, --reload       ���� ���� ���� �ٽ� �б�.\n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
genlocale: LDML �Է� ������ ����ϴ� LDML ���� ���̺귯���� ����\n\
����: %1$s genlocale [�ɼ�] [language-string]\n\
\n\
language-string	      ���� ���̺귯���� ������ ��Ķ ����� ���ڿ�.\n\
		      language-string�� �����Ǹ� 'cubrid_locales.txt'�� ���ǵ� ��� �� ���� �����մϴ�.\n\
valid options:\n\
  -v, --verbose	              �� ���� ���; �⺻��: ��� �� ��\n\
  -i, --input-ldml-file       �Է� LDML ���Ͽ� ���� ���. language-string�� �־��� ���� ���.\n
61 ��ȿ���� ���� ��Ķ ����.\n

$set 47 MSGCAT_UTIL_SET_DUMPLOCALE
57 -i/--input-file�� ��Ķ �ĺ��ڸ� ���ÿ� ����� �� �����ϴ�.\n
58 ��ȿ���� ���� �ڵ�����Ʈ ����(-s -e).\n
59 ��ȿ���� ���� ��Ķ ����.\n
60 \
dumplocale: LDML �Է� ������ ����Ͽ� �̸� ������ ��Ķ ���� ���̺귯���� ������ ����\n\
����: %1$s dumplocale [�ɼ�] [language-string]\n\
\n\
language-string	 ��Ķ ���� ���̺귯���� ���� ������ ��Ķ ��� ���ڿ�\n\
		         no language-string�� �����Ǹ� 'cubrid_locales.txt'�� �ִ� ��� �� ���� �����մϴ�.\n\
valid options:\n\
  -i, --input-file    �Է��� ��Ķ ���� ���̺귯��(�̸� ������) ���Ͽ� ���� ���.\n\
                      language-string�� ������ ���� ��� ����.\n\
  -d, --calendar      Ķ������ ��¥/�ð� ���� ����; �⺻��: ��� �� ��\n\
  -n, --numeric		  ���� ���� ����; �⺻��: ��� �� ��\n\
  -a, --alphabet=l|lower|u|upper|both	     ���ĺ��� ��ҹ��� ���� ����; �⺻��: ��� �� ��\n\
      --identifier-alphabet=l|lower|u|upper	 ���ĺ��� �ĺ��ڿ� ���� ��ҹ��� ���� ����; �⺻��: ��� �� ��\n\
  -c, --codepoint-order	 �ڵ�����Ʈ ���� ������� ������ �ݷ��̼� ������ ����\n\
                         (cp, char, weight, next-cp, char and weight);\n\
                         �⺻��: ��� �� ��\n\
  -w, --weight-order  ����ġ ���� ������� ������ �ݷ��̼� ������ ����\n\
                      (weight, cp, char); �⺻��: ��� �� ��\n\
  -s, --start-value	  ���͸� �ɼ�; �Ʒ� �ɼǵ鿡 ���� ���� �ڵ�����Ʈ\n\
                      -a, --identifier-alphabet, -c, -w; default : 0\n\
  -e, --end-value     ���͸� �ɼ�; �Ʒ� �ɼǵ鿡 ���� �� �ڵ� ����Ʈ\n\
                      -a, --identifier-alphabet, -c, -w; \n\
                      �⺻��: ��Ķ ���� ���̺귯������ �о���� �ִ� ��\n\
  -k, --console-conversion  �ܼ� ��ȯ �����͸� ����; �⺻��: ��� �� ��\n\
  -z, --normalization   ����ȭ �����͸� ����; �⺻��: ��� �� ��\n

$set 48 MSGCAT_UTIL_SET_SYNCCOLLDB
43 ��Ƽ�� ���� �÷��� �ݷ��̼� "%1$s"�� �����ϱ� ���� ��Ƽ���� �����ؾ� �մϴ�.\nŬ����\n
44 �ݷ��̼��� ������Ʈ �ϱ� ���� �ݷ��̼� "%1$s"�� ������ �÷��� �ܷ�Ű�� �����ؾ� �մϴ�.\n���̺� | �ܷ�Ű\n
45 �ݷ��̼� "%1$s"�� ���� Ŭ������ ������ �ؾ� �մϴ�.\nŬ����\n
46 �ݷ��̼� "%1$s"�� ������ ǥ������ ������ �Լ� �ε����� ������ �ؾ� �մϴ�.\n���̺� | �ε��� | �Լ� expression\n
47 �ݷ��̼� ������Ʈ�� ������� �ʾҽ��ϴ�. �����ͺ��̽��� ������� �ʾҽ��ϴ�.\n
48 �ý��� ���̺� "%1$s"�� ���ο� �ݷ��̼����� ������Ʈ�Ǿ����ϴ�.\n
49 �ý��� �ݷ��̼��� ������Ʈ�ϸ� �����ͺ��̽��� �ջ�� �� �ֽ��ϴ�. ��� �Ͻðڽ��ϱ�? (y/n)\n
50 ���ο� �ý��� �������� �����ͺ��̽��� �ݷ��̼� "%1$s"(Id: %2$d)�� �������� �ʰų� ����Ǿ����ϴ�.\n
51 �ݷ��̼� "%1$s"�� �����ϴ� ������ ���� Ʈ���Ŵ� ������ �ؾ� �մϴ�.\nƮ���� | ����\n
52 �ݷ��̼� "%1$s"�� �����ϴ� ��� ������ �ؾ� �մϴ�.\n�� | ���ǽ���\n
53 �ݷ��̼� "%1$s"�� ���� ��Ʈ����Ʈ �������� ������ �ؾ� �մϴ�.\n���̺� | ��Ʈ����Ʈ ������\n
54 ������ �˻簡 �ʿ��մϴ�.\n\
�����ͺ��̽��� �ݷ��̼��� ������ �����ϱ� ���� "%1$s" ������ �˻��Ͻñ� �ٶ��ϴ�.\n
55 �ݷ��̼� ������Ʈ�� �ʿ����� �ʽ��ϴ�.\n
56 �����ͺ��̽��� �ý��� �ݷ��̼����� ������Ʈ �Ǿ�� �մϴ�.\n\
"cubrid synccolldb -f %1$s"���� ���� �Ͻʽÿ�.\n
57 �����ͺ��̽��� ���� �ý��� �������� %1$d���� ���ο� �ݷ��̼��� �ֽ��ϴ�.\n
58 �����ͺ��̽� "%1$s"�� ��� �ݷ��̼��� ���ο� �ý��� ���� �� �����մϴ�.\n
59 �����ͺ��̽��� %1$d���� �ݷ��̼��� �������� �ʾҰų� ���ο� �ý��� �ݷ��̼����� ����Ǿ����ϴ�.\n
60 \
synccolldb: �����ͺ��̽� �ݷ��̼��� �ý��� ������ ���ǵ� �ݷ��̼ǿ� �°� ������Ʈ �մϴ�.\n\
����: %1$s synccolldb [�ɼ�] �����ͺ��̽�-�̸�\n\
\n\
valid options:\n\
  -c, --check-only    �����ͺ��̽��� �ݷ��̼ǰ� �ý��� �ݷ��̼��� �˻��մϴ�.\n\
		      ���� �ݷ��̼� ������Ʈ�� �ʿ��ϸ� �����ڵ� 1�� ���ϵ˴ϴ�.\n\
		      ����, ȣȯ���� �ʴ� �ݷ��̼��� ����մϴ�.\n\
  -f, --force-only    �����ͺ��̽��� �ݷ��̼��� ���ο� �ý��� ������ �ݷ��̼����� ���� ������Ʈ �մϴ�.\n\

$set 49 MSGCAT_UTIL_SET_TRANLIST
20 ����� %1$s�� ��й�ȣ�� �Է��ϼ��� :
22 Ʈ������ε���        ������̸�     ȣ��Ʈ�̸�     ���μ��� ID     ���α׷��̸�\n
23 ---------------------------------------------------------------------------------\n
24 %1$4d%2$-12s %3$14s %4$14s %5$15d %6$17s\n
25 Ʈ������� �����ϴ�\n
26 tranlist�� DBA �Ǵ� DBA �׷��� ����� ���ؼ��� ����� �� �ֽ��ϴ�.\n
27 ��ȿ���� ���� �÷���ȣ(%1$d) �Դϴ�.\n
32 Ʈ������ε���        ���μ��� ID     ���α׷��̸�         ���Ǽ���ð�  Ʈ����ǽð�             ������� �� ������      ���ǹ� ID     ���ǹ�\n
33 %1$4d%2$-12s %3$14d    %4$-25s %5$10.2f %6$12.2f    %7$28s     %8$13s   %9$s\n
34 ----------------------------------------------------------------------------------------------------------------------------------------------------\n
42 Ʈ������ε���        ������̸�     ȣ��Ʈ�̸�     ���μ��� ID     ���α׷��̸�         ���Ǽ���ð�  Ʈ����ǽð�             ������� �� ������      ���ǹ� ID     ���ǹ�\n
43 %1$4d%2$-12s %3$14s %4$14s %5$14d    %6$-25s %7$10.2f %8$12.2f    %9$28s     %10$13s   %11$s\n
44 -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 Ʈ����� �ε��� : %1$d
46 ���ǹ� ID : %1$s\n
59 tranlist�� ���� ��忡�� ������ �� �����ϴ�.\n
60 \
tranlist: Ȱ��ȭ�� Ʈ����� ���� ���.\n\
����: %1$s tranlist [�ɼ�] database-name\n\
\n\
valid options:\n\
  -s, --summary                �������� �� �� ���� ����\n\
      --sort-key=COLUMN_NUMBER ������ �÷���ȣ; �⺻�� : 1\n\
      --reverse                �������� ���ļ���; �⺻�� : �������� ����\n

$set 51 MSGCAT_UTIL_SET_GEN_TZ
59 ��ȿ���� ���� ���� ��尡 �����Ǿ����ϴ�.\n
60 ��ȿ���� �ʰų� ������ �Է� �����Դϴ�.\n
61 \
gen_tz: Ÿ���� �����͸� �����ϴ� �ҽ� ������ �����Ͽ� ���� ���̺귯���� ������\n\
����: %s gen_tz -g [new|extend] [database-name] (extend ��忡 ���ؼ���)\n\
\n\
valid options:\n\
  -i, --input-folder    IANA Ÿ���� �����ͺ��̽� ������ �����ϴ� �Է� ���͸� ���\n\
                        �⺻��: $CUBRID/timezones/tzdata\n\
  -g, --gen-mode=TYPE   TYPE�� ���� gen_tz�� ����� ����:\n\
                        TYPE:\n\
                        1. NEW (�⺻��) : ���� ������ ���� �������� ���� ���̺귯���� ���� �ҽ� ���� ����;\n\
                        ���� ��� ������ Ÿ���� �̸��� �����ϵ��� �ҽ� ������ ����\n\
                        (�����ڰ� ����ϴ� ���� �ǵ�, ���� CUBRID ������ ���� ����).\n\
                        2. EXTEND : �Է� ������ �����͸� ����Ͽ� Ÿ���� �ҽ� ������ ����;\n\
                        �Է� ������ ���ο� Ÿ�������� �߰�; GMT �����°� �ϱ� ���� �ð��� \n\
                        ���� ������ ���ŵǰų� ���ο� Ÿ������ ���� �߰���;\n\
                        ������ Ÿ���� ���̺귯���� ���Ե� Ÿ���� �ĺ��ڴ� �� �Է� ���Ͽ�\n\
                        �� �̻� �������� �ʴ��� �������� �ʰ� �׻� ������;\n\
                        ��� �����Ϳ� ���� ���� ȣȯ�� ������ �� ���� ��쿡�� �ش� �����͸�\n\
                        �����ϴ� �����ͺ��̽��� ��� �÷��� ���ؼ� ������Ʈ�� ����.\n

$set 52 MSGCAT_UTIL_SET_DUMP_TZ
59 ��ȿ���� ���� Ÿ���� �ĺ��ڰ� �־������ϴ�. ��ü Ÿ������ �ش� �ĺ��ڸ� Ȯ���ϱ� ���ؼ� -z�� ����ϼ���\n
60 \
dump_tz: Ÿ���� ���� ������ ���\n\
usage: %s dump_tz [�ɼ�]\n\
\n\
valid options:\n\
  -c, --list-countries      ���� �����ϴ� ������ ����Ʈ�� ���\n\
  -l, --leap-seconds	    ���� ����Ʈ�� ���\n\
  -s, --summary		    Ÿ���� �����Ϳ� ���� ��� ���� ���\n\
  -z, --list-zones          ���� �����ϴ� Ÿ���� ����Ʈ�� ���\n\
  -d, --zone-id=ZONE_ID|ALL �־��� ZONE_ID�� ������ Ÿ������ ���õ� ��� ������ Ÿ���� ���� �Ǵ� \n\
                            ��� Ÿ���� ������ ���.\n\
                            Ÿ������ �ĺ��ڴ� -z �ɼ��� ����ϴ� ����Ʈ�� ���ؼ� Ȯ���� �� ����.\n

$set 53 MSGCAT_UTIL_SET_RESTORESLAVE
20 Restoreslave ������ ��ҵǰų� ������ �߻��߽��ϴ�.\n
21 �����ͺ��̽� ������ �Ϸ�Ǿ�����, ���� ���� �ʱ�ȭ �����߽��ϴ�. (�����ͺ��̽� ���� �ð�='%1$s', ������=%2$lld, ������=%3$d)\n
22 �����ͺ��̽��� ȣ��Ʈ�� HA �����Ǿ� ���� �ʽ��ϴ�.\n
23 ��ȿ���� ���� ��� �ҽ� ��� �� '%1$s' �Դϴ�.\n
60 \
restoreslave: �����̺� �����ͺ��̽� ����.\n\
usage: %1$s restoreslave [�ɼ�] database-name\n\
\n\
valid options:\n\
  -s, --source-state=STATE          ��� �ҽ� ��� ����; �ʼ�\n\
                                    STATE�� 'master', 'slave' �Ǵ� 'replica'\n\
  -m, --master-host-name=NAME       ���� ������ ����� ȣ��Ʈ �̸�; �ʼ�\n\
      --list                        ���� �������� �ʰ� ��� ������ ����Ʈ�� ���\n\
  -B, --backup-file-path=PATH       ������ ��� ������ ���͸� ���\n\
  -o, --output-file=FILE            ��� �޽����� FILE�� ����; �⺻��: ����\n\
  -u, --use-database-location-path  �����ͺ��̽��� �α� ������ �����ͺ��̽� ��� ���Ͽ� ���õ� ��η� ����\n\
  -k, --keys-file-path              ������ ���� Ű ���� (_keys)�� ��� \n

$set 54 MSGCAT_UTIL_SET_DELVOLDB
21 Ready to remove the following volumes:\n\n
22 Do you wish to proceed ? (Y/N)
31 Cannot remove system volume or the first volume (Volid : %1$d).\n
32 Cannot find the volume (Volid : %1$d).\n
33 There are too many Volids (Max Volids : %1$d).\n
34 Invalid Volid(%1$s).\n
60 \
delvoldb: Remove a empty volume-file of the database.\n\
usage: %1$s delvoldb [OPTION] database-name -i Volid\n\
\n\
valid options:\n\
  -c, --clear-cache                clear all cached files in the temporary temp volume\n\
  -f, --force                      remove the volume without a prompt for verification\n\
  -p, --dba-password=PASS          password of the DBA user; will prompt if don't specify\n\
  -S, --SA-mode                    stand-alone mode execution\n\
  -C, --CS-mode                    client-server mode execution\n

$set 55 MSGCAT_UTIL_SET_VACUUMDB
20 Vacuumdb�� ���� Ŭ���̾�Ʈ ���� ��忡�� �������� �ʽ��ϴ�.\n
21 vacuum �۾� ����\n
22 ��� ���� '%1$s'�� �� �� �����ϴ�.\n
60 \
vacuumdb: ������ �ʴ� ���ڵ带 �����ͺ��̽����� ����\n\
usage: %1$s vacuumdb [�ɼ�] database-name\n\
\n\
valid options:\n\
  -S, --SA-mode                ���� ��� ����\n\
  -C, --CS-mode                Ŭ���̾�Ʈ ���� ��� ����\n\
      --dump                   ���� vacuum ���� ����\n\
  -o, --output-file=FILE       ��� �޽����� �������� ����; �⺻��: ����\n

$set 56 MSGCAT_UTIL_SET_CHECKSUMDB
1 %1$s���κ��� ���̺� �̸� �ε� ����.\n
2 checksumdb�� �ݵ�� HA active �������� �����ؾ� �մϴ�. %1$s (���� ����: %2$s)\n
3 üũ�� ����� ������ ���̺��� �����ڴ� DBA���� �մϴ�.\n
58 HA ����� �������� �ʴ� �÷����Դϴ�.\n
59 checksumdb�� ���� ���� ������ �� �����ϴ�.\n
60 \
checksumdb: ���� �ϰ��� �˻�\n\
usage: %1$s checksumdb [�ɼ�] database-name@hostname\n\
\n\
valid options:\n\
  -c, --chunk-size=NUMBER       üũ�� ��� ������ �� ���ڵ� ��\n\
                                �⺻��: 500 ���ڵ�, �ּ�: 100 ���ڵ�\n\
  -s, --sleep=NUMBER            üũ�� ��� ���� sleep �ð�; �⺻��: 100 ms\n\
  -i, --include-class-file=FILE ���Խ�ų ���̺��� �����ϴ� �Է� ����\n\
  -e, --exclude-class-file=FILE ���ܽ�ų ���̺��� �����ϴ� �Է� ����; -i �Ǵ� -e �ϳ��� ���� ����, ���� ���� �Ұ�\n\
  -t, --timeout=NUMBER          �и��� ���� ���� Ÿ�� �ƿ�; �⺻��: 1000 ms\n\
  -n, --table-name=NAME         üũ�� ����� ������ ���̺� �̸�; �⺻��: dba.db_ha_checksum\n\
  -r, --report-only             üũ�� ����� ����Ͽ� ���� �ϰ��� ���� ����\n\
                                --table-name �ɼ� �ܿ� �ٸ� �ɼ��� ��� ���õ�\n\
      --resume                  üũ�� ��� ����\n\
      --schema-only             ��Ű�� �ϰ����� �˻�\n\
      --cont-on-error           ��� ���� �߻��ص� ��� ����\n

$set 57 MSGCAT_UTIL_SET_TDE
21 DBA ��� ��ȣ:
25 �����ͺ��̽��� ��ϵ� Ű ������ ������ �� �����ϴ�.\n
26 Ű (�ε���: %1$d)���� Ű (�ε���: %2$d)�� ������ �õ��մϴ�..\n
27 Ű (�ε���: %1$d)���� Ű (�ε���: %2$d)�� ����Ǿ����ϴ�.\n
28 �����ͺ��̽��� ��ϵ� Ű�� ������ �� �����ϴ�.\n
29 �ε���: %1$d Ű�� �����Ͽ����ϴ�.\n
30 ���ο� Ű�� �����Ǿ����ϴ�. Ű �ε���: %1$d, ���� �ð�: %2$s
60 \
tde: Transparent Data Encryption (TDE) ����\n\
usage: %1$s tde <operation> [�ɼ�] <�����ͺ��̽�-�̸�>\n\
\n\
valid options:\n\
    -p, --dba-password=PASS       DBA �н�����; �������� ���� ��� ������Ʈ\n\
\n\
operations:\n\
    -s, --show-keys               �����ͺ��̽��� ��ϵ� Ű, Ű ���� (_keys)���� Ű�鿡 ���� ������ ��� \n\
    -n, --generate-new-key        Ű ���� (_keys)�� ���ο� Ű�� �߰�; �ִ� ����: 128\n\
    -d, --delete-key=KEY_INDEX    Ű ���� (_keys)�� �� Ű�� ����\n\
    -c, --change-key=KEY_INDEX    �����ͺ��̽��� ��ϵ� Ű�� Ű ���� (_keys)�� �����ϴ� �ٸ� Ű�� ����\n

$set 58 MSGCAT_UTIL_SET_FLASHBACK
1 DBA ��й�ȣ:
2 �߸��� ��¥ �����Դϴ�. �ùٸ� ������ dd-mm-yyyy:hh:mm:ss�Դϴ�.\n
3 ���� ��¥(%1$s)�� ���� ��¥(%2$s)���� �۾ƾ� �մϴ�.\n
4 ���� ��¥ ����(%1$s) �Ǵ� �����ͺ��̽� ���� ����(%2$s) ��¥�� �õ��Ͻʽÿ�.\n
5 ���̺�(%1$s)�� �����ͺ��̽��� �����ϴ�.\n
6 �÷��ù��� Ʈ������� �ʹ� �����ϴ�. ���� ��¥�� ���� ��¥ ������ ������ ���̽ʽÿ�.\n
7 ��Ű��(%1$s)�� ����Ǿ��� ������ �÷��ù��� ������ �� �����ϴ�.\n
8 �α� ������ ���ŵǾ����ϴ�. ���� ��¥�� �õ��Ͻʽÿ�.\n
9 ��� ���� '%1$s'�� �� �� �����ϴ�.\n
10 Ʈ����� ID(%1$d)�� �������� �ʽ��ϴ�.\n
11 'cubrid.conf'�� 'supplemental_log'�� ������ �ּ���. 'supplemental_log'�� ������ �� ����� SQL �α׸� �÷��ù��� �� �ֽ��ϴ�.\n
12 Ʈ����� ID�� %1$d�� �ȿ� �Է��ؾ� �մϴ�. 'cubrid.conf'�� 'flashback_timeout' ������ ���� �ð� ���� ������ �� �ֽ��ϴ�. \n
13 �÷��ù��� �̹� ���� ���Դϴ�. �÷��ù��� �� �� �̻��� ���μ������� ������ �� �����ϴ�.\n
14 �÷��ù��� ���� ��忡�� ������ �� �����ϴ�.\n
15 �ý��� īŻ�α� Ŭ����(%1$s)�� ���Ͽ� �÷��ù��� �������� �ʽ��ϴ�.\n
60 \
flashback: Ư�� Ʈ������� �÷��ù��ϴ� ���ɹ��� �����մϴ�.\n\
����: %1$s flashback [OPTION] �����ͺ��̽� �̸� owner_name.class_name1 [owner_name.class_name2 ...]\n\
\n\
��ȿ�� �ɼ�:\n\
    -o, --output-file=FILE      ��� �޽����� FILE�� ���𷺼��մϴ�. �⺻��: ����.\n\
    -u, --user=ID               �÷��ù��� SQL ���� ������ ����ڸ� �����մϴ�. �⺻��: ���.\n\
    -p, --dba-password          DBA ������� ��й�ȣ; �������� ������ �޽����� ǥ�õ˴ϴ�.\n\
    -s, --start-date=DATE       �÷��ù��� ���� ��¥�� �����մϴ�. DATE ���Ŀ� ����� SQL�αװ� ����˴ϴ�.\n\
                                DATE�� ������ ���� �����˴ϴ�. dd-mm-yyyy:hh:mm:ss.\n\
    -e, --end-date=DATE         �÷��ù��� ���� ��¥�� �����մϴ�. DATE���� ����� SQL�αװ� ����˴ϴ�.\n\
                                ���� ��¥�� �������� ������ ((���� - 10��) ~ ����) ������ �����˴ϴ�.\n\
                                ���� ��¥�� �������� ���� ��� (���� ~ (���� + 10��)) ������ �����˴ϴ�.\n\
                                ���� ��¥�� ���� ��¥ ��� �������� ���� ���,\n\
                                (���� �ð� - 10��)�� ���� �ð� ���̿� ����� SQL �α׸� �����մϴ�.\n\
    --detail                    �� SQL ���� ���� �ڼ��� ������ ����մϴ�.\n\
                                Ʈ����� ID, ����� �̸� �� ����ڰ� ������ SQL ��, �÷������� ���� SQL ���� ��µ˴ϴ�;\n\
                                �⺻��: �÷��ù��� ���� SQL ���� ���\n\
    --oldest                    ���� �ð����� ���� �ð����� ����� SQL���� ���Ͽ� �ð� ������ �÷��ù��� �����մϴ�;\n\
                                �⺻��: �ֱٿ� ����� SQL������ �������� �÷��ù��� �����մϴ�.\n
