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
1 크기가 잘못되었거나, 잘못된 문자 '%1$c'이(가) 데이터베이스 이름 '%2$s'에 포함되어 있습니다\n
2 출력 파일 '%1$s'을(를) 열 수 없습니다.\n
3 유틸리티 이름이 빠져 있거나 혹은 잘못 되어 있습니다.\n
4 잘못된 유틸리티 이름 '%1$s'\n
5 다음 리스트는 올바른 유틸리티 이름입니다:\n
6 잘못된 문자 '%1$c'이(가) 볼륨 이름 '%2$s'에 포함되어 있습니다.\n
9 \
%1$s (CUBRID 유틸리티)\n\
%2$s\n\
Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n
10 \
cubrid 관리자 유틸리티, 버전 %1$s\n\
사용법: %2$s <유틸리티 이름> [옵션]\n\
각 유틸리티에 대한 도움말을 원하면 '%3$s <유틸리티 이름>'  명령을 수행 하세요.\n\
\n\
가용한 유틸리티 이름:\n\
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
%4$s (은)는 DBMS 관리 도구입니다.\n\
추가적인 정보는 다음 사이트를 참조하세요. http://www.cubrid.org\n
12 '%1$s' 는 없는 유틸리티 이름입니다.\n'cubrid --help' 명령어로 사용법을 알아보세요.\n
13 '%1$s' 는 잘못된 명령어입니다.\n'cubrid --help' 명령어로 사용법을 알아보세요.\n
14 설정 파일에서 하나 이상의 필요한 속성을 읽어 들일 수 없습니다.\n
15 @ %1$s %2$s: %3$s\n
16 @ %1$s %2$s\n
17 ++ %1$s '%2$s' (이)가 수행되고 있지 않습니다.\n
18 ++ %1$s (이)가 수행되고 있지 않습니다.\n
19 ++ %1$s '%2$s' (이)가 수행되고 있습니다.\n
20 ++ %1$s (이)가 수행되고 있습니다.\n
21 ++ %1$s %2$s: %3$s\n
22 인자가 부족합니다. \n'cubrid --help' 명령어로 사용법을 알아보세요.\n
23 cubrid 유틸리티, 버전 %1$s\n\
사용법: %2$s <유틸리티 이름> [인자]\n\
'%3$s <utility-name>' 명령어로 각 유틸리티에 대한 도움말을 보실 수 있습니다.\n\
\n\
가용한 서비스 유틸리티 이름:\n\
    service\n\
    server\n\
    broker\n\
    manager\n\
    heartbeat\n\
\n\
가용한 관리자 유틸리티 이름:\n\
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
%4$s (은)는 DBMS  도구입니다.\n\
추가적인 정보는 다음 사이트를 참조하세요. http://www.cubrid.org\n
24 cubrid 유틸리티, 버전 %1$s\n\
사용법: %2$s service <명령어>\n\
\n\
가용한 명령어:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 cubrid 유틸리티, 버전 %1$s\n\
사용법: %2$s server <명령어> [인자]\n\
\n\
가용한 명령어:\n\
    start       [데이터베이스 이름]\n\
    stop        [데이터베이스 이름]\n\
    restart     [데이터베이스 이름]\n\
    status\n\
    acl         <status|reload> 데이터베이스 이름\n
37 서버가 HA 모드로 설정되어 있습니다.\n
26 cubrid 유틸리티, 버전 %1$s\n\
사용법: %2$s broker <명령어> [인자]\n\
\n\
가용한 명령어:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [-S] [-P] [-m] [-c] [<expr>]\n\
    on          <브로커 이름>\n\
    off         <브로커 이름>\n\
    reset       <브로커 이름>\n\
    acl         <status|reload> [브로커 이름]\n\
    getid       -b <브로커 이름> [-f] shard-key\n\
    test        <브로커 이름> [-D <database_name>] [-u <user_name>] [-p <user_password>] [-c <SQL_command>] [-i <input_file>] [-o <output_file>] [-v] [-s]\n
27 cubrid 유틸리티, 버전 %1$s\n\
사용법: %2$s manager <명령어>\n\
\n\
가용한 명령어:\n\
    start\n\
    stop\n\
    status\n
28 cubrid 유틸리티, 버전 %1$s\n\
사용법: %2$s heartbeat <명령어> [인자]\n\
\n\
가용한 명령어:\n\
    start         [데이터베이스 이름]\n\    
    stop          [-h <호스트 이름>] [-i] [데이터베이스 이름]\n\
    copylogdb     <start|stop> <데이터베이스 이름> <노드 이름>\n\
    applylogdb    <start|stop> <데이터베이스 이름> <노드 이름>\n\
    prfetchlogdb  <start|stop> <데이터베이스 이름> <노드 이름>\n\
    replication   <start|stop> <노드 이름>\n\
    status         [-v]\n\
    reload\n
31 '%1$s' 인자가 필요 없습니다.\n
32 데이터베이스 이름이 누락됐습니다.\n
33 %1$s: 이 옵션은 사용 중지 될 것입니다.\n
34 설정에 오류가 있습니다. %1$s %2$s\n
35 메모리 할당에 실패하였습니다.\n
36 서버가 HA 모드로 설정되지 않았습니다.\n
38 서버가 HA 모드로 설정되어 있으나, ha_db_list 목록에는 설정되어 있지 않습니다.\n
39 서버가 HA 모드로 설정되어 있으나, ha_node_list 목록에는 설정되어 있지 않습니다.\n
40 잘못된 명령어 입니다.\n
41 CUBRID 메니저가 설치되지 않았습니다.\n
42 인자가 잘못되었습니다.\n
43 파일(%1$s)을 열지 못했습니다.\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 경고: -t 옵션은 독립 실행 모드에서는 무시 됩니다.\n
31 에러: 대상 경로가 존재하지 않거나 디렉토리가 아닙니다.\n
60 \
backupdb: 데이터베이스를 백업합니다.\n\
사용법: %1$s backupdb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -D, --destination-path=<백업> 볼륨이 저장될 디렉터리 경로. 지정되지 않은 경우에는 로그 디렉터리.\n\
  -r, --remove-archive         불필요한 로그 파일을 지움. 유의해서 사용해야 합니다. 관리자 매뉴얼을 참고하세요.\n\
  -l, --level=LEVEL            백업 수준. 기본 수준은 전체입니다.\n\
                               다음과 같은 백업 수준을 지정할 수 있습니다.\n\
                                   0 - 전체\n\
                                   1 - 증분 1\n\
                                   2 - 증분 2\n\
  -o, --output-file=<파일> 자세한 백업 메시지를 파일에 출력합니다.\n\
  -S, --SA-mode                독립 모드 실행\n\
  -C, --CS-mode                클라이언트 서버 모드 실행\n\
      --no-check               데이터베이스의 정합성 확인 작업을 수행하지 않습니다; 기본값: 수행\n\
  -t, --thread-count=COUNT     백업을 수행하는 스레드 개수; 기본값: 자동\n\
  -z, --compress               백업 볼륨을 압축합니다; 기본값: 압축하지 않음\n\
  -e, --except-active-log      백업 볼륨에 활동 로그를 포함하지 않습니다; 기본값 : 포함\n\
      --sleep-msecs=N          매 1M 읽기 마다 N 밀리초를 쉽니다; 기본값: 0\n
71 \"%1$s\" 와 \"%2$s\" (은)는 함께 사용할 수 없습니다.\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 에러: 대상 이름이 원본 이름과 동일합니다.\n
31 에러: extended-volume-path 가 존재하지 않거나 쓰기 금지되어 있습니다.\n
32 에러: control-file이 없거나 읽을 수 없습니다.\n
60 \
copydb: 데이터베이스 복사.\n\
사용법: %1$s copydb [옵션] <원본 데이터베이스 이름> <대상 데이터베이스 이름>\n\
\n\
옵션:\n\
      --server-name=HOST          서버 호스트 이름; 기본값: 현재 호스트 이름\n\
  -F, --file-path=PATH            데이터베이스가 저장되는 디렉터리 경로; 기본값: 현재 디렉터리\n\
  -L, --log-path=PATH             로그 볼륨이 저장되는 디렉터리 경로; 기본값: <file-path>\n\
  -B, --lob-base-path=PATH        LOB 파일이 저장되는 디렉토리 경로; 기본값: <file-path>/lob\n\
  -E, --extended-volume-path=PATH 확장 볼륨이 저장되는 디렉터리 경로; 기본값: 데이터베이스 저장 디렉터리 경로 또는 control-file에 주어진 경로\n\
  -i, --control-file=FILE         여러 개의 볼륨들이 각각 저장되는 디렉터리 경로를 지정하는 제어 파일; 기본값: 없음\n\
  -r, --replace                   같은 이름의 데이터베이스가 존재하면 덮어쓰기; 기본값: 덮어쓰기 안함\n\
  -d, --delete-source             복사 후 원본 데이터베이스 삭제; 기본값: 삭제 안함\n\
      --copy-lob-path             원본 데이터베이스의 LOB 파일 디렉터리 경로를 복사. -B 옵션과 같이 사용할 수 없음; 기본값: 복사 안함\n


$set 4 MSGCAT_UTIL_SET_CREATEDB
40 그룹 '%1$s'는 존재하지 않습니다\n
41 사용자 '%1$s'는 존재하지 않습니다\n 
42 알 수 없는 명령어 '%1$s'\n
43 출력 파일 '%1$s' 을(를) 열 수 없습니다.\n
44 기존 데이터베이스를 삭제할 수 없습니다.\n
45 %1$s 크기의 데이터베이스를 로케일 "%2$s"으로 생성합니다. 필요한 디스크의 용량은 %3$s 입니다.\n
46 데이터베이스를 생성할 수 없습니다.\n
47 user file '%1$s'에 오류가 있습니다.\n
48 The given '%1$s', %2$s is out of range. Choose a size between %3$s and %4$s.\n
49 %1$s의 값 '%2$s'은 유효하지 않습니다.\n
60 \
createdb: 데이터베이스 생성.\n\
사용법: %1$s createdb [옵션] <데이터베이스 이름> <데이터베이스 로케일>\n\
\n\
<데이터베이스 로케일>의 형식은 <language>.<charset> 입니다. 예: en_US.iso88591, ko_KR.utf8\n\
옵션:\n\
      --db-volume-size=SIZE           처음 생성되는 볼륨(generic)의 크기\n\
                                      기본값: cubrid.conf의 db_volume_size의 값\n\
      --db-page-size=SIZE             바이트 단위의 데이터베이스 페이지 크기; 기본값: 16K\n\
      --log-volume-size=SIZE          로그 볼륨의 크기\n\
                                      기본값: cubrid.conf의 log_volume_size의 값\n\
      --log-page-size=SIZE            바이트 단위의 로그 페이지 크기; 기본값: 데이터베이스 페이지 크기\n\
      --comment=COMMENT               데이터베이스에 관한 설명; 기본값: 없음\n\
  -F, --file-path=PATH                데이터베이스가 생성되는 디렉터리 경로; 기본값: 현재 디렉터리\n\
  -L, --log-path=PATH                 로그 볼륨이 저장되는 디렉터리 경로; 기본값: <file-path>\n\
  -B, --lob-base-path=PATH            LOB 파일이 저장되는 디렉터리 경로; 기본값: <file-path>/lob\n\
      --server-name=HOST              서버 호스트 이름; 기본값: 현재 호스트\n\
  -r, --replace                       같은 이름의 데이터베이스가 존재하면 덮어쓰기; 기본값: 덮어쓰기 안함\n\
      --more-volume-file=FILE         추가로 확장될 볼륨에 대한 제어 파일\n\
      --user-definition-file=FILE     데이터베이스 사용자를 정의하는 파일; 기본값: 없음\n\
      --csql-initialization-file=FILE 데이터베이스 생성 후 CSQL에서 FILE에 저장된 질의문을 실행; 기본값: 없음\n\
  -o, --output-file=FILE              출력 메시지를 파일에 저장; 기본값: 없음\n\
  -v, --verbose                       상세한 상태 정보를 화면 출력; 기본값: 출력하지 않음\n


$set 6 MSGCAT_UTIL_SET_DELETEDB
60 \
deletedb: 데이터베이스 삭제.\n\
사용법: %1$s deletedb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -o, --output-file=FILE       출력 메시지가 저장될 파일; 기본값: 없음\n\
  -d, --delete-backup          백업 볼륨을 지움; 기본값: 지우지 않음\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 에러: extended-volume-path 가 존재하지 않거나 쓰기 금지되어 있습니다.\n
32 에러: control-file이 없거나 읽을 수 없습니다.\n
60 \
renamedb: 데이터베이스 이름 변경.\n\
사용법: %1$s renamedb [옵션] <원본 데이터베이스 이름> <대상 데이터베이스 이름>\n\
\n\
옵션:\n\
  -E, --extended-volume-path=PATH 확장 볼륨이 저장되는 디렉터리 경로; 기본값: 데이터베이스 저장 디렉터리 경로 또는 control-file에 주어진 경로\n\
  -i, --control-file=FILE         여러 개의 볼륨들이 각각 저장되는 디렉터리 경로를 지정하는 제어 파일; 기본값: 없음\n\
  -d, --delete-backup             백업 볼륨을 지움; 기본값: 지우지 않음\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 %1$s 프로세스가 이미 실행 중입니다\n
12 마스터 서버 시작\n
13 마스터 서버 종료\n
14 로컬 호스트 이름을 찾을 수 없습니다\n
15 인자 파일을 찾을 수 없습니다\n
16 마스터 프로세스를 초기화할 수 없습니다\n
17 서버 %1$s는 %2$d 분 내에 shutdown 합니다
18 서버 %1$s는 shutdown됩니다.\n이 작업은 몇 분 걸릴 수 있습니다. 잠시 기다리십시오.
19 서버 %1$s를 찾을 수 없습니다
20 마스터 서버는 %1$d 분 내에 shutdown 합니다\n
21 절체가 완료되었습니다. 모든 서버 프로세스들의 종료과정이 완료될때 까지 잠시 기다리십시오.\n

$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 출력 파일 '%1$s' 을(를) 열 수 없습니다\n
60 \
optimizedb: 데이터베이스 최적화.\n\
사용법: %1$s optimizedb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -n, --class-name=NAME        해당 클래스의 통계 정보를 갱신함; 기본값: 전체 클래스\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 날짜 형식 오류, 날짜 형식: 일-월-년:시:분:초(dd-mm-yyyy:hh:mm:ss) 또는 'backuptime'\n
20 복구 취소되었거나 에러 발생\n
60 \
restoredb: 데이터베이스 복구\n\
사용법: %1$s restoredb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -d, --up-to-date=DATE             주어진 시간으로 데이터베이스 상태를 복구. 날짜 형식: dd-mm-yyyy:hh:mm:ss 또는 'backuptime'\n\
      --list                        복구를 하지 않고 모든 백업 볼륨을 나열\n\
  -B, --backup-file-path=PATH       백업된 볼륨이 존재하는 디렉터리 경로\n\
  -l, --level=LEVEL                 복구에 사용될 백업의 레벨; 기본값: 0(Full backup)\n\
  -p, --partial-recovery            archive 로그가 존재하지 않으면 강제로 partial recovery 수행함\n\
  -o, --output-file=FILE            출력 메시지가 저장될 파일; 기본값: 없음\n\
  -u, --use-database-location-path  데이터베이스 위치 파일에 설정된 경로에 데이터베이스 볼륨과 로그 볼륨을 복구\n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 데이터베이스 이름이 누락되었습니다.\n
20 파일 '%1$s' 에 %2$d 만큼의 라인이 없습니다.\n
21 인터럽트... \n
22 로드가 인터럽트 됨 - 현재 트랜잭션은 커밋됩니다\n
23 로드가 인터럽트 됨 - 현재 트랜잭션은 철회됩니다\n
24 파일 '%1$s'를 열 수 없습니다.\n
25 \n*** 문법 검사 ***\n\n
26 %1$d 개의 에러가 검출되었습니다.\n
27 \n*** 객체 삽입 중 ***\n\n
28 총 %1$8d 개의 객체가 삽입되었습니다.\n
29 총 %1$8d 개의 참조된 객체가 기본 내용 값으로 입력되었습니다.\n
30 \n*** 트랜잭션 커밋 중 ***\n
31 \n*** 데이터베이스 닫는 중 ***\n
32 라인 %1$d:
33 '%4$s'의 속성 '%3$s'에 대한 '%1$s'을(를) '%2$s' 값으로 파싱 시 에러\n
34 %2$s의 속성 '%1$s'에 대한 도메인 에러\n
35 %2$s의 속성 %1$s에 대해 타입 %3$s은(는) set의 도메인으로 허용되지 않습니다.\n
36 %2$s의 속성 %1$s에 대해 타입 %3$s을(를) 예상했으나 set이 입력되었습니다.\n
37 %2$s의 속성 %1$s에 대해 타입 %3$s을(를) 예상했으나 %4$s이(가) 입력되었습니다.
38 존재하지 않는 클래스 %1$s이(가) %3$s의 속성 %2$s에 의해 참조되었습니다.
39 %1$s 클래스는 존재하지 않습니다. 튜플들은 무시되었습니다.\n
40 식별자 %1$d인 존재하지 않는 클래스는 %3$s의 속성 %2$s에 의해 참조되었습니다.\n
41 클래스 %1$s는 로더 입력 파일 내에서 허용되지 않습니다.\n
42 에러로 인한 중단.\n
43 경고: 기존 인스턴스를 갱신.\n
44 경고: 클래스 %2$s의 인스턴스 %1$d을(를) 재정의.\n
45 클래스 %2$s의 인스턴스 %1$d은(는) 이미 정의되었습니다.\n
46 클래스 %2$s의 인스턴스 %1$d은(는) 이미 사용되었습니다.\n
47 속성 '%1$s'상의 UNIQUE 위반, NULL로 변환 되었습니다.\n
48    %1$d 인스턴스\n
49 클래스 %1$s\n
50 패스워드: 
51 \n*** 클래스 통계 정보 갱신 중 ***\n
52 %1$s 에러가 라인 %2$d에서 발생('%3$s')\n
53 Lexical 오류
54 라인 %1$d: 문법 에러 at \"%2$s\"
55 missing 
56 in %1$s
57 함께 사용할 수 없는 인자 \"%1$s\", 와 \"%2$s\"\n
58 %1$d 인스턴스가 커밋 되었습니다\n
59 \n이전 버전의 loaddb를 수행할 수 없습니다.\n 
60 에러 parsing \"%1$s\", as %2$s type.\n
112   %1$d 개의 인스턴스\r
113 마지막으로 커밋된 라인 : %1$d
114 에러 제어 파일
115 로드 시에 에러 제어를 위한 입력 파일
116 총 %1$8d 개의 객체가 삽입되었으며, %2$d 개의 객체가 실패하였습니다.\n
117 로드 실패.\n
118 클래스 이름의 최대 길이는 %1$d 바이트 입니다.\n
120 \
loaddb: 데이터베이스에 객체 및 스키마 적재\n\
사용법: %1$s loaddb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -u, --user=ID                 데이터베이스 접근에 관한 사용자 이름; 기본값: "public"\n\
  -p, --password=PASS           사용자 패스워드; 기본값: 없음\n\
      --data-file-check-only    문법 체크만 수행; 기본값: 문법 체크 이후 적재\n\
  -l, --load-only               문법 체크하지 않고 적재; 기본값: 문법 체크 이후 적재\n\
      --estimated-size=NUMBER   인스턴스의 대략적인 개수; 기본값: 5000\n\
  -v, --verbose                 많은 상태 정보를 출력; 기본값: 출력하지 않음\n\
  -c, --periodic-commit=COUNT   주기적인 커밋에 대한 카운트; 기본값: 적재 중 커밋하지 않음\n\
      --no-oid                  OID 사용 안 함\n\
      --no-statistics           로딩 후에 통계 정보 갱신하지 않음\n\
  -s, --schema-file=FILE[:LINE] 파일[:라인] 정보로 스키마 생성\n\
  -i, --index-file=FILE[:LINE]  파일[:라인] 정보로 인덱스 생성\n\
  -d, --data-file=FILE          적재할 데이터 파일\n\
  -t, --table=TABLE             데이터를 적재할 테이블 이름; 데이터 파일에 테이블 정보가 없는 경우 사용\n\
      --error-control-file=FILE 적재 시 발생하는 에러에 대한 제어 파일\n\
      --ignore-class-file=FILE  적재하지 않을 클래스 이름이 있는 파일\n
121 \
load_noopt: 이 도구는 더 이상 추가 지원되지 않습니다. 사용에 유의 바랍니다.\n\
사용법: load_noopt [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -u, --user=ID                 데이터베이스 접근에 관한 사용자 이름; 기본값: "public"\n\
  -p, --password=PASS           사용자 패스워드; 기본값: 없음\n\
      --data-file-check-only    문법 체크만 수행; 기본값: 문법 체크 이후 적재\n\
  -l, --load-only               문법 체크하지 않고 적재; 기본값: 문법 체크 이후 적재\n\
      --estimated-size=NUMBER   인스턴스의 대략적인 개수; 기본값: 5000\n\
  -v, --verbose                 많은 상태 정보를 출력; 기본값: 출력하지 않음\n\
  -c, --periodic-commit=COUNT   주기적인 커밋에 대한 카운트; 기본값: 적재 중 커밋하지 않음\n\
      --no-oid                  OID 사용 안 함\n\
  -s, --schema-file=FILE[:LINE] 파일[:라인] 정보로 스키마 생성\n\
  -i, --index-file=FILE[:LINE]  파일[:라인] 정보로 인덱스 생성\n\
  -d, --data-file=FILE          적재할 데이터 파일\n\
      --error-control-file=FILE 적재 시 발생하는 에러에 대한 제어 파일\n


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 cached-pages 값이 유효하지 않습니다.\n
42 cached-page-size 값이 유효하지 않습니다.\n
43 %1$d 개의 객체가 덤프 되었습니다.\n
44 클래스 %1$s
45 , %1$d 개의 인스턴스.\n
46 %1$d 개의 객체가 (전체 %2$d 객체) 성공적으로 덤프 되었습니다.\n
47 출력 디렉터리 이름이 너무 깁니다.\n
48 LSA : %1$d %2$d \n
49 패스워드: 
60 \
unloaddb: 데이터베이스에서 스키마와 객체를 언로드\n\
사용법: %1$s unloaddb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -u, --user=ID               데이터베이스 접근에 관한 사용자 이름; 기본값: "dba"\n\
  -p, --password=PASS         사용자 패스워드; 기본값: 없음\n\
  -i, --input-class-file=FILE 테이블 이름이 기입된 파일; 기본값: 모든 클래스\n\
      --include-reference     참조되는 테이블 포함; 기본값: 허용하지 않음(-i가 지정 되야 함)\n\
      --input-class-only      명세한 클래스만 처리; 기본값: 허용하지 않음(-i가 지정 되야 함)\n\
      --lo-count=COUNT        하나의 디렉터리에 저장할 lo 파일 개수; 기본값: 0\n\
      --estimated-size=NUMBER 인스턴스의 대략적인 개수; 기본값: 계산됨\n\
      --cached-pages=NUMBER   cached 페이지 개수; 기본값: 계산됨\n\
  -O, --output-path=PATH      출력 디렉터리 경로; 기본값: 현재 작업 디렉터리\n\
  -s, --schema-only           스키마만 처리; 기본값: 스키마와 오브젝트 처리\n\
  -d, --data-only             오브젝트만 처리; 기본값: 스키마와 오브젝트 처리\n\
      --output-prefix=PREFIEX 출력 파일의 prefix; 기본값: 데이터베이스 이름 사용\n\
      --hash-file=FILE        해쉬 파일이름; 기본값: 파일을 생성하기 위해 tmpnam()을 사용함\n\
  -v, --verbose               많은 상태 메시지를 출력; 기본값: 출력 안 함\n\
      --use-delimiter         식별자 처음과 끝에 '"' 사용; 기본값: 사용 안 함\n\
  -S, --SA-mode               독립 모드 실행\n\
  -C, --CS-mode               클라이언트 서버 모드 실행\n\
      --datafile-per-class    각 클래스별 오브젝트 파일 생성; 기본값:한 개의 오브젝트 파일생성\n



$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 \n패스 1\n\n
12 %1$d 오브젝트가 처리되었습니다.\n
13 \n패스 2\n\n
14 클래스 %1$s\n
15 oid:  %1$d|%2$d|%3$d,\n
16 %1$d 인스턴스.\n
17 객체 갱신 중\n
18 ref_oid:  %1$d.%2$d.%3$d, ref_class_oid:  %4$d.%5$d.%6$d\n
19 객체를 변환할 수 없습니다\n
20 클래스의 힙 파일이 없습니다\n
21 객체를 갱신하지 못했습니다\n
22 %1$d 개의 객체(전체 %2$d 객체)가 성공적으로 처리되었습니다.\n
23 COMPACTDB가 이미 시작되었습니다.\n
24 페이지 개수는 %1$d와 %2$d 사이가 되어야 합니다.\n
25 인스턴스 잠금 타임아웃은 %1$d와 %2$d 사이가 되어야 합니다.\n
26 전체 오브젝트 수: %1$d\n
27 실패한 오브젝트 수: %1$d\n
28 수정된 오브젝트 수: %1$d\n
29 큰 오브젝트 수: %1$d\n
30 카탈로그에서 과거 클래스 표현이 삭제되었습니다.\n
31 카탈로그에 과거 클래스 표현이 보존되어 있습니다.\n
32 트랜잭션 고립 수준을 설정할 수 없습니다.\n
33 데이터베이스의 공간을 정리할 수 없습니다.\n
34 클래스 잠금 타임아웃은 %1$d와 %2$d 사이가 되어야 합니다.\n
35 잠긴 클래스... 불완전한 처리...\n
36 처리되지 않은 클래스...\n
37 처리 에러... 불완전한 처리...\n
38 처리할 것이 없음...\n
39 유효하지 않은 파라미터...\n
40 알 수 없는 클래스 이름...\n
41 사용되지 않은 클래스 OID와 페이지가 회수되었습니다.\n
42 OID와 페이지에 대한 회수가 생략되었습니다.\n
43 클래스를 처리하는 도중 에러로 인해 클래스의 처리가 생략되었습니다.\n
44 Pass 3\n\n
45 힙 공간 정리에 실패했습니다.\n
46 힙 공간 정리에 성공했습니다.\n
60 \
compactdb: 데이터베이스에서 사용되지 않는 영역을 해제.\n\
사용법: %1$s compactdb [옵션] <데이터베이스 이름> [테이블1 테이블2 ...]\n\
\n\
옵션:\n\
  -v, --verbose                     많은 상태 정보 출력; 기본값: 출력 안 함\n\
  -i, --input-class-file=FILE       테이블 이름을 포함하는 FILE 이름\n\
                                    해당 옵션 사용 시 [테이블1 테이블2 ...]이 생략되어야 함\n\
                                    기본값: 모든 테이블들의 공간을 정리함\n\
                                    클라이언트 서버 모드에서만 가능\n\
  -p, --pages-commited-once=NUMBER  한 번에 커밋할 수 있는 최대 페이지 개수\n\
                                    기본값: 10\n\
                                    클라이언트 서버 실행 모드에서만 가능\n\
  -S, --SA-mode                     독립 모드 실행\n\
  -C, --CS-mode                     클라이언트 서버 모드 실행\n\
  -d, --delete-old-repr             카탈로그에서 과거 테이블 표현을 삭제; 기본값: 사용 안 함\n\
  [테이블1 테이블2 ...]             클라이언트 서버 모드에서만 가능\n\
  -I, --Instance-lock-timeout       인스턴스 잠금에 대한 타임아웃\n\
  -c, --class-lock-timeout          클래스 잠금에 대한 타임아웃\n

$set 15 MSGCAT_UTIL_SET_COMMDB
21 CUBRID master가 호스트 %1$s 에서 수행 중입니다. %2$s 에 시작되어  
22 %1$d 개의 요청을 처리 하였습니다. \n현재 %2$d 개의 서버가 동작 중입니다.\n
23 %1$s 분 내에 SHUTDOWN 합니다!!!\n
24 %1$s
25 Shutdown 할 서버의 이름 :
26 Shutdown 할 때까지의 시간(분)? :
27 \
cub_commdb: 이 유틸리티는 더 이상 사용하지 않습니다. 'cubrid' 명령을 사용하십시오.\n\
사용법: cub_commdb [옵션]\n\
\n\
옵션:\n\
  -P, --server-list                서버 프로세스 출력\n\
  -O, --all-list                   모든 프로세스 출력\n\
  -S, --shutdown-server=NAME       해당 데이터베이스 서버 shutdown\n\
  -A, --shutdown-all               모든 프로세스 shutdown\n\
  -c, --server-mode=NAME           해당 데이터베이스 서버의 HA 모드 출력\n\
  -N, --node-list                  CUBRID heartbeat에 의해 관리되는 노드 조회\n\
  -L, --process-list               CUBRID heartbeat에 의해 관리되는 로컬 프로세스 정보 조회\n\
  -p, --ping-host                  CUBRID heartbeat를 위한 ping 호스트 정보 조회\n\
  -D, --dereg-process=PID          대상 프로세스를 종료하고 관리 대상 목록에서 제거\n\
  -F, --reconfig-node-list         CUBRID heartbeat 노드 설정 정보를 재 적용\n\
  -V, --verbose                    많은 상태 메시지 출력; 기본값: 출력하지 않음\n
28 알 수 없는 명령입니다: %1$s\n
29 호스트 이름을 찾을 수 없습니다-중지\n
30 인자 파일을 찾을 수 없습니다\n
31 %1$s 에서 마스터 서버로 연결할 수 없습니다\n
32 얼마 후에 shutdown 하시겠습니까?\n
33 마스터는 %1$d 분 후에 shutdown 됩니다.\n
34 클라이언트로부터의 데이터 수신 에러
35 한글 database 서버 %1$s 은(는) 현재 수행 중이 아닙니다.
36 쉘 (%1$s) 를 수행할 수 없습니다. %2$s, %3$s 
37 프로세스 fork %1$s, 반환 %2$d, %3$s
38 %1$s을(를) 수행할 수 없습니다
39 -i(--immediately)옵션은 --deact-stop-all 옵션이 지정되어야합니다.\n

$set 16 MSGCAT_UTIL_SET_PATCHDB
60 \
emergency_patchlog: 긴급한 상황에서 데이터베이스 로그 패치\n\
사용법: %1$s emergency_patchlog [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -r, --recreate-log           로그 파일 재생성\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 페이지 개수 값 %1$d가 잘못되었습니다\n
21 볼륨 저장소이름 '%1$s'가 잘못되었습니다\n
22 --max-writesize-in-sec 옵션은 독립 실행 모드에서는 무시 됩니다.\n
60 \
addvoldb: 데이터베이스 볼륨 파일을 추가\n\
사용법: %1$s addvoldb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
      --db-volume-size=SIZE        추가될 볼륨의 크기; 기본값: 설정에서 읽음\n\
      --max-writesize-in-sec=SIZE  초당 추가될 볼륨의 크기. (예. 512K, 1M, 10M); 기본값: 사용하지 않음; 최소값: 160K\n\
  -n, --volume-name=NAME           볼륨 이름; 기본값: "db"_ext1 과 같은 이름을 생성\n\
  -F, --file-path=PATH             데이터베이스 파일에 대한 볼륨 패스; 기본값: 현재 디렉터리\n\
      --comment=COMMENT            볼륨 주석; 기본값: 없음\n\
  -p, --purpose=PURPOSE            볼륨 사용 용도; 다음과 같은 값이 허용됨\n\
                                           DATA - 데이터 볼륨\n\
                                           INDEX - 인덱스 볼륨\n\
                                           TEMP - 임시 데이터 볼륨\n\
                                           GENERIC - 임의 데이터 볼륨\n\
  -S, --SA-mode                    독립 모드 실행\n\
  -C, --CS-mode                    클라이언트 서버 모드 실행\n

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 데이터베이스에서 비일관성이 발견되었습니다.\n추가적인 정보는 error_log_file %1$s 을(를) 참고 하십시오\n
21 %1$s: 클래스를 찾을 수 없어 무시합니다.\n
22 %1$s: Cannot find the index on class %2$s.\n
60 \
checkdb: 데이터베이스 일관성 검사.\n\
사용법: %1$s checkdb [옵션] <데이터베이스 이름> [class_name1 class_name2 ...]\n\
\n\
옵션:\n\
  -S, --SA-mode                독립 모드 실행\n\
  -C, --CS-mode                클라이언트 서버 모드 실행\n\
  -r, --repair                 비 일관성 발견 시 복구\n\
  -i, --input-class-file=FILE  클래스 이름이 나열된 입력 파일 명시\n\
                               기본값: 모든 클래스를 검사\n\
  -I, --index-name             index name that will be checked only\n\
                               default: check all indices\n\
      --check-prev-link        인덱스 링크의 오류를 검사\n\
      --repair-prev-link       인덱스 링크가 잘못된 경우 복구 수행\n\
      --check-file-tracker     check all allocated pages of file\n\
      --check-heap             check all pages of heap files\n\
      --check-catalog          check catalog consistency\n\
      --check-btree            check all btree indices\n\
      --check-class-name       check class name consistency\n\
      --check-btree-entries    check entries of btree consistency\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
15 데이터베이스 '%1$s'의 (페이지 크기 %2$s) 볼륨 정의 (로그 페이지 크기 %3$s)\n\n
16 %1$5d %2$9s  %3$s %4$s  %5$s\n
17 출력파일 '%1$s' 을(를) 열 수 없습니다\n
18 \n데이터베이스 '%1$s'의 (페이지크기 %2$s) 임시 볼륨\n\n
19 번호  용도        페이지수  여유페이지  볼륨이름\n\n
20 번호  용도        전체크기    여유공간  볼륨이름\n\n
21 LOB 파일 저장소 %1$s\n\n
22 -------------------------------------------------------------------------------\n
30 데이터베이스 '%1$s'(페이지 크기 %2$s)의 볼륨 용도별 합산 (로그 페이지 크기 %3$s)\n\n
31 용도         페이지수  사용된페이지  여유페이지  볼륨개수\n
32 용도         전체크기    사용된공간    여유공간  볼륨개수\n
33 %1$10s %2$s %3$s %4$s %5$10d\n
34 -------------------------------------------------------------\n
40 번호   용도      페이지수     여유페이지 데이터페이지 인덱스페이지 임시페이지 볼륨이름\n\n
41 번호   용도      전체크기       여유공간   데이터공간   인덱스공간   임시공간 볼륨이름\n\n
42 %1$5d %2$9s  %3$s %4$s %5$s %6$s %7$s  %8$s\n
43 ----------------------------------------------------------------------------------------------------\n
50 용도        페이지수  사용된페이지 여유페이지 데이터페이지 인덱스페이지 임시페이지  볼륨개수\n
51 용도        전체크기    사용된공간   여유공간   데이터공간   인덱스공간   임시공간  볼륨개수\n
52 %1$10s %2$s %3$s %4$s %5$s %6$s %7$s %8$10d\n
53 -------------------------------------------------------------------------------------------------\n
60 \
spacedb: 데이터베이스 디스크 공간 정보 출력\n\
사용법: %1$s spacedb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -o, --output-file=FILE       파일로 출력을 재지정; 기본값: 재지정 하지 않음\n\
  -S, --SA-mode                독립 모드 실행\n\
  -C, --CS-mode                클라이언트 서버 모드 실행\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               출력 단위(page, mega, giga, tera, human readable) 설정; 기본값: H\n\
  -s, --summarize              볼륨 용도별 합계 결과 출력\n\
  -p, --purpose                각 볼륨의 목적별 (데이터/인덱스/임시) 사용량을 출력\n


$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 추정되는 페이지 개수 = %1$d\n
60 \
estimate_data: 데이터 크기 추정\n\
사용법: %1$s estimate_data number-of-instances average-instance-size number-of-attributes variable-number-of-attributes\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 잘못된 인덱스 키 타입입니다  = %1$s\n
16 평균 키 값이 잘못되었습니다 length = %1$d, precision = %2$d\n
17 잘못된 인자가 있습니다\n
20 예상 전체 인덱스 페이지 수 = %1$d\n(인스턴스가 만들어진 이후 인덱스가 생성되는 경우)\n
21 예상 전체 인덱스 페이지 수 = %1$d\n(인스턴스가 만들어지기 전에 인덱스가 생성되는 경우(average case))\n
22 예상 전체 인덱스 페이지 수 = %1$d\n(인스턴스가 만들어지기 전에 인덱스가 생성되는 경우(worst case))\n
23 주어진 입력에 따른 값...\n
24 인스턴스의 총 개수  : %1$d\n
25 서로 다른 키의 개수 : %1$d\n
26 평균 키 크기        : %1$d\n
27 키 타입             : %1$s\n\n
60 \
estimate_index: 인덱스 크기 추정\n\
사용법: %1$s estimate_index number-of-instances number-of-different-keys average-key-size key-type\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: 데이터베이스 설치\n\
사용법: %1$s installdb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
      --server-name=HOST       서버 호스트 이름; 기본값: 현재 호스트 이름\n\
  -F, --file-path=PATH         데이터베이스 볼륨을 위한 디렉터리 경로; 기본값: 현재 작업 디렉터리\n\
  -L, --log-path=PATH          로그 볼륨을 위한 디렉터리 경로; 기본값: file-path와 동일\n

$set 23 MSGCAT_UTIL_SET_MIGDB
1 임시 클래스 OID\n
2 ELO 값을 출력 할 수 없습니다\n
3 long object 액세스 에러\n
4 long object migration 을 위한 출력 파일 오픈 에러\n
5 읽기 에러\n
6 쓰기 에러\n
7 ELO 입력 파일 "%1$s" 을(를) 여는데 에러\n
8 클래스:
9 해쉬 파일 이름
10 이름:                          %1$s\n
11 크기:                          %1$d\n
12 페이지 크기:                   %1$d\n
13 데이터 크기:                   %1$d\n
14 엔트리 크기:                   %1$d\n
15 페이지당 엔트리들:             %1$d\n
16 Cached 페이지:                 %1$d\n
17 엔트리의 개수:                 %1$d\n
18 collision 개수:                %1$d\n
19 해쉬 파일 이름:                %1$s\n
20 다음 오버플로우 엔트리:        %1$d\n
21 키 타입:                       %1$s\n
22 페이지 헤더의 주소:            %1$x\n
23 마지막 페이지 헤더의 주소:     %1$x\n
24 여유 페이지 헤더의 주소:       %1$x\n
25 페이지 비트맵의 주소:          %1$x\n
26 페이지 비트맵 크기:            %1$d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
60 \
diagdb: 데이터베이스 정보 덤프\n\
사용법: %1$s diagdb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -d, --dump-type=TYPE         TYPE of dump\n\
                               -1 모두 덤프\n\
                                1 파일 테이블 덤프\n\
                                2 파일 용량 덤프\n\
                                3 힙 용량 덤프\n\
                                4 인덱스 용량 덤프\n\
                                5 클래스 이름 덤프\n\
                                6 디스크 bitmap 덤프\n\
                                7 카탈로그 덤프\n\
                                8 로그 덤프\n\
                                9 힙 덤프\n


$set 25 MSGCAT_UTIL_SET_LOCKDB
15 출력 파일 '%1$s'을 열 수 없습니다.\n
59 lockdb는 독립 모드에서 실행할 수 없습니다.\n
60 \
lockdb: 데이터베이스의 lock 정보 출력\n\
사용법: %1$s lockdb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -o, --output-file=FILE       출력 메시지를 재지정할 파일; 기본값: 없음\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 다음 인자 중 하나만 허용: 트랜잭션-인덱스, 사용자, 호스트, 프로그램\n
21 DBA 비밀 번호:
26 주어진 인자에 해당하는 트랜잭션이 없습니다\n
27 다음 트랜잭션들을 종료시킬 준비가 되었습니다:\n\n
28 계속하시겠습니까? (Y/N)
29 트랜잭션 인덱스 %1$d로 지정된 트랜잭션들을 종료시킵니다\n
30 다음 트랜잭션들의 종료에 실패했습니다\n\n
31 트랜잭션이 종료되지 않았습니다. 롤백 진행 중일 수 있습니다.\n 잠시 기다렸다가 확인하고 재시도하시기 바랍니다.\n
32 유효하지 않은 트랜잭션 인덱스입니다.(%1$s).\n
59 killtran은 독립 모드에서 실행할 수 없습니다.\n
60 \
killtran: 데이터베이스 transaction 제거 관리\n\
사용법: %1$s killtran [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -i, --kill-transaction-index=INDEX로 지정되는 해당 트랜잭션 종료\n\
      --kill-user-name=ID            ID로 지정되는 사용자의 모든 트랜잭션 종료\n\
      --kill-host-name=HOST          HOST로 지정되는 클라이언트의 모든 트랜잭션 종료\n\
      --kill-program-name=NAME       NAME으로 지정되는 모든 트랜잭션 종료\n\
      --kill-sql-id=SQL_ID           SQL_ID에 해당하는 질의를 실행중인 모든 트랜잭션 종료\n\
  -p, --dba-password=PASS            DBA 패스워드; 지정되지 않은 경우 프롬프트\n\
  -d, --display-information          현재 활성화된 트랜잭션에 대한 정보 보여줌\n\
  -q, --query-exec-info              현재 활성화된 트랜잭션에 대한 질의 수행정보를 보여줌\n\
  -f, --force                        사용자 확인 없이 지정된 트랜잭션 종료\n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1  데이터베이스-이름
2  필수 인자; 기본값: 없음
3  호환성 검사
4  호환성 검사만 수행
5  백업 경로 
6  백업 볼륨을 저장할 디렉터리 경로나 디바이스 이름
20 데이터베이스 '%1$s'가 존재하지 않습니다.
21 '%1$s'는(은) 버전이 너무 낮아 %2$s으로 마이그레이션 할 수 없습니다.\n
22 '%1$s'는(은) 마이그레이션이 필요 없습니다.\n
23 '%1$s'는(은) 마이그레이션이 필요합니다.\n
24 마이그레이션 후 백업하는 것을 권장합니다. 지금 백업하시겠습니까? (y/n): 
25 마이그레이션 후 백업하는 것을 권장합니다. 
26 \n*** 경고: %1$s 으로 마이그레이션 수행 전에 데이터베이스를 백업하십시오 ***\n\n마이그레이션 중 발생할 수 있는 장애를 복구하기 위해서는 반드시 백업이 필요합니다.\n백업 하셨습니까? (y/n): 
27 \n1. backupdb 실행 \n2. 새로운 데이터베이스에 대한 새로운 백업 경로 입력. (현재 경로: %1$s) \n3. 종료 \n
28 \n새로운 데이터베이스에 대한 백업 경로: 
29 \n마이그레이션 및 백업을 정상적으로 수행했습니다.
30 백업에 실패했습니다. 다시 백업하십시오.
31 \n\n*** 경고: 마이그레이션 완료 이후에는 이전 버전의 백업 파일을 이용하여 복구할 수 없습니다.\n이전 버전의 백업 파일은 해당 버전에서만 복구가 가능합니다.\n이전 버전의 백업 파일을 보존하십시오. ***\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: 데이터베이스의 호스트 변경\n\
사용법: %1$s alterdbhost [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -h, --host=HOST              호스트 이름; 기본값: 현재 호스트 이름\n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 database-name
2 필수 인자; 기본값: 없음
3 java-class-file
4 필수 인자; 기본값: 없음
5 force-overwrite
6 클래스 파일이 이미 존재하면 덮어씀; 기본값: 덮어쓰지 않음

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 출력 파일 '%1$s'을 열 수 없습니다\n
59 plandump는 독립 모드에서 실행할 수 없습니다.\n
60 \
plandump: 쿼리 플랜 표시 및 관리\n\
사용법: %1$s plandump [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -d, --drop                   서버 케시에 있는 모든 플랜 삭제\n\
  -o, --output-file=FILE       출력 메시지를 재지정할 파일; 기본값: 없음\n

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 출력 파일 '%1$s'을 열 수 없습니다.\n
21 클라이언트 프로그램에 의해 사용되는 시스템 파라미터\n
22 서버 '%1$s'에 의해 로딩된 시스템 파라미터\n
23 독립 모드에서 사용되는 시스템 파라미터\n
60 \
paramdump: 시스템 파라미터 설정을 덤프\n\
사용법: %1$s paramdump [옵션] database-name\n\
\n\
valid options:\n\
  -o, --output-file=FILE       출력 메시지를 재지정할 파일; 기본값: 없음\n\
  -b, --both                   dump both client and server parameters\n\
  -S, --SA-mode                독립 모드 실행\n\
  -C, --CS-mode                클라이언트 서버 모드 실행\n

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 서버의 HA 모드에 대해 잘못된 값 \"%1$s\"입니다.\n
12 서버의 HA 모드를 바꿀 수 없습니다.\n
21 DBA 암호 입력:
22 서버 '%1$s'의 현재 HA 모드는 %2$s입니다.\n
23 서버 '%1$s'의 HA 모드는 \"%2$s\"로 바뀌었습니다.\n
24 서버가 HA를 사용할 수 있도록 설정되지 않았습니다.\n
58 HA 기능이 지원되지 않는 플랫폼입니다.\n
59 changemode는 독립 모드에서 실행할 수 없습니다.\n
60 \
changemode: 서버의 HA 모드 출력 또는 변경\n\
사용법: %1$s changemode [옵션] <데이터베이스 이름>\n\
\n\
valid options:\n\
  -m, --mode=MODE             서버의 HA 모드를 지정한 MODE로 변경; 기본값: 없음\n\
  -f, --force                 강제 변경\n\
  -t, --timeout=S             트랜잭션이 정상 종료될 때까지 S초 대기\n\
                              -t 옵션은 사용자가 서버의 HA 모드를 maintenance로 변경할 때만 유효\n\
                              기본값: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 copylogdb의 실행 모드에 대해 잘못된 값 \"%1$s\"\n
21 DBA 암호 입력:
22 서버가 HA 모드로 설정되지 않았습니다.\n
58 HA 기능이 지원되지 않는 플랫폼입니다.\n
59 copylogdb는 독립 모드에서 실행할 수 없습니다.\n
60 \
copylogdb: 서버에서 로그 페이지 복사\n\
사용법: %1$s copylogdb [옵션] <데이터베이스 이름>\n\
\n\
valid options:\n\
  -L, --log-path=PATH         복사된 로그 볼륨에 대한 디렉터리 PATH\n\
  -m, --mode=MODE             동작 모드; 기본값: sync\n\
                              MODE: sync or semisync or async\n\
  -S, --start-page-id=ID      복사 시작 로그 페이지\n

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 DBA 암호 입력:
22 서버가 HA 모드로 설정되지 않았습니다.\n
58 HA 기능이 지원되지 않는 플랫폼입니다.\n
59 applylogdb는 독립 모드에서 실행할 수 없습니다.\n
60 \
applylogdb: 복사된 로그를 서버에 반영\n\
사용법: %1$s applylogdb [옵션] <데이터베이스 이름>\n\
\n\
valid options:\n\
  -L, --log-path=PATH         복사된 로그 볼륨의 디렉터리 PATH\n\
      --max-mem-size=SIZE     MB 단위의 최대 프로세스 크기(SIZE); 최대값 500\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 출력 파일 '%1$s'을 열 수 없습니다.\n
59 statdump는 독립 모드에서 실행할 수 없습니다.\n
60 \
statdump: 전역 통계를 출력.\n\
사용법: %1$s statdump [옵션] <데이터베이스 이름>\n\
\n\
valid options:\n\
  -o, --output-file=FILE       출력 메시지를 재지정할 파일; 기본값: 없음\n\
  -c, --cumulative             누적값을 출력\n\
  -i, --interval=S             매 S 초마다 통계를 출력\n\
  -s, --substr=STRING          지정한 문자열이 포함된 항목만 출력\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 DBA 암호 입력:
22 서버가 HA 모드로 설정되지 않았습니다.\n
58 HA 기능이 지원되지 않는 플랫폼입니다.\n
59 applyinfo는 독립 모드에서 실행할 수 없습니다.\n
60 \
applyinfo: display CUBRID HA 복제 상태 출력\n\
사용법: %1$s applyinfo [옵션] database-name\n\
\n\
valid options:\n\
  -r, --remote-host-name      원격 호스트 이름; 원격 노드의 활성 로그 정보 출력\n\
  -a, --applied-info          반영된 정보 출력\n\
  -L, --copied-log-path=PATH  복사된 로그 볼륨의 경로; 복사된 로그 정보 출력\n\
  -p, --page=ID               페이지 id; 기본값: 0\n\
  -v, --verbose               상세 상태 정보 출력; 기본값: 사용하지 않음\n\
  -i, --interval=S            매 S 초마다 로그 정보 출력\n

$set 45 MSGCAT_UTIL_SET_ACLDB
59  acldb는 독립 모드에서 실행할 수 없습니다.\n
60 \
acldb: 서버의 접근권한 목록을 출력하거나 다시 읽습니다.\n\
사용법: %1$s acldb [옵션] <데이터베이스 이름>\n\
\n\
valid options:\n\
  -r, --reload       접근 권한 파일 다시 읽기.\n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
genlocale: LDML 입력 파일을 사용하는 LDML 공유 라이브러리를 생성\n\
사용법: %1$s genlocale [옵션] [language-string]\n\
\n\
language-string	      공유 라이브러리를 생성할 로캘 언어의 문자열.\n\
		      language-string이 생략되면 'cubrid_locales.txt'에 정의된 모든 언어에 대해 생성합니다.\n\
valid options:\n\
  -v, --verbose	              상세 정보 출력; 기본값: 사용 안 함\n\
  -i, --input-ldml-file       입력 LDML 파일에 대한 경로. language-string이 주어질 때만 사용.\n
61 유효하지 않은 로캘 인자.\n

$set 47 MSGCAT_UTIL_SET_DUMPLOCALE
57 -i/--input-file과 로캘 식별자를 동시에 사용할 수 없습니다.\n
58 유효하지 않은 코드포인트 범위(-s -e).\n
59 유효하지 않은 로캘 인자.\n
60 \
dumplocale: LDML 입력 파일을 사용하여 미리 생성된 로캘 공유 라이브러리의 내용을 덤프\n\
사용법: %1$s dumplocale [옵션] [language-string]\n\
\n\
language-string	 로캘 공유 라이브러리에 대해 덤프할 로캘 언어 문자열\n\
		         no language-string이 생략되면 'cubrid_locales.txt'에 있는 모든 언어에 대해 덤프합니다.\n\
valid options:\n\
  -i, --input-file    입력할 로캘 공유 라이브러리(미리 생성됨) 파일에 대한 경로.\n\
                      language-string이 생략될 때만 사용 가능.\n\
  -d, --calendar      캘린더와 날짜/시간 정보 덤프; 기본값: 사용 안 함\n\
  -n, --numeric		  숫자 정보 덤프; 기본값: 사용 안 함\n\
  -a, --alphabet=l|lower|u|upper|both	     알파벳과 대소문자 정보 덤프; 기본값: 사용 안 함\n\
      --identifier-alphabet=l|lower|u|upper	 알파벳과 식별자에 대한 대소문자 정보 덤프; 기본값: 사용 안 함\n\
  -c, --codepoint-order	 코드포인트 값을 기반으로 정렬한 콜레이션 정보를 덤프\n\
                         (cp, char, weight, next-cp, char and weight);\n\
                         기본값: 사용 안 함\n\
  -w, --weight-order  가중치 값을 기반으로 정렬한 콜레이션 정보를 덤프\n\
                      (weight, cp, char); 기본값: 사용 안 함\n\
  -s, --start-value	  필터링 옵션; 아래 옵션들에 대한 시작 코드포인트\n\
                      -a, --identifier-alphabet, -c, -w; default : 0\n\
  -e, --end-value     필터링 옵션; 아래 옵션들에 대한 끝 코드 포인트\n\
                      -a, --identifier-alphabet, -c, -w; \n\
                      기본값: 로캘 공유 라이브러리에서 읽어들인 최대 값\n\
  -k, --console-conversion  콘솔 변환 데이터를 덤프; 기본값: 사용 안 함\n\
  -z, --normalization   정규화 데이터를 덤프; 기본값: 사용 안 함\n

$set 48 MSGCAT_UTIL_SET_SYNCCOLLDB
43 파티션 기준 컬럼의 콜레이션 "%1$s"를 변경하기 전에 파티션을 삭제해야 합니다.\n클래스\n 
44 콜레이션을 업데이트 하기 전에 콜레이션 "%1$s"을 가지는 컬럼의 외래키를 삭제해야 합니다.\n테이블 | 외래키\n
45 콜레이션 "%1$s"를 가진 클래스는 재정의 해야 합니다.\n클래스\n
46 콜레이션 "%1$s"를 가지는 표현식을 포함한 함수 인덱스는 재정의 해야 합니다.\n테이블 | 인덱스 | 함수 expression\n
47 콜레이션 업데이트가 수행되지 않았습니다. 데이터베이스는 변경되지 않았습니다.\n
48 시스템 테이블 "%1$s"은 새로운 콜레이션으로 업데이트되었습니다.\n
49 시스템 콜레이션을 업데이트하면 데이터베이스가 손상될 수 있습니다. 계속 하시겠습니까? (y/n)\n 
50 새로운 시스템 설정에서 데이터베이스에 콜레이션 "%1$s"(Id: %2$d)가 존재하지 않거나 변경되었습니다.\n
51 콜레이션 "%1$s"를 포함하는 조건을 가진 트리거는 재정의 해야 합니다.\n트리거 | 조건\n
52 콜레이션 "%1$s"를 포함하는 뷰는 재정의 해야 합니다.\n뷰 | 질의스펙\n 
53 콜레이션 "%1$s"를 가진 애트리뷰트 도메인은 재정의 해야 합니다.\n테이블 | 애트리뷰트 도메인\n
54 수동적 검사가 필요합니다.\n\
데이터베이스에 콜레이션을 강제로 적용하기 전에 "%1$s" 파일을 검사하시기 바랍니다.\n
55 콜레이션 업데이트가 필요하지 않습니다.\n
56 데이터베이스는 시스템 콜레이션으로 업데이트 되어야 합니다.\n\
"cubrid synccolldb -f %1$s"으로 실행 하십시오.\n
57 데이터베이스에 비해 시스템 설정에는 %1$d개의 새로운 콜레이션이 있습니다.\n
58 데이터베이스 "%1$s"의 모든 콜레이션은 새로운 시스템 설정 상에 존재합니다.\n 
59 데이터베이스의 %1$d개의 콜레이션은 설정되지 않았거나 새로운 시스템 콜레이션으로 변경되었습니다.\n 
60 \
synccolldb: 데이터베이스 콜레이션을 시스템 설정에 정의된 콜레이션에 맞게 업데이트 합니다.\n\
사용법: %1$s synccolldb [옵션] 데이터베이스-이름\n\
\n\
valid options:\n\
  -c, --check-only    데이터베이스의 콜레이션과 시스템 콜레이션을 검사합니다.\n\
		      만약 콜레이션 업데이트가 필요하면 에러코드 1이 리턴됩니다.\n\
		      또한, 호환되지 않는 콜레이션을 출력합니다.\n\
  -f, --force-only    데이터베이스의 콜레이션을 새로운 시스템 설정의 콜레이션으로 강제 업데이트 합니다.\n\

$set 49 MSGCAT_UTIL_SET_TRANLIST
20 사용자 %1$s의 비밀번호를 입력하세요 : 
22 트랜잭션인덱스      사용자이름     호스트이름     프로세스 ID     프로그램이름\n
23 -------------------------------------------------------------------------------\n
24 %1$4d%2$-10s %3$14s %4$14s %5$15d %6$17s\n
25 트랜잭션이 없습니다\n
26 tranlist는 DBA 또는 DBA 그룹의 멤버에 의해서만 수행될 수 있습니다.\n
27 유효하지 않은 컬럼번호(%1$d) 입니다.\n
32 트랜잭션인덱스      프로세스 ID     프로그램이름         질의수행시간  트랜잭션시간             대기중인 락 소유자      질의문 ID     질의문\n
33 %1$4d%2$-10s %3$14d    %4$-25s %5$10.2f %6$12.2f    %7$28s     %8$13s   %9$s\n
34 --------------------------------------------------------------------------------------------------------------------------------------------------\n
42 트랜잭션인덱스      사용자이름     호스트이름     프로세스 ID     프로그램이름         질의수행시간  트랜잭션시간             대기중인 락 소유자      질의문 ID     질의문\n
43 %1$4d%2$-10s %3$14s %4$14s %5$14d    %6$-25s %7$10.2f %8$12.2f    %9$28s     %10$13s   %11$s\n
44 ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 트랜잭션 인덱스 : %1$d
46 질의문 ID : %1$s\n
59 tranlist는 독립 모드에서 실행할 수 없습니다.\n
60 \
tranlist: 활성화된 트랜잭션 정보 출력.\n\
사용법: %1$s tranlist [옵션] database-name\n\
\n\
valid options:\n\
  -u, --user=ID                데이터베이스 접근에 관한 사용자 이름; 기본값: DBA\n\
  -p, --password=PASS          사용자 패스워드; 기본값: 없음\n\
  -s, --summary                질의정보 및 락 정보 생략\n\
      --sort-key=COLUMN_NUMBER 정렬할 컬럼번호; 기본값 : 1\n\
      --reverse                내림차순 정렬수행; 기본값 : 오름차순 정렬\n

$set 50 MSGCAT_UTIL_SET_PREFETCHLOGDB
22 서버가 HA 모드로 설정되지 않았습니다.\n
23 Prefetch 기능을 활성화 시키지 않았습니다.\n
58 HA 기능이 지원되지 않는 플랫폼입니다.\n
59 prefetchlogdb는 독립 모드에서 실행할 수 없습니다.\n
60 \
prefetchlogdb: 복사된 로그를 서버에 반영하기 위해 필요한 데이터를 prefetch\n\
사용법: %1$s prefetchlogdb [옵션] <데이터베이스 이름>\n\
\n\
valid options:\n\
  -L, --log-path=PATH         복사된 로그 볼륨의 디렉터리 PATH\n
