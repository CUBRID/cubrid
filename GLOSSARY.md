# Workspace OOS terminology

이 용어집은 workspace 저장과 OOS 변환을 설명하는 용어를 정리한다.
OOS의 규범적 정의는 [OOS 명세](/home/vimkim/gh/cubrid-oos-context/OOS-CONTEXT.md)를 따른다.

## Language

**Workspace 객체**:
데이터베이스 객체의 속성 값과 식별·변경 상태를 메모리에서 관리하는 표현이다.
_Avoid_: 디스크 레코드와 동일한 의미로 사용하는 객체

**클래스 표현 정보 (class representation)**:
특정 스키마 버전에 속하는 객체의 속성과 저장 구조를 설명하는 정보이다.
_Avoid_: 현재 행의 속성 값

**직렬화된 레코드**:
객체의 헤더와 속성 값을 저장 형식의 바이트로 표현한 레코드이다.
_Avoid_: 메모리 객체와 동일한 의미로 사용하는 레코드

**변수 영역 오프셋 테이블 (VOT)**:
레코드의 가변 길이 속성 위치와 관련 표시를 담은 테이블이다.
_Avoid_: 속성의 논리적 값 목록

**OOS inline stub**:
별도로 저장된 OOS 값을 참조하는 레코드 내부 표현이다.
_Avoid_: stub 전체를 가리키는 OOS OID

**OOS demotion**:
저장 시 선택한 속성 값을 OOS로 옮기고 레코드 내부 값을 OOS inline stub으로 교체하는 변환이다.
_Avoid_: 모든 가변 길이 속성을 무조건 분리한다는 의미의 변환
