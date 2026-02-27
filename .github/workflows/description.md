# CUBRID Engine PR ↔ TC 브랜치 자동화

## 1. 개요

엔진 PR 테스트 시 다른 개발자의 TC 변경이 `develop`에 먼저 머지되면 관계없는 PR에서도 테스트 실패가 발생한다. 엔진 PR 번호 기반의 독립 TC 브랜치(`tc/pr-<N>`)를 자동 관리하여 이를 방지한다.

**목표**: PR 단위로 독립된 테스트환경 제공, 엔진 PR과 TC 변경을 트랜잭션처럼 묶어 처리

## 2. 대상 레포지토리

| 역할 | 레포지토리 |
|------|-----------|
| 엔진 | `CUBRID/cubrid` |
| TC 공개 | `CUBRID/cubrid-testcases` |
| TC 비공개 | `CUBRID/cubrid-testcases-private-ex` |

## 3. 상태 전이

```
PR Opened → TC Branch Created (tc/pr-<N>)
     ↓
CI Testing → TC Branch Used
     ↓
[TC 머지 필수 선행] → TC Branch → develop (Squash Merge)
     ↓ (성공 확인 후)
Engine PR → develop 머지 가능
     ↓
TC Branch Deleted
```

### Revert PR 흐름
```
Revert PR Opened → Original TC Merge Commit Reverted → tc/pr-<N>에 Push
     ↓
Revert PR Merged → TC Revert → develop
```

### 머지 순서 (중요)
**반드시 TC 머지 먼저, 엔진 PR 머지 나중**

```
[Step 1] TC 머지: tc/pr-N → develop (cubrid-testcases)
    ↓ (성공 확인 후)
[Step 2] Engine 머지: Engine PR → develop (cubrid)
    ↓
[Step 3] Cleanup: TC Branch Deleted
```

⚠️ **TC 머지가 선행되지 않으면 Engine PR 머지 불가** (Branch Protection 연동)
⚠️ TC 머지 없이 Engine만 머지되면 다른 사용자의 CI 파이프라인 오염

## 4. 워크플로우

### tc-branch-sync.yml
**트리거**: `pull_request_target` (opened, reopened, synchronize)

**동작**:
- PR body에서 `Reverts #N` 패턴 감지 → 일반/Revert PR 분기
- **일반 PR**: `tc/pr-<N>` 생성 (존재 시 skip)
- **Revert PR**: 원본 PR의 TC merge commit을 `git revert -m 1` 후 push
  - revert 커밋 메시지에 엔진 PR 헤더(예: `[CBRD-1234]`) 포함
  - conflict 시 PR에 수동 처리 요청 코멘트
- 실패 시 PR에 에러 코멘트

**Concurrency**: `group: tc-branch-sync-<PR>`, `cancel-in-progress: true`

### tc-branch-finalize.yml
**트리거**: `pull_request_target` (closed)

**merged == true**:
- `tc/pr-<N>` → develop squash merge 후 삭제
- squash 커밋 메시지에 엔진 PR 헤더 포함
- 머지 성공 후 삭제 실패 시 "TC Branch Finalize Failed" 코멘트

**merged == false**:
- `tc/pr-<N>` 브랜치 삭제 (GitHub REST API)
- 실패 시 "TC Branch Delete Failed" 코멘트

**Concurrency**: `group: tc-branch-finalize` (전역), `cancel-in-progress: false`

## 5. 공통 설정

- **timeout**: 10분 제한
- **GitHub App 토큰 마스킹**: `echo "::add-mask::${APP_TOKEN}"`

## 6. 인증 설정

GitHub App 생성 (권한: Contents: Write) → 두 TC repo에 설치 → Secrets 추가:

| Secret | 설명 |
|--------|------|
| `TC_APP_ID` | App 숫자 ID |
| `TC_APP_PRIVATE_KEY` | PEM 전체 내용 |

## 7. 보안

- `pull_request_target` 사용: fork PR에서도 시크릿 접근 가능하나 PR 코드를 checkout/실행하지 않음
- 입력 살균: `PR_BODY`, `PR_TITLE`을 환경변수로 전달, `${{ github.event... }}` 직접 보간 금지

## 8. CI 연동

### TC 브랜치 fallback
```bash
PR_BRANCH="tc/pr-${GITHUB_PULL_REQUEST_NUMBER}"
if git ls-remote --exit-code --heads \
     https://github.com/CUBRID/cubrid-testcases.git \
     "${PR_BRANCH}" 2>/dev/null; then
  TC_BRANCH="${PR_BRANCH}"
else
  TC_BRANCH="develop"
fi
git clone -b "${TC_BRANCH}" https://github.com/CUBRID/cubrid-testcases.git
```

### TC conflict 시 머지 차단
```bash
git fetch origin develop:develop
git merge --no-commit --no-ff develop || { git merge --abort; exit 1; }
git merge --abort
```

## 9. AGENTS.md 준수 현황

| 원칙 | sync | finalize | 상태 |
|------|------|----------|------|
| Caching | ❌ | ❌ | 미적용 |
| Concurrency | ✅ PR 단위 | ⚠️ 전역 | 준수/개선 필요 |
| Parallel Execution | ⚠️ 순차 | ⚠️ 순차 | Matrix 권장 |
| Modularization | ❌ | ❌ | Composite Action 추출 가능 |
| State & Artifact | ⚠️ | ⚠️ | JSON 미사용 |
| Least Privilege | ⚠️ | ⚠️ | 전역 permissions 누락 |
| Input Sanitization | ✅ | ✅ | 준수 |
| Immutable Dependencies | ❌ | ❌ | SHA 고정 필요 |
| No Hardcoded Creds | ✅ | ✅ | 준수 |

**주요 위반**:
- `actions/create-github-app-token@v1` → commit SHA로 고정 필요
- finalize concurrency를 PR 단위로 변경 필요
- 전역 `permissions: {}` 추가 필요

## 10. 미구현 항목 (우선순위)

| 항목 | 설명 | 우선순위 |
|------|------|----------|
| develop 자동 동기화 | 엔진 PR에 develop 머지 시 TC 브랜치도 동기화 | 중간 |
| Immutable Dependencies | Action을 commit SHA로 고정 | 높음 |
| Concurrency (finalize) | PR 단위 그룹으로 변경 | 높음 |
| 저장소 이름 불일치 | `tw-kang/...` → `CUBRID/...` | 높음 |

## 11. 장애 대응

### 일반적인 장애

| 증상 | 원인 | 조치 |
|------|------|------|
| "TC Branch Sync Failed" | GitHub App 권한/네트워크 | App 설정, Secrets 확인, 재실행 |
| TC 머지 실패 | develop 충돌 | 수동 머지 수행 |
| TC 삭제 실패 | 브랜치 보호 규칙 | 보호 규칙 확인 후 수동 삭제 |
| Revert 충돌 | TC 브랜치 변경 있음 | 수동 revert 후 코멘트 안내 |

### 수동 복구
```bash
# TC 브랜치 머지
BRANCH="tc/pr-1234"
git clone -b develop https://github.com/CUBRID/cubrid-testcases.git
cd cubrid-testcases
git fetch origin "${BRANCH}:${BRANCH}"
git merge --squash "${BRANCH}"
git commit -m "[CBRD-XXXX] Merge TC branch '${BRANCH}' into develop"
git push origin develop
git push origin --delete "${BRANCH}"

# TC 브랜치 삭제
curl -X DELETE \
  -H "Authorization: Bearer ${GITHUB_TOKEN}" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/CUBRID/cubrid-testcases/git/refs/heads/tc/pr-1234"
```

## 12. 테스트 결과 (tw-kang fork)

| 시나리오 | 결과 | PR |
|---------|------|-----|
| PR open → TC 브랜치 생성 | ✅ | #22 |
| PR merge → develop 머지 + 삭제 | ✅ | #22 |
| PR close (reject) → 브랜치 삭제 | ✅ | #26 |
| TC 브랜치 이미 존재 (skip) | ✅ | #22 |
| App 미설치 → 실패 코멘트 | ✅ | #27 |
| Revert PR → TC 자동 revert | ✅ | #28/#29 |

## 13. 제안사항

### 기능 개선
- develop 자동 동기화: 엔진 PR rebase 시 TC 브랜치도 동기화
- PR 상태 뱃지: "TC Ready" / "TC Conflict" 표시
- 머지 순서 강제: 엔진 PR 머지 전 TC PR 머지 필수화

### 아키텍처 개선
- **현재**: Direct git operations
- **제안**: Event-Driven (SQS/SNS) → 비동기 처리, DLQ, 감사 로그 통합

### 성능 최적화
- Shallow clone + filter: 20-30% 감소
- strategy.matrix: 2x 속도 향상
- 비동기 batch: Rate limit 효율화

## 14. 참고 문서

- [AGENTS.md](./AGENTS.md) — GitHub Actions 설계 철학
- [tc-branch-sync.yml](./tc-branch-sync.yml) — TC 브랜치 생성/동기화
- [tc-branch-finalize.yml](./tc-branch-finalize.yml) — TC 브랜치 머지/삭제

---

## A. 추가 고려사항

### 운영
- 주간 워크플로우 성공률 점검
- 월간 orphan TC 브랜치 정리
- GitHub App 토큰 유효성 확인
- 슬랙/이메일 실패 알림 설정

### 보안 강화
- `tc/pr-*` 브랜치 force push 제한
- TC 머지 전 승인 프로세스
- GitHub App private key 정기 교체
- Audit logging 외부 시스템 연동

### 트러블슈팅
- **401/403 오류**: GitHub App 설치, 권한, PEM 형식 확인
- **Rate Limit**: 시간당 5,000 요청 제한, exponential backoff 고려
- **머지 순서 위반**: 즉시 TC 수동 머지, 팀 알림
