#!/usr/bin/env python3
import dataclasses
import json
from pathlib import Path
import tempfile
import os
import shutil
import sys
sys.dont_write_bytecode = True
import check

jar = Path(os.environ.get('TLA2TOOLS_JAR', Path.home() / '.cache/tlaplus/tla2tools-1.7.4.jar')).resolve()
assert check.digest(jar) == check.JAR_HASH
java = shutil.which(os.environ.get('JAVA', 'java'))
assert java
root = Path(tempfile.mkdtemp(prefix='sidecar-tlc-selftest-'))
print('Self-test output:', root, flush=True)
module = '''---- MODULE Test ----
EXTENDS Naturals
CONSTANT Fault
VARIABLE x
Init == x = 0
Next == IF Fault = "deadlock" THEN FALSE ELSE x' = IF Fault = "invariant" THEN 1 ELSE x
Spec == Init /\\ [][Next]_x
TypeOK == x \\in 0..1
Safe == x = 0
EventuallyOne == <> (x = 1)
====
'''
base = check.Case('test', 'Test', 'safe', 'Test.cfg', ('quick','qualification'), 'safe', 'complete', None, (), ('TypeOK','Safe'), (), None, 'test', 20, ())

def run(name, fault, case, extra='', text=module):
    path = root / name; path.mkdir()
    (path / 'Test.tla').write_text(text)
    cfg = f'SPECIFICATION Spec\nCONSTANT Fault = "{fault}"\nINVARIANTS TypeOK Safe\n{extra}'
    (path / 'Test.cfg').write_text(cfg)
    result = check.run_case(case, path, jar, java)
    print(name, result['verdict'], result['reason'], flush=True)
    return result

assert run('safe', 'none', base)['verdict'] == 'match'
assert run('empty-init', 'none', base, text=module.replace('Init == x = 0', 'Init == FALSE /\\ x = 0'))['verdict'] == 'inconclusive_or_tool_error'
control = dataclasses.replace(base, purpose='unsafe', kind='invariant_violation', expected_name='Safe', controls_property='Safe')
assert run('invariant', 'invariant', control)['verdict'] == 'match'
assert run('wrong-property', 'invariant', dataclasses.replace(control, expected_name='TypeOK'))['verdict'] == 'unexpected_semantic_result'
temporal = dataclasses.replace(control, kind='temporal_violation', expected_name='EventuallyOne', properties=('EventuallyOne',))
assert run('temporal', 'none', temporal, 'PROPERTY EventuallyOne\n')['verdict'] == 'match'
assert run('parser', 'none', base, text=module.replace('Init ==', 'Init == ('))['verdict'] == 'inconclusive_or_tool_error'
assert run('deadlock', 'deadlock', control)['verdict'] == 'inconclusive_or_tool_error'
assert run('timeout', 'none', dataclasses.replace(base, timeout_seconds=0.000001))['reason'] == 'timeout'
constants, invariants, properties, specification = check.parse_cfg('SPECIFICATION Spec\nCONSTANTS N = 3 Flag = TRUE S = {a, b} Fault = "none"\nINVARIANTS TypeOK Safe\nPROPERTY Live\n')
assert constants['N'] == 3 and constants['Flag'] is True and len(constants['S'][1]) == 2
assert invariants == ('TypeOK', 'Safe') and properties == ('Live',) and specification == 'Spec'
for bad in ('CHECK_DEADLOCK FALSE', 'CONSTRAINT Foo', 'CONSTANT N <- Foo'):
    try: check.parse_cfg('SPECIFICATION Spec\n' + bad)
    except check.InputError: pass
    else: raise AssertionError(bad)
completed = 'Finished computing initial states: 1 distinct state generated.\nFinished in 00s\nModel checking completed. No error has been found.\n100 states generated, 90 distinct states found, 10 states left on queue.\n'
assert check.classify(base, 0, completed)[0] == check.Verdict.ERROR
assert check.classify(base, 0, completed.replace('10 states left', '0 states left'))[0] == check.Verdict.MATCH
inventory = json.loads((check.ROOT / 'references' / 'requirements.json').read_text())['families']['undo']
cases = check.load_family('undo', check.ROOT / 'models' / 'undo')
check.validate_coverage(cases, inventory, 'undo', 'qualification')
for field, value in (('invariants', ('TypeOK',)), ('properties', ()), ('specification', 'WeakerSpec'), ('constants', (('MaxSlots', 1),))):
    changed = [dataclasses.replace(c, **{field: value}) if c.id == 'larger' else c for c in cases]
    try: check.validate_coverage(changed, inventory, 'undo', 'qualification')
    except check.InputError: pass
    else: raise AssertionError('accepted changed safe profile: ' + field)
try: check.validate_coverage([c for c in cases if c.id != 'larger'], inventory, 'undo', 'qualification')
except check.InputError: pass
else: raise AssertionError('accepted missing qualification')
print('All self-tests passed.')
