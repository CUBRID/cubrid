# Producer 07 develop 포트 인계

Producer 06의 최종 코드·증거는 이 문서를 포함하는 커밋에 있다.
정확한 SHA는 `git log -1 --format=%H -- docs/pgbuf-inspector/producer-07-handoff.md`로
확인한다. 작업 시작 HEAD는 `f8c068f771ccd141a3c2f08541fe75c84e41ce53`,
합의된 format-aligned base는 `e1e651debf6cc100172bde96603b17424f9c135a`다.

다음 세션의 요청 대상:
`/home/vimkim/temp/volmap/.scratch/pgbuf-producer-implementation/issues/07-port-and-verify-on-develop.md`.
먼저 이 문서, [producer 06 ledger](verification/06/README.md), 해당 티켓과
상위 `spec.md`를 읽는다. Producer 07은 이 세션에서 실행하지 않았다.

## 완료된 producer 06

- 테스트 전용 fixture에 `--permanent` 모드를 추가했다. 알려진 영구 VPID
  `1:577`의 native WRITE fix를 유지한 상태에서 실제 Volmap HTTP clean/dirty를
  확인하고, 실제 capacity replacement 뒤 native cache miss와 consumer의
  complete omission → `not-resident`를 연결했다.
- Partial/shared 검증은 바이트를 변조하지 않는 relay가 읽기를 지연한다.
  실제 엔진이 만든 partial scan 하나를 HTTP 요청 8개가 공유했고 omission은
  `unknown`이었다. Debug 214 / release 219개 raw record를 직접 셌다.
- 각 모드 native/실제 consumer 검사 11개, 기본 CTest 28/28(그중 inspector
  61 cases 및 기본 native 6 checks), shipped-server 브라우저 12/12가 통과했다.
- 격리된 Volmap `5dacafbb248600fd6a27b6aae8b5a1655cfa12c9` 전체 gate 통과:
  Rust/Clippy/static-musl, frontend 73 tests, browser 47 pass·기존 1 skip.
- 최종 companion CTP 실행 수·child binary/helper hash·testcase SHA는
  `verification/06/manifest.json`과 `ctp-*/`를 확인한다. 기존 04/05 및 Volmap
  증거 142개 체크섬도 재검증했다. 과거 실패와 새 시도는 ledger에 구분돼 있다.
- Standards 차단 사항 0, Spec 결함·범위 이탈 0. 짧은 HTTP request 구성 중복은
  비차단 의견으로 남겼다. 엔진의 shipped producer나 wire corpus 변경은 없다.

## 재사용할 정확한 입력

| 용도 | 경로 |
| --- | --- |
| Source | `/home/vimkim/gh/cb/CBRD-27398-pgbuf-inspector-contract` |
| Debug build | source의 `build_preset_debug_gcc` |
| Debug 검증 installation | `/tmp/producer06-install/debug` |
| Release build | source의 `build_preset_release_gcc` (RelWithDebInfo) |
| Release installation | `/home/vimkim/.cub/install/CBRD-27398-pgbuf-inspector-contract/release_gcc` |
| 검증한 consumer source | `/home/vimkim/temp/volmap-producer06-5dacafb` |
| 검증한 consumer binaries | 위 checkout의 `target/x86_64-unknown-linux-musl/{debug,release}/volmap` |
| Companion testcase | `/home/vimkim/gh/tc/CBRD-27398-pgbuf-inspector-fixtures` @ `a648d78f599504fe0c916628ea51ea3f2b5f7ca7` |
| Corpus | source의 `docs/pgbuf-inspector/v1`, v1.0 / revision 2, 109 files |
| Corpus aggregate SHA-256 | `11dbecc72e4b9dd78e22080f138c801c189c7e047c0db23af8233f44a804d5ce` |

실행 입력 JSON, build ID, 원본/설치본 hash와 실제 `ldd` 결과는 ledger 디렉터리에
있다. CMake 설치 시 RUNPATH가 바뀌므로 두 실행 파일의 바이트 hash가 다를 수
있다. Hash 하나나 version string만으로 소스 출처를 추정하지 않는다.
임시 installation은 영구 배포물이 아니므로 재개 시 존재·hash를 다시 확인한다.

기존 debug installation의 사용자 `demodb`가 실행 중이어서 종료하지 않고
별도 installation을 만들었다. 이 서버를 테스트 cleanup 대상으로 삼지 않는다.
기존 source의 dirty `cubrid-cci`와 `demodb_loaddb.log`도 보존했다.
원래 `/home/vimkim/temp/volmap`에는 사용자 변경이 계속 존재한다. 특히 명시적
LAN IPv4 runtime 허용 변경은 이번 loopback-only 티켓과 달라, 검증은 고정된
격리 checkout에서 했다. 원본 consumer 변경을 reset/stage/commit하지 않는다.

## 다음 세션 순서

1. 적용 AGENTS.md, `cubrid-build` 지침, 현재 git/worktree 상태와 develop ref를
   다시 확인한다. 최신 develop에서 별도 이슈 브랜치/worktree를 만든다.
   Source에서 `git log --reverse e1e651d..HEAD`로 전체 producer commit 범위를
   확인한다. Producer 06 마지막 커밋만 옮기면 producer 본체가 빠진다.
2. Producer/corpus/테스트를 포트하면서 BCB scalar 수명·접근, latch 한 번 읽기,
   flags 한 번 읽기, daemon 시작/종료 순서, persistent identity 수집을 develop
   소스로 다시 검증한다. 단순 cherry-pick 성공은 안전성 증거가 아니다.
3. Native page-kind ordinal이 바뀌는 지점을 확인한다. Wire의 `oos`는 예약된
   vocabulary로 유지하지만 develop producer가 이를 만들어 내면 안 된다.
4. Debug와 release를 각각 준비·빌드·설치하고 corpus/semantic/socket/security/
   deadline/native controlled-state/companion CTP를 실행한다. 로컬 재빌드에는
   사용자 지침대로 `just build`, `just build-test`를 쓴다. 공개 문서에는 CMake,
   ctest, 프로젝트 스크립트로 검증을 설명한다.
5. `TMPDIR`는 여유 있는 파일시스템의 짧은 전용 경로, `CUBRID_TMP`는 PL/inspector
   Unix socket 한도 내 짧은 경로를 쓴다. CTP service cleanup은 PID/network/mount/
   IPC namespace 안에서만 실행한다. `verification/06/ctp-*/inside.sh`가 실제
   재현 근거이며 그 installation·source·testcase 경로를 새 입력으로 바꾼다.
6. Producer 06 opt-in `--volmap-run`은 의도적으로 `feat-oos`만 받는다. Develop의
   실물 consumer 통합은 Volmap ADR 0007의 명시적 profile과 독립 disk corpus를
   확인한 뒤 companion Volmap 07에서 진행한다. Wire 통과를 develop disk-format
   지원으로 확대하지 않는다.
7. 정확한 develop source SHA, build/install 경로, corpus identity, 실제 실행 수와
   raw evidence를 Volmap 07에 돌려준다. Producer 07 리뷰·커밋 후 producer 08의
   release gate로 넘긴다. Push/PR/JIRA 게시 권한은 별도 요청에서 판단한다.

## 남은 경계

Producer 06 → producer 07 → Volmap 07의 develop 검증 순서이며 순환 의존성은 없다.
Windows 실행, 나머지 build/platform 조합과 전용 호스트 성능·수동 접근성 검증은
이번 결과로 통과 처리하지 않는다. Browser `0:0`/LRU 표시는 smoke이고 native
의미 검증은 별도 controlled/unit 증거다. Volmap work item 126은 이 세션의
producer 완료만으로 done이 되지 않는다. Producer 06 작업 기록은 work item 131이다.
