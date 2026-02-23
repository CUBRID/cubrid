# opencode-review 워크플로우 테스트 가이드

## 커밋해야 할 파일

```
cubrid/
├── .github/workflows/opencode-review.yml   ← PR 자동 리뷰 워크플로우
└── .opencode/oh-my-opencode.json           ← 에이전트 모델 설정
```

---

## Step 1 — 파일 커밋 & Fork에 Push

```bash
# 현재 브랜치에서 두 파일 추가
git add .github/workflows/opencode-review.yml .opencode/oh-my-opencode.json
git commit -m "ci: add opencode PR review workflow with oh-my-opencode"

# fork(twkang)에 push
git push twkang cbrd-26557
```

---

## Step 2 — Fork에서 GitHub Actions 활성화

> ⚠️ Fork된 레포는 GitHub Actions가 기본 비활성화 상태일 수 있음

1. https://github.com/tw-kang/cubrid 접속
2. **Actions** 탭 클릭
3. "I understand my workflows, go ahead and enable them" 버튼 클릭

---

## Step 3 — Secret 등록

> ⚠️ upstream(CUBRID/cubrid)의 Secret은 fork에서 사용 불가 — fork 레포에 별도 등록 필요

1. https://github.com/tw-kang/cubrid/settings/secrets/actions 접속
2. **New repository secret** 클릭
3. 다음 Secret 추가:

| Name | Value |
|---|---|
| `OPENCODE_API_KEY` | OpenCode Zen API 키 (https://opencode.ai/auth 에서 발급) |

---

## Step 4 — 테스트 PR 생성

> ⚠️ **중요**: upstream(CUBRID/cubrid)으로 보내는 PR은 Secret이 전달되지 않아 워크플로우가 실패함
> fork 내부(tw-kang/cubrid)의 브랜치 간 PR로 테스트해야 함

```bash
# fork에 테스트용 브랜치 생성
git checkout -b test/opencode-review-trigger
echo "# trigger" >> .github/workflows/opencode-review.yml
git add .github/workflows/opencode-review.yml
git commit -m "test: trigger opencode review workflow"
git push twkang test/opencode-review-trigger
```

그 후 GitHub에서:
- **Base**: `tw-kang/cubrid:develop` (또는 `tw-kang/cubrid:cbrd-26557`)
- **Compare**: `tw-kang/cubrid:test/opencode-review-trigger`
- PR 생성 → `opencode-review` 워크플로우 자동 트리거

---

## Step 5 — 워크플로우 확인

1. https://github.com/tw-kang/cubrid/actions 에서 `opencode-review` 실행 확인
2. 단계별 로그 확인:
   - `Install oh-my-opencode` — npx 설치 성공 여부
   - `Run opencode` — Sisyphus 에이전트 실행 여부
3. PR 코멘트에 리뷰 내용이 달리는지 확인

---

## 트러블슈팅

| 증상 | 원인 | 해결 |
|---|---|---|
| 워크플로우가 트리거되지 않음 | Actions 비활성화 | Step 2 수행 |
| `OPENCODE_API_KEY` 관련 오류 | Secret 미등록 | Step 3 수행 |
| `Missing API key` 오류 | upstream PR로 테스트 | fork 내부 PR 사용 (Step 4) |
| `oh-my-opencode` 설치 실패 | npx 버전 문제 | 로그 확인 후 패키지명 검토 |
| Sisyphus 에이전트를 찾을 수 없음 | oh-my-opencode 미로드 | opencode.json plugin 등록 확인 |
