
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

$set 1 MSGCAT_UTIL_SET_GENERIC
1 크기가 잘못되었거나, 잘못된 문자 `%c'이(가) 데이터베이스 이름 `%s'에 포함되어 있습니다\n
2 출력 파일 '%s'을(를) 열 수 없습니다.\n
3 유틸리티 이름이 빠져 있거나 혹은 잘못 되어 있습니다.\n
4 잘못된 유틸리티 이름 `%s'\n
5 다음 리스트는 올바른 유틸리티 이름입니다:\n
6 잘못된 문자 `%c'이(가) 볼륨 이름 `%s'에 포함되어 있습니다.\n
9 \
%s (cubrid 유틸리티) %s\n\
Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n
10 \
cubrid 관리자 유틸리티, 버전 %s\n\
사용법: %s <유틸리티 이름> [옵션]\n\
각 유틸리티에 대한 도움말을 원하면 '%s <유틸리티 이름>'  명령을 수행 하세요.\n\
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
\n\
%s (은)는 DBMS 관리 도구입니다.\n\
추가적인 정보는 다음 사이트를 참조하세요. http://www.cubrid.com\n
12 '%s' 는 없는 유틸리티 이름입니다.\n'cubrid --help' 명령어로 사용법을 알아보세요.\n
13 '%s' 는 잘못된 명령어입니다.\n'cubrid --help' 명령어로 사용법을 알아보세요.\n
14 cubrid.conf 파일에서 서비스 속성을 읽어 들일 수 없습니다.\n
15 @ %s %s: %s\n
16 @ %s %s\n
17 ++ %s '%s' (이)가 수행되고 있지 않습니다.\n
18 ++ %s (이)가 수행되고 있지 않습니다.\n
19 ++ %s '%s' (이)가 수행되고 있습니다.\n
20 ++ %s (이)가 수행되고 있습니다.\n
21 ++ %s %s: %s\n
22 인자가 부족합니다. \n'cubrid --help' 명령어로 사용법을 알아보세요.\n
23 cubrid 유틸리티, 버전 %s\n\
사용법: %s <유틸리티 이름> [인자]\n\
'%s <utility-name>' 명령어로 각 유틸리티에 대한 도움말을 보실 수 있습니다.\n\
\n\
가용한 서비스 유틸리티 이름:\n\
    service\n\
    server\n\
    broker\n\
    manager\n\
    repl_server\n\
    repl_agent\n\
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
\n\
%s (은)는 DBMS  도구입니다.\n\
추가적인 정보는 다음 사이트를 참조하세요. http://www.cubrid.com\n
24 cubrid 유틸리티, 버전 %s\n\
사용법: %s service <명령어>\n\
\n\
가용한 명령어:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 cubrid 유틸리티, 버전 %s\n\
사용법: %s server <명령어> [인자]\n\
\n\
가용한 명령어:\n\
    start       [데이터베이스 이름]\n\
    stop        [데이터베이스 이름]\n\
    restart     [데이터베이스 이름]\n\
    status\n
26 cubrid 유틸리티, 버전 %s\n\
사용법: %s broker <명령어> [인자]\n\
\n\
가용한 명령어:\n\
    start\n\
    stop\n\
    restart\n\
    status      [-b] [-q] [-t <sec>] [-s <sec>] [<expr>]\n\
    on          <브로커 이름>\n\
    off         <브로커 이름>\n
27 cubrid 유틸리티, 버전 %s\n\
사용법: %s manager <명령어>\n\
\n\
가용한 명령어:\n\
    start\n\
    stop\n\
    status\n
28 cubrid 유틸리티, 버전 %s\n\
사용법: %s repl_server <명령어> [인자]\n\
\n\
가용한 명령어:\n\
    start       <데이터베이스 이름> <서버 네트워크 포트> [-a 최대 agent 개수] [-e 에러 파일]\n\
    stop        <데이터베이스 이름>\n\
    status\n
29 cubrid 유틸리티, 버전 %s\n\
사용법: %s repl_agent <명령어> [인자]\n\
\n\
가용한 명령어:\n\
    start       <데이터베이스 이름> [DBA 패스워드]\n\
    stop        <데이터베이스 이름>\n\
    status\n
30 '%s' 인자가 필요 없습니다.\n
31 데이터베이스 이름이 누락됐습니다.\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 경고: -t 옵션은 독립 실행 모드에서는 무시 됩니다.\n
60 \
backupdb: 데이터베이스를 백업합니다.\n\
사용법: %s backupdb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -D, --destination-path=백업 볼륨이 저장될 디렉터리 경로. 지정되지 않은 경우에는 로그 디렉터리.\n\
  -r, --remove-archive         불필요한 로그 파일을 지움. 유의해서 사용해야 합니다. 관리자 매뉴얼을 참고하세요.\n\
  -l, --level=LEVEL            백업 수준. 기본 수준은 전체입니다.\n\
                               다음과 같은 백업 수준을 지정할 수 있습니다.\n\
                                   0 - 전체\n\
                                   1 - 증분 1\n\
                                   2 - 증분 2\n\
  -o, --output-file=<파일> 자세한 백업 메시지를 파일에 출력합니다.\n\
  -S, --SA-mode                독립 실행 모드\n\
  -C, --CS-mode                클라이언트 서버 실행 모드\n\
      --no-check               데이터베이스의 정합성 확인 작업을 수행하지 않습니다. (기본값: 수행)\n\
  -t, --thread-count=COUNT     백업을 수행하는 스레드 개수. (기본값: 자동)\n\
  -z, --compress               백업 볼륨을 압축합니다. (기본값: 압축하지 않음)\n\
  -e, --except-active-log      백업 볼륨에 활동 로그를 포함하지 않습니다. (기본값 : 포함)\n
71 \"%s\" 와 \"%s\" (은)는 함께 사용할 수 없습니다.\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 에러: 대상 이름이 원본 이름과 동일합니다.\n
31 에러: extended-volume-path 가 존재하지 않거나 쓰기 금지되어 있습니다.\n
32 에러: control-file이 없거나 읽을 수 없습니다.\n
60 \
copydb: 데이터베이스 복사.\n\
사용법: %s copydb [옵션] <원본 데이터베이스 이름> <대상 데이터베이스 이름>\n\
\n\
옵션:\n\
      --server-name=HOST     서버 호스트 이름 (기본값: 현재 호스트 이름)\n\
  -F, --file-path=PATH            데이터베이스 볼륨 디렉터리 경로(기본값: 현재 디렉터리)\n\
  -L, --log-path=PATH            데이터베이스 로그 파일 경로 (기본값: file-path 값과 동일)\n\
  -E, --extended-volume-path=PATH 확장된 볼륨들을 copy/rename을 위한 디렉터리 경로,기본값은 원 볼륨과 같은 경로 혹은  control-file에 주어진 경로입니다. \n\
  -i, --control-file=FILE       여러 장소에 있는 볼륨들을 copy/rename을 위한 제어 파일\n\
  -r, --replace                   기존 데이터베이스 대체 허용(기본값: 대체 안 함)\n\
  -d, --delete-source        기존 데이터베이스 삭제 (기본값: 삭제 안 함)\n


$set 4 MSGCAT_UTIL_SET_CREATEDB
40 그룹 `%s'는 존재하지 않습니다\n
41 사용자 `%s'는 존재하지 않습니다\n 
42 알 수 없는 명령어 `%s'\n
43 출력 파일 '%s' 을(를) 열 수 없습니다.\n
44 기존 데이터베이스를 삭제할 수 없습니다.\n
45 %d 페이지의 데이터베이스 생성 중.\n
46 데이터베이스 생성할 수 없습니다.\n
47 user file `%s'에 오류가 있습니다.\n
48 페이지 수는 최소 100 이상이어야 합니다.\n
49 데이터베이스 `%s'가 `%s' 경로에 성공적으로 생성되었습니다.\n데이터베이스를 시작하려면 `cubrid start server %s' 명령을 실행하십시오.\n질의를 실행하려면 `csql %s' 명령을 실행하십시오.\n
60 \
createdb: 데이터베이스 생성.\n\
사용법: %s createdb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -p, --pages=NUMBER                  첫 번째 볼륨에 대한 초기 페이지 수.(기본값: 5000)\n\
      --comment=COMMENT               데이터베이스 정보를 포함한 문자열.(기본값: 없음)\n\
  -F, --file-path=PATH                데이터베이스 볼륨에 대한 디렉터리 경로.(기본값: 현재 디렉터리)\n\
  -L, --log-path=PATH                 데이터베이스 로그 볼륨에 대한 디렉터리 경로.(기본값: file-path와 동일)\n\
      --server-name=HOST              서버 호스트 이름.(기본값: 현재 호스트)\n\
  -r, --replace                       기존 데이터베이스 대체 가능.(기본값: 허용하지 않음)\n\
      --more-volume-file=FILE         추가 확장될 볼륨의 생성에 대한 제어 파일\n\
      --user-definition-file=FILE     사용자 정의를 포함한 파일.(기본값: 없음)\n\
      --csql-initialization-file=FILE FILE 데이터베이스 초기화 SQL 파일.(기본값: 없음)\n\
  -o, --output-file=FILE              출력 메시지 저장 파일.(기본값: 없음)\n\
  -v, --verbose                       많은 상태 메시지 출력.(기본값: 출력하지 않음)\n\
  -l, --log-page-count=NUMBER         로그 archive에 사용될 페이지 수.(기본값: 첫 번 째 일반 볼륨과 동일)\n\
  -s, --page-size=SIZE                바이트 단위의 데이터베이스 페이지 크기.(기본값: OS의 I/O 단위)\n


$set 6 MSGCAT_UTIL_SET_DELETEDB
60 \
deletedb: 데이터베이스 삭제.\n\
사용법: %s deletedb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -o, --output-file=FILE       출력 메시지가 저장될 파일.(기본값: 없음)\n\
  -d, --delete-backup          백업 볼륨을 지움(기본값: 지우지 않음)\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
60 \
renamedb: 데이터베이스 이름 변경.\n\
사용법: %s renamedb [옵션] <원본 데이터베이스 이름> <대상 데이터베이스 이름>\n\
\n\
옵션:\n\
  -E, --extended-volume-path=PATH 확장된 볼륨들을 copy/rename을 위한 디렉터리 경로,기본값은 원 볼륨과 같은 경로 혹은  control-file에 주어진 경로입니다.\n\
  -i, --control-file=FILE         여러 장소에 있는 볼륨들을 copy/rename을 위한 제어 파일\n\
  -d, --delete-backup             백업 볼륨을 지움(기본값: 지우지 않음)\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 %s 프로세스가 이미 실행 중입니다\n
12 마스터 서버 시작\n
13 마스터 서버 종료\n
14 로컬 호스트 이름을 찾을 수 없습니다\n
15 인자 파일을 찾을 수 없습니다\n
16 마스터 프로세스를 초기화할 수 없습니다\n
17 서버 %s는 %d 분 내에 shutdown 합니다
18 서버 %s는 shutdown됩니다.\n이 작업은 몇 분 걸릴 수 있습니다. 잠시 기다리십시오.
19 서버 %s를 찾을 수 없습니다
20 마스터 서버는 %d 분 내에 shutdown 합니다\n
21 복제 서버 %s는 shutdown됩니다.\n이 작업은 몇 분 걸릴 수 있습니다. 잠시 기다리십시오.
22 복제 에이전트 %s는 shutdown됩니다.\n이 작업은 몇 분 걸릴 수 있습니다. 잠시 기다리십시오.

$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 출력 파일 '%s' 을(를) 열 수 없습니다\n
60 \
optimizedb: 데이터베이스 최적화.\n\
사용법: %s optimizedb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -n, --class-name=NAME        해당 클래스의 통계 정보를 갱신함, 기본값: 전체 클래스\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 날짜 형식 오류, 날짜 형식: 일-월-년:시:분:초(dd-mm-yyyy:hh:mm:ss) 또는 'backuptime'
20 복구 취소되었거나 에러 발생\n
60 \
restoredb: 데이터베이스 복구\n\
사용법: %s restoredb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -d, --up-to-date=DATE             주어진 시간으로 데이터베이스 상태를 복구. 날짜 형식: dd-mm-yyyy:hh:mm:ss 또는 'backuptime'\n\
      --list                        복구를 하지 않고 모든 백업 볼륨을 나열\n\
  -B, --backup-file-path=PATH       백업된 볼륨이 존재하는 디렉터리 경로\n\
  -l, --level=LEVEL                 복구에 사용될 백업의 레벨, 기본값: 0(Full backup)\n\
  -p, --partial-recovery            archive 로그가 존재하지 않으면 강제로 partial recovery 수행함\n\
  -o, --output-file=FILE            출력 메시지가 저장될 파일, 기본값: 없음\n\
  -u, --use-database-location-path  데이터베이스 위치 파일에 설정된 경로에 데이터베이스 볼륨과 로그 볼륨을 복구\n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 데이터베이스 이름이 누락되었습니다.\n
20 파일 '%s' 에 %d 만큼의 라인이 없습니다.\n
21 인터럽트... \n
22 로드가 인터럽트 됨 - 현재 트랜잭션은 커밋됩니다\n
23 로드가 인터럽트 됨 - 현재 트랜잭션은 철회됩니다\n
24 파일 `%s'를 열 수 없습니다.\n
25 \n*** 문법 검사 ***\n\n
26 %d 개의 에러가 검출되었습니다.\n
27 \n*** 객체 삽입 중 ***\n\n
28 총 %8d 개의 객체가 삽입되었습니다.\n
29 총 %8d 개의 참조된 객체가 기본 내용 값으로 입력되었습니다.\n
30 \n*** 트랜잭션 커밋 중 ***\n
31 \n*** 데이터베이스 닫는 중 ***\n
32 라인 %d:
33 '%4$s'의 속성 '%3$s'에 대한 '%1$s'을(를) '%2$s' 값으로 파싱 시 에러\n
34 %2$s의 속성 '%1$s'에 대한 도메인 에러\n
35 %2$s의 속성 %1$s에 대해 타입 %3$s은(는) set의 도메인으로 허용되지 않습니다.\n
36 %2$s의 속성 %1$s에 대해 타입 %3$s을(를) 예상했으나 set이 입력되었습니다.\n
37 %2$s의 속성 %1$s에 대해 타입 %3$s을(를) 예상했으나 %4$s이(가) 입력되었습니다.
38 존재하지 않는 클래스 %1$s이(가) %3$s의 속성 %2$s에 의해 참조되었습니다.
39 %s 클래스는 존재하지 않습니다. 튜플들은 무시되었습니다.\n
40 식별자 %1$d인 존재하지 않는 클래스는 %3$s의 속성 %2$s에 의해 참조되었습니다.\n
41 클래스 %s는 로더 입력 파일 내에서 허용되지 않습니다.\n
42 에러로 인한 중단.\n
43 경고: 기존 인스턴스를 갱신.\n
44 경고: 클래스 %2$s의 인스턴스 %1$d을(를) 재정의.\n
45 클래스 $2$s의 인스턴스 %1$d은(는) 이미 정의되었습니다.\n
46 클래스 $2$s의 인스턴스 %1$d은(는) 이미 사용되었습니다.\n
47 속성 '%s'상의 UNIQUE 위반, NULL로 변환 되었습니다.\n
48    %d 인스턴스\n
49 클래스 %s\n
50 패스워드: 
51 \n*** 클래스 통계 정보 갱신 중 ***\n
52 %s 에러가 라인 %d에서 발생('%s')\n
53 Lexical 오류
54 라인 %d: 문법 에러 at \"%s\"
55 missing 
56 in %s
57 함께 사용할 수 없는 인자 \"%s\", 와 \"%s\"\n
58 %d 인스턴스가 커밋 되었습니다\n
59 \n이전 버전의 loaddb를 수행할 수 없습니다.\n 
60 에러 parsing \"%s\", as %s type.\n
112   %d 개의 인스턴스\r
113 마지막으로 커밋된 라인 : %d
114 에러 제어 파일
115 로드 시에 에러 제어를 위한 입력 파일
120 \
loaddb: 데이터베이스에 객체 및 스키마 적재\n\
사용법: %s loaddb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -u, --user=ID                 데이터베이스 접근에 관한 사용자 이름, 기본값: "public"\n\
  -p, --password=PASS           사용자 패스워드, 기본값: 없음\n\
      --data-file-check-only    문법 체크만 수행, 기본값: 문법 체크 이후 적재\n\
  -l, --load-only               문법 체크하지 않고 적재, 기본값: 문법 체크 이후 적재\n\
      --estimated-size=NUMBER   인스턴스의 대략적인 개수, 기본값: 5000\n\
  -v, --verbose                 많은 상태 정보를 출력, 기본값: 출력하지 않음\n\
  -c, --periodic-commit=COUNT   주기적인 커밋에 대한 카운트, 기본값: 적재 중 커밋하지 않음\n\
      --no-oid                  OID 사용 안 함\n\
  -s, --schema-file=FILE[:LINE] 파일[:라인] 정보로 스키마 생성\n\
  -i, --index-file=FILE[:LINE]  파일[:라인] 정보로 인덱스 생성\n\
  -d, --data-file=FILE          적재할 데이터 파일\n\
      --error-control-file=FILE 적재 시 발생하는 에러에 대한 제어 파일\n\
      --ignore-class-file=ARG  적재하지 않을 클래스 이름이 있는 파일\n
121 \
load_noopt: 이 도구는 더 이상 추가 지원되지 않습니다. 사용에 유의 바랍니다.\n\
사용법: load_noopt [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -u, --user=ID                 데이터베이스 접근에 관한 사용자 이름, 기본값: "public"\n\
  -p, --password=PASS           사용자 패스워드, 기본값: 없음\n\
      --data-file-check-only    문법 체크만 수행, 기본값: 문법 체크 이후 적재\n\
  -l, --load-only               문법 체크하지 않고 적재, 기본값: 문법 체크 이후 적재\n\
      --estimated-size=NUMBER   인스턴스의 대략적인 개수, 기본값: 5000\n\
  -v, --verbose                 많은 상태 정보를 출력, 기본값: 출력하지 않음\n\
  -c, --periodic-commit=COUNT   주기적인 커밋에 대한 카운트, 기본값:적재 중 커밋하지 않음\n\
      --no-oid                  OID 사용 안 함\n\
  -s, --schema-file=FILE[:LINE] 파일[:라인] 정보로 스키마 생성\n\
  -i, --index-file=FILE[:LINE]  파일[:라인] 정보로 인덱스 생성\n\
  -d, --data-file=FILE          적재할 데이터 파일\n\
      --error-control-file=FILE 적재 시 발생하는 에러에 대한 제어 파일\n


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 cached-pages 값이 유효하지 않습니다.\n
42 cached-page-size 값이 유효하지 않습니다.\n
43 %d 개의 객체가 덤프 되었습니다.\n
44 클래스 %s
45 , %d 개의 인스턴스.\n
46 %d 개의 객체가 (전체 %d 객체) 성공적으로 덤프 되었습니다.\n
47 출력 디렉터리 이름이 너무 깁니다.\n
48 LSA : %d %d \n
60 \
unloaddb: 데이터베이스에서 스키마와 객체를 언로드\n\
사용법: %s unloaddb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -i, --input-class-file=FILE 테이블 이름이 기입된 파일, 기본값: 모든 클래스\n\
      --include-reference     참조되는 테이블 포함, 기본값: 허용하지 않음(-i가 지정 되야 함)\n\
      --input-class-only      명세한 클래스만 처리 , 기본값: 허용하지 않음(-i가 지정 되야 함)\n\
      --lo-count=COUNT        하나의 디렉터리에 저장할 lo 파일 개수, 기본값: 0\n\
      --estimated-size=NUMBER 인스턴스의 대략적인 개수, 기본값: 계산됨\n\
      --cached-pages=NUMBER   cached 페이지 개수, 기본값: 계산됨\n\
  -O, --output-path=PATH      출력 디렉터리 경로, 기본값: 현재 작업 디렉터리\n\
  -s, --schema-only           스키마만 처리, 기본값: 스키마와 오브젝트 처리\n\
  -d, --data-only             오브젝트만 처리, 기본값: 스키마와 오브젝트 처리\n\
      --output-prefix=PREFIEX 출력 파일의 prefix, 기본값: 데이터베이스 이름 사용\n\
      --hash-file=FILE        해쉬 파일이름, 기본값: 파일을 생성하기 위해 tmpnam()을 사용함\n\
  -v, --verbose               많은 상태 메시지를 출력; 기본값: 출력 안 함\n\
      --use-delimiter         식별자 처음과 끝에 '"' 사용; 기본값: 사용 안 함\n\
  -S, --SA-mode               독립 모드 실행\n\
  -C, --CS-mode               클라이언트 서버 모드 실행\n


$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 패스 1\n\n
12 %d 오브젝트가 처리되었습니다.\n
13 패스 2\n\n
14 클래스 %s\n
15 oid:  %d|%d|%d,\n
16 %d 인스턴스.\n
17 객체 갱신 중\n
18 ref_oid:  %d.%d.%d, ref_class_oid:  %d.%d.%d\n
19 객체를 변환할 수 없습니다\n
20 클래스의 힙 파일이 없습니다\n
21 객체를 갱신하지 못했습니다\n
22 %d 개의 객체 (전체 %d 객체)이 성공적으로  처리되었습니다.\n
60 \
compactdb: 데이터베이스에서 사용되지 않는 영역을 해제.\n\
사용법: %s compactdb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -v, --verbose               많은 상태 정보 출력; 기본값: 출력 안 함\n\


$set 15 MSGCAT_UTIL_SET_COMMDB
21 CUBRID master가 호스트 %s 에서 수행 중입니다. %s 에 시작되어  
22 %d 개의 요청을 처리 하였습니다. \n현재 %d 개의 서버가 동작 중입니다.\n
23 %s 분 내에 SHUTDOWN 합니다!!!\n
24 %s
25 Shutdown 할 서버의 이름 :
26 Shutdown 할 때까지의 시간(분)? :
27 \
cub_commdb: This utility is deprecated. Use 'cubrid' command instead.\n\
사용법: cub_commdb [옵션]\n\
\n\
옵션:\n\
  -P, --server-list                서버 프로세스 출력\n\
  -R, --repl-list                  복제 프로세스 출력\n\
  -O, --all-list                   모든 프로세스 출력\n\
  -S, --shutdown-server=NAME       해당 데이터베이스 서버 shutdown\n\
  -K, --shutdown-repl-server=NAME  해당 복제 서버 shutdown\n\
  -k, --shutdown-repl-agent=NAME   해당 복제 agent shutdown\n\
  -A, --shutdown-all               모든 프로세스 shutdown\n\
  -h, --host=HOST                  해당 호스트의 마스트 서버로 접속\n
28 알 수 없는 명령입니다: %s\n
29 호스트 이름을 찾을 수 없습니다-중지\n
30 인자 파일을 찾을 수 없습니다\n
31 %s 에서 마스터 서버로 연결할 수 없습니다\n
32 얼마 후에 shutdown 하시겠습니까?\n
33 마스터는 %d 분 후에 shutdown 됩니다.\n
34 클라이언트로부터의 데이터 수신 에러
35 한글 database 서버 %s 은(는) 현재 수행 중이 아닙니다.
36 쉘 (%s) 를 수행할 수 없습니다. %s, %s 
37 프로세스 fork %s, 반환 %d, %s
38 %s을(를) 수행할 수 없습니다


$set 16 MSGCAT_UTIL_SET_PATCHDB
60 \
emergency_patchlog: 긴급한 상황에서 데이터베이스 로그 패치\n\
사용법: %s emergency_patchlog [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -r, --recreate-log           로그 파일 재생성\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 페이지 개수 값 %d가 잘못되었습니다\n
21 볼륨 저장소이름 '%s'가 잘못되었습니다\n
60 \
addvoldb: 데이터베이스 볼륨 파일을 추가\n\
사용법: %s addvoldb [옵션] <데이터베이스 이름> number-of-pages\n\
\n\
옵션:\n\
  -n, --volume-name=NAME       볼륨 이름, 기본값: "db"_ext1 과 같은 이름을 생성\n\
  -F, --file-path=PATH         데이터베이스 파일에 대한 볼륨 패스, 기본값: 현재 디렉터리\n\
      --comment=COMMENT        볼륨 주석, 기본값: 없음\n\
  -p, --purpose=PURPOSE        볼륨 사용 용도; 다음과 같은 값이 허용됨\n\
                                       DATA - 데이터 볼륨\n\
                                       INDEX - 인덱스 볼륨\n\
                                       TEMP - 임시 데이터 볼륨\n\
                                       GENERIC - 임의 데이터 볼륨\n\
  -S, --SA-mode                독립 모드 실행\n\
  -C, --CS-mode                클라이언트 서버 실행\n

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 데이터베이스에서 비 일관성이 발견되었습니다.\n추가적인 정보는 error_log_file %s 을(를) 참고 하십시오\n
60 \
checkdb: 데이터베이스 일관성 검사.\n\
사용법: %s checkdb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -S, --SA-mode                독립 모드 실행\n\
  -C, --CS-mode                클라이언트 서버 실행\n\
  -r, --repair                 비 일관성 발견 시 복구\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
15 데이터베이스 '%s'의 (페이지 크기 %d) 볼륨 정의\n\n
16 %5d %8s  %s %s  %s\n
17 출력파일 `%s' 을(를) 열 수 없습니다\n
18 \n데이터베이스 '%s'의 (페이지크기 %d) 임시 볼륨\n\n
19 번호  용도       페이지수  여유페이지  볼륨이름\n\n
20 번호  용도       전체크기    여유공간  볼륨이름\n\n
60 \
spacedb: 데이터베이스 디스크 공간 정보 출력\n\
사용법: %s spacedb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -o, --output-file=FILE       파일로 출력을 재지정; 기본값: 재지정 하지 않음\n\
  -S, --SA-mode                독립 모드 실행\n\
  -C, --CS-mode                클라이언트 서버 모드 실행\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               출력 단위(page, mega, giga, tera, human readable) 설정;\n\
                               기본값:PAGE\n


$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 Estimated Number of Pages = %d\n
60 \
estimate_data: 데이터 크기 추정\n\
사용법: %s estimate_data number-of-instances average-instance-size number-of-attributes variable-number-of-attributes\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 잘못된 인덱스 키 타입입니다  = %s\n
16 평균 키 값이 잘못되었습니다 length = %d, precision = %d\n
17 잘못된 인자가 있습니다\n
20 예상 전체 인덱스 페이지 수 = %d\n(인스턴스가 만들어진 이후 인덱스가 생성되는 경우)\n
21 예상 전체 인덱스 페이지 수 = %d\n(인스턴스가 만들어지기 전에 인덱스가 생성되는 경우(average case))\n
22 예상 전체 인덱스 페이지 수 = %d\n(인스턴스가 만들어지기 전에 인덱스가 생성되는 경우(worst case))\n
23 주어진 입력에 따른 값...\n
24 인스턴스의 총 개수  : %d\n
25 서로 다른 키의 개수 : %d\n
26 평균 키 크기        : %d\n
27 키 타입             : %s\n\n
60 \
estimate_index: 인덱스 크기 추정\n\
사용법: %s estimate_index number-of-instances number-of-different-keys average-key-size key-type\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: 데이터베이스 설치\n\
사용법: %s installdb [옵션] <데이터베이스 이름>\n\
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
7 ELO 입력 파일 "%s" 을(를) 여는데 에러\n
8 클래스:
9 해쉬 파일 이름
10 이름:                          %s\n
11 크기:                          %d\n
12 페이지 크기:                   %d\n
13 데이터 크기:                   %d\n
14 엔트리 크기:                   %d\n
15 페이지당 엔트리들:             %d\n
16 Cached 페이지:                 %d\n
17 엔트리의 개수:                 %d\n
18 collision 개수:                %d\n
19 해쉬 파일 이름:                %s\n
20 다음 오버플로우 엔트리:        %d\n
21 키 타입:                       %s\n
22 페이지 헤더의 주소:            %x\n
23 마지막 페이지 헤더의 주소:     %x\n
24 여유 페이지 헤더의 주소:       %x\n
25 페이지 비트맵의 주소:          %x\n
26 페이지 비트맵 크기:            %d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
60 \
diagdb: 데이터베이스 정보 덤프\n\
사용법: %s diagdb [옵션] <데이터베이스 이름>\n\
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
                                8 로그 덤프\n


$set 25 MSGCAT_UTIL_SET_LOCKDB
15 출력 파일 `%s'을 열 수 없습니다.\n
60 \
lockdb: 데이터베이스의 lock 정보 출력\n\
사용법: %s lockdb [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -o, --output-file=FILE       출력 메시지를 재지정할 파일; 기본값: 없음\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 다음 인자 중 하나만 허용: 트랜잭션-인덱스, 사용자, 호스트, 프로그램\n
21 DBA 비밀 번호:
22 트랜잭션인덱스  사용자이름     호스트이름     프로세스 ID     프로그램이름\n
23 -------------------------------------------------------------------------------\n
24 %7d(%1c) %14s %14s %15d %17s\n
25 트랜잭션이 없습니다\n
26 주어진 인자에 해당하는 트랜잭션이 없습니다\n
27 다음 트랜잭션들을 종료시킬 준비가 되었습니다:\n\n
28 계속하시겠습니까? (Y/N)
29 트랜잭션 인덱스 %d로 지정된 트랜잭션들을 종료시킵니다\n
30 다음 트랜잭션들의 종료에 실패했습니다\n\n
31 트랜잭션이 종료되지 않았습니다. 롤백 진행 중일 수 있습니다.\n 잠시 기다렸다가 확인하고 재시도하시기 바랍니다.\n
60 \
killtran: 데이터베이스 transaction 제거 관리\n\
사용법: %s killtran [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -i, --kill-transaction-index=INDEX로 지정되는 해당 트랜잭션 종료\n\
      --kill-user-name=ID            ID로 지정되는 사용자의 모든 트랜잭션 종료\n\
      --kill-host-name=HOST          HOST로 지정되는 클라이언트의 모든 트랜잭션 종료\n\
      --kill-program-name=NAME       NAME으로 지정되는 모든 트랜잭션 종료\n\
  -p, --dba-password=PASS            DBA 패스워드; 지정되지 않은 경우 프롬프트\n\
  -d, --display-information          현재 활성화된 트랜잭션에 대한 정보 보여줌\n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1  데이터베이스-이름
2  필수 인자, 기본값 없음
3  호환성 검사
4  호환성 검사만 수행
5  백업 경로 
6  백업 볼륨을 저장할 디렉터리 경로나 디바이스 이름
20 데이터베이스 '%s'가 존재하지 않습니다.
21 '%s'는(은) 버전이 너무 낮아 %s으로 마이그레이션 할 수 없습니다.\n
22 '%s'는(은) 마이그레이션이 필요 없습니다.\n
23 '%s'는(은) 마이그레이션이 필요합니다.\n
24 마이그레이션 후 백업하는 것을 권장합니다. 지금 백업하시겠습니까? (y/n): 
25 마이그레이션 후 백업하는 것을 권장합니다. 
26 \n*** 경고: %s 으로 마이그레이션 수행 전에 데이터베이스를 백업하십시오 ***\n\n마이그레이션 중 발생할 수 있는 장애를 복구하기 위해서는 반드시 백업이 필요합니다.\n백업 하셨습니까? (y/n): 
27 \n1. backupdb 실행 \n2. 새로운 데이터베이스에 대한 새로운 백업 경로 입력. (현재 경로: %s) \n3. 종료 \n
28 \n새로운 데이터베이스에 대한 백업 경로: 
29 \n마이그레이션 및 백업을 정상적으로 수행했습니다.
30 백업에 실패했습니다. 다시 백업하십시오.
31 \n\n*** 경고: 마이그레이션 완료 이후에는 이전 버전의 백업 파일을 이용하여 복구할 수 없습니다.\n이전 버전의 백업 파일은 해당 버전에서만 복구가 가능합니다.\n이전 버전의 백업 파일을 보존하십시오. ***\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: 데이터베이스의 호스트 변경\n\
사용법: %s alterdbhost [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -h, --host=HOST              호스트 이름; 기본값: 현재 호스트 이름\n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 database-name
2 필수 인자, 기본값 없음
3 java-class-file
4 필수 인자, 기본값 없음
5 force-overwrite
6 클래스 파일이 이미 존재하면 덮어씀, 기본값:덮어쓰지 않음

$set 35 MSGCAT_UTIL_SET_REPLSERVER
1 데이터베이스 이름
2 포트 번호
3 에러 로그 파일
4 지원 가능한 에이전트의 수
11 내부오류(%d): 통신 오류가 발생했습니다. 
12 내부오류(%d): 메모리를 할당 받지 못했습니다. 
13 내부오류(%d): IO 오류가 발생했습니다. 
14 내부오류(%d): 일반 오류
21 오류(%d): MASTER DB의 액티브 로그 파일을 읽을 수 없습니다. - %s
22 오류(%d): databases.txt 파일을 읽을 수 없습니다.
23 오류(%d): %s 가 databases.txt 파일에 등록되어 있지 않습니다.
24 오류(%d): 카탈로그 파일을 찾을 수 없습니다. 
25 오류(%d): 파라미터 입력이 잘못되었습니다. 
26 오류(%d): master에 접속할 수 없습니다. 동일한 repl_server가 구동되어 있는지 master가 구동되어 있는지 확인하시기 바랍니다.
27 오류(%d): 포트 번호가 이미 사용되고 있습니다. 동일한 포트 번호를 사용하고 있는 repl_server가 이미 구동되고 있는지 확인하시기 바랍니다.
28 안내(%d): 동시 접속 가능한 repl_agent의 수를 초과하였습니다. 신규로 들어온 요청은 무시합니다.
29 안내(%d): repl_agent 접속이 종료되었습니다.
30 안내(%d): repl_agent로부터의 요청을 받아들일 수 없습니다. 나중에 요청된 항목은 무시하였습니다.
31 오류(%d): 보관 로그 파일을 열 수 없습니다. - %s
32 오류(%d): 보관 로그 파일을 읽을 수 없습니다. - %s

$set 36 MSGCAT_UTIL_SET_REPLAGENT
1 데이터베이스 이름
2 비밀 번호
3 보관 로그 생성 모드
4 로그 덤프 
5 페이지 크기
11 내부오류(%d): 통신 오류가 발생했습니다. 오류 코드를 기술지원팀으로 보내주시기 바랍니다. 
12 내부오류(%d): 메모리를 할당 받지 못했습니다. 
13 내부오류(%d): IO 오류가 발생했습니다. 
14 내부오류(%d): 오류 코드를 기술지원팀으로 보내주시기 바랍니다.
15 내부오류(%d): 로그압축해제에 실패했습니다.
16 내부 오류(%d): 쿼리 에러:%s
21 오류(%d): repl_server에 접속할 수 없습니다. repl_server의 상태와 포트번호를 확인하시기 바랍니다.
22 오류(%d): 로그복사본 파일을 열 수 없습니다. - %s
23 오류(%d): 로그복사본에 대한 보관 파일을 생성할 수 없습니다. -%s
24 오류(%d): 카탈로그 파일을 찾을 수 없습니다. 
25 오류(%d): 트레일 로그 파일에 접속할 수 없거나 데이터를 읽을 수 없습니다. - %s
26 오류(%d): master에 접속할 수 없습니다. 동일한 repl_agent가 구동되어 있는지 master가 구동되어 있는지 확인하시기 바랍니다.
27 오류(%d): SLAVE DB에 로그인할 수 없습니다. - %s
28 오류(%d): SLAVE DB에 접속할 수 없습니다. - %s
29 오류(%d): SLAVE DB가 정지하였습니다. - %s
30 오류(%d): 복제 오류가 발생했습니다. MASTER DB와 재동기화가 필요합니다. - %s
31 오류(%d): repl_server로부터 ID를 받을 수 없습니다. repl_server에 장애가 발생했는지 확인하시기 바랍니다.
32 오류(%d): repl_server로부터 로그 헤더 정보를 받을 수 없습니다. repl_server에 장애가 발생했는지 확인하시기 바랍니다.
33 오류(%d): repl_server로부터 로그 페이지를 받을 수 없습니다. repl_server에 장애가 발생했는지 확인하시기 바랍니다.
34 오류(%d): 배포 DB로부터 필요한 정보를 찾지 못했습니다.
35 오류(%d): 배포 DB에 로그인할 수 없습니다. - %s
36 오류(%d): 배포 DB에 접속할 수 없습니다. - %s
37 오류(%d): 배포 DB의 slave_info 클래스에 너무 많은 SLAVE DB가 등록되어 있습니다. 
38 오류(%d): SLAVE DB <-> MASTER DB 간 복제 관계가 정의되지 않았습니다. 배포 DB의 trail_info 클래스를 확인하여 주시기 바랍니다.
39 오류(%d): 배포 DB의 master_info 클래스에 너무 많은 MASTER DB가 등록되어 있습니다. 
40 오류(%d): 배포 DB의 trail_info 클래스를 점검하여 주십시오.
41 오류(%d): 트랜잭션이 철회되었습니다. 설정 파일의 (cubrid.conf) 의 max_block_size 및 max_block_count를 조정하여 워크스페이스의 용량을 늘려 주십시오.

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 출력 파일 `%s'을 열 수 없습니다\n
60 \
plandump: 쿼리 플랜 표시 및 관리\n\
사용법: %s plandump [옵션] <데이터베이스 이름>\n\
\n\
옵션:\n\
  -d, --drop                   서버 케시에 있는 모든 플랜 삭제\n\
  -o, --output-file=FILE       출력 메시지를 재지정할 파일; 기본값: 없음\n
