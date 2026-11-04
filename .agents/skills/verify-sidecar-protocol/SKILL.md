---
name: verify-sidecar-protocol
description: Check Sidecar's proposed value lifetime, historical WAL, replica transaction, and undo-resource protocols with bounded TLA+ models. Use when changing the Sidecar design or its verification models before implementation. These checks do not prove engine conformance.
---

# Verify the proposed Sidecar protocol

Run from the CUBRID checkout root:

```sh
python3 .agents/skills/verify-sidecar-protocol/scripts/check.py
python3 .agents/skills/verify-sidecar-protocol/scripts/check.py --matrix qualification
```

Use `--model lifetime`, `history`, `undo`, or `boundary` for a restricted run.
Its result covers only that family. The default quick matrix includes safe
cases, deliberate unsafe controls, and reachability witnesses. Qualification
also requires the larger frozen configurations.

Python 3.10 or newer and Java are required. Put TLA+ tools 1.7.4 at
`~/.cache/tlaplus/tla2tools-1.7.4.jar`, or set `TLA2TOOLS_JAR`. The runner
checks SHA-256 `936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88`.
Set `JAVA` to choose an executable. The runner installs nothing.

Read [the protocol and scope](references/protocol.md) before interpreting a
pass. Each model's `coverage.md` maps its states and actions to that proposal,
documents imported assumptions, and records its exact finite bounds.
Read every TLA+ result with `/statechart`. Interpret a named violation as a
path through the model, not as an established engine failure.

## Read the verdict

The command prints a fresh output directory and writes `report.json`, frozen
input copies and hashes, effective configurations, exact commands, state
counts, and full TLC logs. A safe case must exhaust its state space and finish
all configured temporal checks. An unsafe control or witness succeeds only
when its exact named property fails. Expected controls are not new bugs.

Exit 0 means every selected case matched. Exit 1 means an unexpected semantic
result. Exit 2 means invalid input, tool failure, timeout, or incomplete work.
A timeout, deadlock, parser error, wrong-property failure, and partial search
are never successful verification. No state/depth constraints or symmetry
reductions are accepted.

The independent [requirements inventory](references/requirements.json) fixes
the promises, required witnesses, and exact safe configuration constants.
Removing a required safe profile, scenario, or witness, or changing a frozen
specification, check list, or bound requires a reviewed inventory change.
An unfinished qualification run cannot silently become a pass.
Keep the runner source unchanged while a run executes; its saved source hash
is recorded at startup. Java launcher hashing does not pin the full runtime;
read the runtime version in the TLC logs.

The boundary model imports the actual frozen history predicates. It does not
prove action refinement of the detailed lifetime or undo models. Physical
resource policies are proposed guards. Keep the existing
`verify-heap-vacuum-reuse` result separate; its legacy forward-slot finding is
not fixed by a pass here.

## Change the verification

Keep canonical expected values independent of writers, decoders, and undo
journals. For every semantic property, keep a targeted unsafe control and a
reachable schedule. Progress properties need explicit fairness, availability,
and nonstuttering fair-livelock controls under the same premises.

Use the affected family for iteration. Run qualification before handoff; it
includes every quick case, so an unchanged quick matrix needs no separate
rerun after completed qualification.
Keep counterexamples. Resolve whether they expose the protocol or a modeling
mistake before changing an action. Do not remove a promised property to get a
pass. Run `python3 .agents/skills/verify-sidecar-protocol/scripts/selftest.py`
when changing the runner's input or verdict handling.

Use `/maintain-formal-verification-skill` after related design or engine work
that did not recheck these mappings. Engine implementation requires a fresh
refinement/source review and real recovery, HA, and regression tests.

## Coverage record

Design basis is documentation commit
`12b1d0d20a0669ff0401c8290e672d65198aaf0d`, refined by the checked protocol
reference in this skill. Engine context is
`e64a04a79a5842827a124b3f8313516d0da2594c`. These are design-level models;
the runner does not extract or certify engine code. Executed model/config
hashes in each report identify the precise checked version. Move design and
source correspondence records only after reading and validating the mapping.
