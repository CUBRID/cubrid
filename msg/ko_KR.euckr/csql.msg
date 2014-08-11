
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
$ csql.msg
$

$set 1 MSGCAT_CSQL_SET_CSQL
40 \
대화식 SQL 도구, 버전 %1$s\n\
사용법: %2$s [옵션] 데이터베이스-이름[@호스트]\n\
\n\
유효한 옵션들:\n\
  -S, --SA-mode                독립 모드 실행\n\
  -C, --CS-mode                클라이언트 서버 모드 실행\n\
  -u, --user=ARG               사용자 이름\n\
  -p, --password=ARG            비밀번호 문자열, 없는 경우 ""\n\
  -e, --error-continue         에러 발생 시 계속 수행\n\
  -i, --input-file=ARG         입력 파일 이름\n\
  -o, --output-file=ARG        출력 파일 이름\n\
  -s, --single-line            단일 라인 실행\n\
  -c, --command=ARG            CSQL 명령어들\n\
  -l, --line-output            한 라인 내에 값 표시\n\
  -r, --read-only              읽기 전용 모드\n\
      --string-width           각 문자열 컬럼 값을 출력할 길이\n\
      --no-auto-commit         자동 커밋 안 함\n\
      --no-pager               페이지 단위로 표시 안 함\n\
      --no-single-line         단일 라인 실행 안 함\n\
      --no-trigger-action      트리거 실행 안함\n\
\n\
추가적인 정보는 http://www.cubrid.org 에서 얻을 수 있습니다.\n
41 %1$s: 환경 변수 %2$s_MODE 의 값이 틀림. \n%3$s 또는 %4$s 이어야 합니다.\n
42 %1$s: 너무 많은 인자가 주어짐. 최대 개수는 %2$d 입니다.\n
43 %1$s: 환경 변수 ($CUBRID) 가 설정되어야 합니다.\n
44 %1$s: "%2$s"  실행 실패.\n
45 %1$s: -S와 -C 옵션을 동시에 지정될 수 없습니다.\n
46 %1$d 개의 명령어가 실행되었습니다.
47 라인 %1$d,
48 라인 %1$d, 열 %2$d,
49 도움말 메시지를 보려면 ';help' 명령어를 입력하세요.
50 에러: 
51 CUBRID SQL 수행기
52 데이터베이스가 변경 되었습니다. 커밋 하겠습니까 ? (`Y' or `N') 
53 커밋은 'Y', 철회는 'N' 입력. 
54 커밋 되었습니다.
55 철회 되었습니다.
56 명령어 버퍼가 저장 되었습니다.
57 명령어 버퍼로 파일을 읽어 들였습니다.
58 명령어 버퍼의 내용을 프린터로 출력했습니다.
59 현재 디렉터리 변경 : 
61 비밀번호 입력 : 
62 <%1$s의 결과, 명령어 라인 %2$d>
63 결과 없음.
64 체크포인트가 실행되었습니다.
65 데이터베이스가 다시 시작되었습니다.
66 트랜잭션인덱스        사용자이름     호스트이름     프로세스 ID     프로그램이름\n
67 %1$4d%2$-12s %3$14s %4$14s %5$15d %6$17s\n
68 지정된 트랜잭션이 종료되었습니다.
69 지정된 트랜잭션 종료가 실패했습니다.
70 %1$d 개의 열이 %2$s.
71 %1$d 개의 열이 %2$s.
$ These 3 should be blank
75 자동화된 API 시험기
76 적합하지 않은 자동화 API 시험기
79 csql>
80 csql
81 sysadm>
82 %1$d 개의 트랜잭션이 %2$s.
83 %1$d 개의 트랜잭션이 %2$s.

145 <도움말: 클래스 스키마>
146 없음
147 <도움말: 모든 트리거>
148 <도움말: 정의된 트리거 없음>
150 <도움말: 트리거 정의>
151 <도움말: 명령어 문법>
152 <도움말: 세션 명령어 요약>
178 파일 이름이 필요합니다.
179 페이지 구분기를 실행할 수 없습니다.
180 메모리 부족.
184 입력된 라인이 너무 깁니다.
185 너무 많은 라인이 입력되었습니다.
188 너무 많은 파일명이 입력되었습니다.
190 없는 세션 명령어입니다. 도움말 메시지를 보려면 ';help'를 입력하세요.
191 세션 명령어가 모호합니다. 더 자세히 입력하세요.
193 명령어가 모호합니다. 더 자세히 입력하세요.
194 명령어 옵션 조합이 잘못되었습니다.
196 미정의 에러 발생.
197 출력이 재지정된 경우에는 ';edit' 명령어를 사용할 수 없습니다.

203 <%1$s> 
204 <상위 클래스> 
205 <하위 클래스> 
206 <속성> 
207 <클래스 속성> 
208 <메소드> 
209 <클래스 메소드> 
210 <해결>
211 <메소드 파일> 
212 <쿼리 명세> 
213 <트리거> 
214 <이름> 
215 <이벤트> 
216 <조건 시간> 
217 <조건> 
218 <액션 시간> 
219 <액션> 
220 <상태> 
221 <우선순위> 
222 <이름> 
223 <설명> 
224 <문법> 
225 <예제> 

$ csql help messages
231 \
\n\
   모든 세션 명령어는 ';'로 시작하며 앞에는 공백/탭 문자 이외에는 올 수 없습니다.\n\
   대문자로 표시된 부분은 해당 명령어를 수행하기 위해 지정되야 할 최소한의 약어입니다.\n\
\n\
   ;REAd   [<file-name>]        - 명령어 버퍼로 파일 내용을 읽어 들임.\n\
   ;Write  [<file-name>]        - 명령어 버퍼 내용을 파일에 저장.\n\
   ;APpend [<file-name>]        - 명령어 버퍼 내용을 파일에 덧붙임.\n\
   ;PRINT                       - 명령어 버퍼 내용을 프린트.\n\
   ;SHELL                       - 쉘 수행.\n\
   ;CD                          - 현재 작업 디렉터리 변경.\n\
   ;EXit                        - 종료.\n\
\n\
   ;CLear                       - 명령어 버퍼 내용을 지움.\n\
   ;EDIT                        - 명령어 버퍼 편집.\n\
   ;LISt                        - 명령어 버퍼 내용 출력.\n\
\n\
   ;RUn                         - 명령어 버퍼 실행.\n\
   ;Xrun                        - 명령어 버퍼 실행 후 버퍼 내용을 지움.\n\
   ;COMmit                      - 진행 중인 트랜잭션 커밋.\n\
   ;ROllback                    - 진행 중인 트랜잭션 철회.\n\
   ;AUtocommit [ON|OFF]         - 자동 커밋 모드 설정|해제.\n\
   ;REStart                     - 데이터베이스 재시작.\n\
\n\
   ;SHELL_Cmd    [shell-cmd]    - 내정된 쉘을 설정하거나 보여줌.\n\
   ;EDITOR_Cmd   [editor-cmd]   - 내정된 편집기를 설정하거나 보여줌.\n\
   ;PRINT_Cmd    [print-cmd]    - 내정된 프린트 명령어를 설정하거나 보여줌.\n\
   ;PAger_cmd    [pager-cmd]    - 내정된 페이지 구분기를 설정하거나 보여줌.\n\
\n\
   ;DATE                        - 지역 날짜, 시간 출력.\n\
   ;DATAbase                    - 접근 중인 데이터베이스 이름 출력.\n\
   ;SChema class-name           - 클래스의 스키마 정보 출력.\n\
   ;TRIgger [`*'|trigger-name]  - 트리거 정의 출력.\n\
   ;Get system_parameter        - 시스템 파라메터 보기.\n\
   ;SEt system_parameter=value  - 시스템 파라메터 설정.\n\
   ;STring-width [width]        - 각 문자열 컬럼 값을 출력할 길이를 설정.\n\
   ;COLumn-width [name]=[width] - 명시한 컬럼 값을 출력할 길이를 설정.\n\
   ;PLan [simple|detail|off]    - 쿼리 플랜 보기 설정.\n\
   ;Info <command>              - 내부 정보 출력.\n\
   ;TIme [ON/OFF]               - 쿼리 수행 시간 보기 설정|해제\n\
   ;LINe-output [ON/OFF]        - 한 라인 내에 값 표시 설정|해제\n\
   ;HISTORYList                 - 수행된 쿼리 리스트 보기.\n\
   ;HISTORYRead <history_num>   - 히스토리 번호에 해당되는 쿼리를 명령어 버퍼에 올림.\n\
   ;TRAce [ON|OFF] [text/json]  - 쿼리 자동 트레이스 설정|해제.\n\
   ;HElp                        - 도움말 메시지 출력.\n\n

232 <제약사항> 
233 Info {schema|trigger|deferred|workspace|lock|stats}\n\
\tschema [class-name]    - 스키마 정보\n\
\ttrigger [trigger-name] - 트리거 정보\n\
\tdeferred               - 지연 트리거 정보\n\
\tworkspace              - workspace 정보\n\
\tlock                   - lock 정보 (client-server mode)\n\
\tstats class-name       - 클래스 통계 정보\n\
\ttrantable              - 트랜잭션 정보\n\
\tlogstat                - 로그 정보\n\
\tcsstat                 - critical section 정보\n\
\tplan                   - 플랜 캐시 정보\n\
\tqcache                 - 쿼리 캐시 정보\n
235 <분할>
236 클래스 이름이 필요합니다.
