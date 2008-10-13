$
$ Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
$
$ $Id$
$
$ esql.msg
$

$set 1
1 "%s"은(는) integer형 인자 값이 아닙니다\n
2 "%s"은(는) float형 인자 값이 아닙니다\n
3 인자 %s 가 빠져 있음\n
4 [옵션] 
5 옵션: \n
6 key %s 에 대한 인자 값이 빠져 있음\n
7 "%s" 은(는) 사용할 수 없는 옵션임\n
8 "%s" 은(는) 잘못 지정된 인자임\n

$set 2
1 커서 "%s"이(가) 재정의 되었음
2  *\t : %s \n
3  *\n * 커서 테이블:\n

$set 3
1 (set_class_bit에서 내부 에러 발생): 스토리지 클래스 부적합 (%d)
2 (add_spec_to_decl()에서 내부 에러 발생): 잘못 형성된 체인 (no specifier)
3 현재의 문맥에서 스토리지 클래스 명세가 허용되지 않는다
4 잘못된 스토리지 클래스 조합
5 잘못된 타입 조합 
6 잘못된 변경자 조합
7 (pp_add_type_spec()에서 내부 에러 발생): 예측되지 않은 상황(%d)
8 (pp_add_type_adj()에서 내부 에러 발생): 예측되지 않은 상황(%d)
9 %s : 부적합한 선언

$set 4
1 %s
2 %s: 은(는) 디렉터리가 아님\n
3 %s: 은(는) 잘못 설치된 루트?\n

$set 5
1 ?
2 토큰이 정확한지 확인 하십시오
3 static 커서 "%s"에 대해서는 USING절을 사용할 수 없습니다.
4 커서 \"%s\" 은(는) 정의가 안되어 있습니다.
5 디스크립터에 대한 포인터
6 DB_OBJECT에 대한 포인터
7 문자열
8 "%s" 을(를) 위한 지시 변수는 허용이 안됩니다.
9 "%s" 은(는) BEGIN DECLARE section에서 선언이 안되어 있습니다.
52 %s ( "%s" )
53 Lexical 오류
54 "%s"에서 문법 에러 : %s가 %s에 없음
55 "%s"에서 문법 에러 : %s가 없음

$set 6
1 suppress-line-directive
2 output-file
3 include-file
4 dump-scope-info
5 dump-malloc-info
6 announce-version
7 enable-uci-trace
8 disable-varchar-length
9 varchar2-style
10 unsafe-null
11 internal-indicator
12 input-file

$set 7
1 출력 파일 안에서  #line 명령어를 보이지 않게 함, 디폴트는 off
2 출력 파일명, 디폴트는  <input-file>.c 또는 stdout
3 uci_ 프로토타입들을 포함한 include 파일, 디폴트는 "esqlx.h"
4 scope 디버깅 정보를 출력, 디폴트는 off
5 최종 malloc 디버깅 정보 출력, 디폴트는 off
6 버전 정보를 출력, 디폴트는 off
7 uci_ 함수 수행 정보 출력, 디폴트는 off
8 VARCHAR 호스트 변수의 length 초기화 안 함, 디폴트는 off
9 다른 VARCHAR 유형 사용, 디폴트는 off
10 ignore -462(null indicator needed) error, default off
11 use internal NULL indicator to prevent -462(null indicator needed) error, default off
12 입력파일명, 디폴트는 stdin

$set 8
1 REPEATED 옵션이 무시됨
2 %s 버전 %s\n

$set 9
1 (yy_pop_mode) 내부 오류 : 스택이 비어 있음
2 "%s" 은(는) 이 문맥 안에서 허용이 안됩니다

$set 10
1 %s 안에서의 내부 오류: %s\n
2 테이블 오버플로우

$set 11
1 지시자 변수 "%s" 은(는) short int 타입이 되어야 함
2 구조체와 함께 지시자 변수를 사용할 수 없음
3 "%s" 은(는) 불완전한 구조체 정의를 갖고 있음
4 "%s" 은(는) 유효한 %s 이어야 함
5 %s 은(는) "%s" 을(를) 위한 유효한 타입이 아님
6 예상되지 않은 호스트 변수 타입: %s
7 (pp_addr_expr): NULL 변수의 주소 접근 시도
8 "%s" 은(는) 포인터가 아님
9 "%s" 은(는) 구조체를 가리키지 않는다
10 "%s" 은(는) 구조체가 아님
11 %s 은(는) 필드 "%s" 을(를) 가지지 않음
12 "%s" 은(는) "%s"에 의하여 dereference 될 수 없습니다.

$set 12
1 메모리 부족

$set 13
1 함수들의 배열이 잘못되어 있습니다.
2 (pp_the_same_type) 알 수 없는 링크 클래스
3 (pp_get_sizeof): 틀린 명사 (%d)

$set 14
1 (%s에서 내부 에러): 불가능한 상황
