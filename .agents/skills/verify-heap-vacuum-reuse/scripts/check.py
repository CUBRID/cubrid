#!/usr/bin/env python3
import argparse
import hashlib
import contextlib
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

SKILL = Path(__file__).resolve().parents[1]
ROOT = SKILL.parents[2]
JAR = Path(os.environ.get('TLA2TOOLS_JAR', str(Path.home() / '.cache/tlaplus/tla2tools-1.7.4.jar')))
SHA = '936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88'
COVER = ('src/query/vacuum.c', 'src/storage/slotted_page.c', 'src/storage/heap_file.c',
         'src/transaction/recovery.c', 'src/transaction/log_manager.c', 'src/transaction/log_recovery.c',
         'src/transaction/log_append.cpp', 'src/transaction/log_append.hpp',
         'src/transaction/log_page_buffer.c', 'src/transaction/log_recovery.h',
         'src/transaction/recovery.h', 'src/transaction/transaction_sr.c',
         'src/storage/page_buffer.c', 'src/storage/page_buffer.h',
         'src/storage/slotted_page.h', 'src/storage/heap_file.h', 'src/query/vacuum.h')
HOME = 'pgbuf_set_dirty (thread_p, helper->home_page, DONT_FREE);'
FORWARD = 'pgbuf_set_dirty (thread_p, helper->forward_page, FREE);\n      helper->forward_page = NULL;'
COMMIT = 'log_sysop_commit (thread_p);'


def source(path):
    return subprocess.check_output(['git', '-C', str(ROOT), 'show', f'{SYNC}:{path}'], stderr=subprocess.PIPE).decode()


def shape(text):
    start = text.index('vacuum_heap_record (THREAD_ENTRY * thread_p, VACUUM_HEAP_HELPER * helper)\n{')
    end = text.index('\n/*\n * vacuum_heap_get_hfid', start)
    body = text[start:end]
    home_pat = r'pgbuf_set_dirty \(thread_p, helper->home_page, (DONT_FREE|FREE)\);'
    matches = list(re.finditer(home_pat, body))
    if len(matches) != 1:
        raise ValueError('home dirty shape')
    home_free = matches[0].group(1) == 'FREE'
    rel_start = body.index('case REC_RELOCATION:')
    rel_end = body.index('case REC_BIGONE:', rel_start)
    rel = body[rel_start:rel_end]
    if rel.count(FORWARD) != 1 or rel.count(COMMIT) != 1:
        raise ValueError('forward release/commit shape')
    fwd_index, commit_index = rel.index(FORWARD), rel.index(COMMIT)
    forward_free = fwd_index < commit_index
    gap = (rel[fwd_index + len(FORWARD):commit_index] if forward_free
           else rel[commit_index + len(COMMIT):fwd_index])
    if gap.strip():
        raise ValueError('unsupported statements between forward release and commit')
    rel = rel.replace(FORWARD, '').replace(COMMIT, 'RELEASE_AND_COMMIT')
    body = body[:rel_start] + rel + body[rel_end:]
    body = re.sub(home_pat, 'HOME_DIRTY', body)
    normalized = text[:start] + body + text[end:]
    normalized = '\n'.join(line.strip() for line in normalized.splitlines() if line.strip())
    return home_free, forward_free, normalized


def run(repo):
    try:
        for path in COVER:
            current = (repo / path).read_text()
            baseline = source(path)
            if path == COVER[0]:
                home, forward, normalized = shape(current)
                if normalized != shape(baseline)[2]:
                    raise ValueError(path + ' changed beyond supported latch releases')
            elif current != baseline:
                raise ValueError(path + ' changed since sync commit')
    except (OSError, ValueError, subprocess.CalledProcessError) as e:
        print('OUT OF SYNC:', e)
        return 2
    try:
        if hashlib.sha256(JAR.read_bytes()).hexdigest() != SHA:
            raise ValueError('TLC jar checksum mismatch')
    except (OSError, ValueError) as e:
        print('TOOL ERROR:', e)
        return 2
    out = Path(tempfile.mkdtemp(prefix='heap-vacuum-reuse-'))
    shutil.copy(SKILL / 'model/HeapVacuumReuse.tla', out)
    vacuum_source = (repo / COVER[0]).read_text()
    begin = vacuum_source.index('vacuum_heap_record (THREAD_ENTRY * thread_p, VACUUM_HEAP_HELPER * helper)\n{')
    end = vacuum_source.index('\n/*\n * vacuum_heap_get_hfid', begin)
    function = 'static int\n' + vacuum_source[begin:end]
    template = (SKILL / 'scripts/replay.c.in').read_text()
    (out / 'replay.c').write_text(template.replace('/* EXTRACTED_FUNCTION */', function))
    try:
        compile_result = subprocess.run(['cc', '-std=c11', str(out / 'replay.c'), '-o', str(out / 'replay')], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except OSError as e:
        print('TOOL ERROR:', e)
        return 2
    if compile_result.returncode:
        (out / 'compile.out').write_text(compile_result.stdout)
        print(f'TOOL ERROR: replay compile failed: {out / "compile.out"}')
        return 2
    print(f'Source: {repo}; home release before commit={home}; forward release before commit={forward}')
    print(f'Artifacts: {out}')
    errors = 0
    for target in ('home', 'forward'):
        cfg = f'''SPECIFICATION Spec\nCONSTANTS\n  Target = "{target}"\n  HomeRelease = {'TRUE' if home else 'FALSE'}\n  ForwardRelease = {'TRUE' if forward else 'FALSE'}\nINVARIANT UndoFindsEmptySlot\nCHECK_DEADLOCK FALSE\n'''
        (out / f'{target}.cfg').write_text(cfg)
        try:
            result = subprocess.run([os.environ.get('JAVA', 'java'), '-cp', str(JAR), 'tlc2.TLC', '-workers', '1', '-config', f'{target}.cfg', 'HeapVacuumReuse.tla'], cwd=out, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        except OSError as e:
            print('TOOL ERROR:', e)
            return 2
        (out / f'{target}.out').write_text(result.stdout)
        replay = subprocess.run([str(out / 'replay'), target], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (out / f'{target}.replay.out').write_text(replay.stdout)
        if replay.returncode not in (0, 1):
            print(f'{target}: REPLAY ERROR (see {out / (target + ".replay.out")})')
            return 2
        print(f'{target}: {replay.stdout.strip()}')
        if result.returncode == 0 and 'Model checking completed. No error has been found' in result.stdout:
            if replay.returncode != 0:
                print(f'{target}: MODEL/REPLAY DISAGREEMENT')
                return 2
            print(f'{target}: PASS')
        elif result.returncode == 12 and 'Invariant UndoFindsEmptySlot is violated' in result.stdout:
            if replay.returncode != 1:
                print(f'{target}: MODEL/REPLAY DISAGREEMENT')
                return 2
            print(f'{target}: MODEL FINDING (see {out / (target + ".out")})')
            errors = 1
        else:
            print(f'{target}: MODEL OR TOOL ERROR (see {out / (target + ".out")})')
            return 2
    return errors


def release_variant(text, home_early, forward_early):
    shape(text)
    start = text.index('vacuum_heap_record (THREAD_ENTRY * thread_p, VACUUM_HEAP_HELPER * helper)\n{')
    end = text.index('\n/*\n * vacuum_heap_get_hfid', start)
    body = text[start:end]
    body = re.sub(r'pgbuf_set_dirty \(thread_p, helper->home_page, (DONT_FREE|FREE)\);',
                  HOME.replace('DONT_FREE', 'FREE') if home_early else HOME, body)
    lo, hi = body.index('case REC_RELOCATION:'), body.index('case REC_BIGONE:')
    rel = body[lo:hi]
    fwd, commit = rel.index(FORWARD), rel.index(COMMIT)
    left = min(fwd, commit)
    right = max(fwd + len(FORWARD), commit + len(COMMIT))
    order = (FORWARD, COMMIT) if forward_early else (COMMIT, FORWARD)
    rel = rel[:left] + '\n      '.join(order) + rel[right:]
    return text[:start] + body[:lo] + rel + body[hi:] + text[end:]


def self_test(repo):
    with tempfile.TemporaryDirectory(prefix='heap-vacuum-source-') as tmp:
        mirror = Path(tmp)
        for path in COVER:
            dest = mirror / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(repo / path, dest)
        vacuum = mirror / COVER[0]
        original = vacuum.read_text()
        home, forward, _ = shape(original)
        scenarios = (
            ('HOME MUTATION', release_variant(original, True, forward), True, forward),
            ('RESTORED', original, home, forward),
            ('SAFE ORDER', release_variant(original, False, False), False, False),
            ('UNSAFE FORWARD CONTROL', release_variant(original, False, True), False, True),
            ('RESTORED AGAIN', original, home, forward),
        )
        for label, text, home_fails, forward_fails in scenarios:
            vacuum.write_text(text)
            print(label + ' scratch source: ' + str(vacuum), flush=True)
            captured = io.StringIO()
            with contextlib.redirect_stdout(captured):
                result = run(mirror)
            output = captured.getvalue()
            print(output, end='')
            expected_exit = int(home_fails or forward_fails)
            expected = [('home', home_fails), ('forward', forward_fails)]
            if result != expected_exit or any(
                    f'{target}: MODEL FINDING' not in output if fails else f'{target}: PASS' not in output
                    for target, fails in expected):
                print('SELF-TEST ERROR: unexpected model result for ' + label)
                return 2
            if any(f'{target}: MOCK REPLAY' not in output for target, fails in expected if fails):
                print('SELF-TEST ERROR: expected mock injection for ' + label)
                return 2
        return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, default=ROOT)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    try:
        sync_record = re.search(r'Sync commit: `([0-9a-f]{40})`', (SKILL / 'SKILL.md').read_text())
        if sync_record is None:
            raise ValueError('SKILL.md has no valid sync commit')
        SYNC = sync_record.group(1)
        sys.exit(self_test(args.repo.resolve()) if args.self_test else run(args.repo.resolve()))
    except (OSError, ValueError) as e:
        print('OUT OF SYNC:', e)
        sys.exit(2)
