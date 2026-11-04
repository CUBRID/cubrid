#!/usr/bin/env python3
import argparse
from dataclasses import dataclass, asdict
from enum import Enum
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
FAMILIES = ('lifetime', 'history', 'undo', 'boundary')
JAR_HASH = '936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88'
NAME = r'[A-Za-z][A-Za-z0-9_]*'

class InputError(Exception):
    pass

class Verdict(Enum):
    MATCH = 'match'
    MISMATCH = 'unexpected_semantic_result'
    ERROR = 'inconclusive_or_tool_error'

@dataclass(frozen=True)
class Case:
    family: str
    model: str
    id: str
    config: str
    matrix: tuple
    purpose: str
    kind: str
    expected_name: str | None
    bounds: tuple
    invariants: tuple
    properties: tuple
    controls_property: str | None
    scenario: str
    timeout_seconds: int
    constants: tuple
    specification: str = 'Spec'


def require(condition, message):
    if not condition:
        raise InputError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def string_list(value, label, names=False):
    require(isinstance(value, list) and all(isinstance(v, str) and v and (not names or re.fullmatch(NAME, v)) for v in value), f'invalid {label}')
    require(len(value) == len(set(value)), f'duplicate {label}')
    return tuple(value)


def parse_cfg(source):
    source = re.sub(r'\\\*[^\n]*', '', source)
    tokens = re.findall(r'"[^"\n]*"|[A-Za-z][A-Za-z0-9_]*|-?\d+|[{}=,]|\S', source)
    sections = {'SPECIFICATION', 'CONSTANT', 'CONSTANTS', 'INVARIANT', 'INVARIANTS', 'PROPERTY', 'PROPERTIES'}
    forbidden = {'CONSTRAINT', 'CONSTRAINTS', 'ACTION_CONSTRAINT', 'ACTION_CONSTRAINTS', 'VIEW', 'SYMMETRY', 'CHECK_DEADLOCK'}
    const, inv, prop, spec = {}, [], [], None
    i = 0
    def ident(token):
        return bool(re.fullmatch(NAME, token))
    while i < len(tokens):
        section = tokens[i]; i += 1
        require(section in sections and section not in forbidden, f'unsupported config token {section}')
        if section == 'SPECIFICATION':
            require(spec is None and i < len(tokens) and ident(tokens[i]) and tokens[i] not in sections | forbidden, 'invalid SPECIFICATION')
            spec = tokens[i]; i += 1
        elif section in ('CONSTANT', 'CONSTANTS'):
            while i < len(tokens) and tokens[i] not in sections | forbidden:
                require(i + 2 < len(tokens) and ident(tokens[i]) and tokens[i+1] == '=', 'unsupported constant assignment')
                key = tokens[i]; i += 2
                require(key not in const, f'duplicate constant {key}')
                value = tokens[i]; i += 1
                if value == '{':
                    values = []
                    while i < len(tokens) and tokens[i] != '}':
                        v = tokens[i]; i += 1
                        require(ident(v) or re.fullmatch(r'-?\d+', v) or re.fullmatch(r'"[^"\n]*"', v), 'unsupported set element')
                        values.append(v)
                        if i < len(tokens) and tokens[i] != '}':
                            require(tokens[i] == ',', 'invalid set'); i += 1
                    require(i < len(tokens) and tokens[i] == '}' and len(set(values)) == len(values), 'invalid set'); i += 1
                    value = ('set', tuple(values))
                elif re.fullmatch(r'-?\d+', value): value = int(value)
                elif value in ('TRUE', 'FALSE'): value = value == 'TRUE'
                elif re.fullmatch(r'"[^"\n]*"', value): value = value[1:-1]
                else: raise InputError(f'unsupported constant value {value}')
                const[key] = value
        else:
            target = inv if section.startswith('INVARIANT') else prop
            start = i
            while i < len(tokens) and tokens[i] not in sections | forbidden:
                require(ident(tokens[i]), f'invalid property {tokens[i]}'); target.append(tokens[i]); i += 1
            require(i > start, f'empty {section}')
    require(spec is not None and len(inv) == len(set(inv)) and len(prop) == len(set(prop)), 'missing specification or duplicate checks')
    return const, tuple(inv), tuple(prop), spec


def load_family(family, directory):
    metadata = json.loads((directory / 'cases.json').read_text())
    require(set(metadata) == {'schema_version', 'model', 'assumptions', 'cases'} and type(metadata['schema_version']) is int and metadata['schema_version'] == 1, f'{family}: invalid metadata schema')
    model = metadata['model']; require(isinstance(model, str) and re.fullmatch(NAME, model), 'invalid model name')
    string_list(metadata['assumptions'], 'assumptions')
    require((directory / (model + '.tla')).is_file(), f'missing module {model}')
    require(isinstance(metadata['cases'], list) and metadata['cases'], 'missing cases')
    cases = []
    keys = {'id','config','matrix','purpose','expect','bounds','invariants','properties','controls_property','scenario','timeout_seconds'}
    for data in metadata['cases']:
        require(isinstance(data, dict) and set(data) == keys, 'invalid case keys')
        cid, config = data['id'], data['config']
        require(isinstance(cid, str) and re.fullmatch(NAME, cid), 'invalid case id')
        require(isinstance(config, str) and re.fullmatch(NAME + r'\.cfg', config), 'invalid config path')
        path = directory / config
        require(path.is_file() and not path.is_symlink(), f'missing or unsafe config {config}')
        matrix = string_list(data['matrix'], 'matrix'); require(matrix in (('quick','qualification'), ('qualification',)), 'invalid matrix')
        purpose = data['purpose']; require(purpose in ('safe','unsafe','witness'), 'invalid purpose')
        ex = data['expect']; require(isinstance(ex, dict) and ex.get('kind') in ('complete','invariant_violation','temporal_violation'), 'invalid expectation')
        kind = ex['kind']; require(set(ex) == ({'kind'} if kind == 'complete' else {'kind','name'}), 'invalid expectation keys')
        expected = ex.get('name'); require(expected is None or isinstance(expected, str) and re.fullmatch(NAME, expected), 'invalid expected name')
        require((purpose == 'safe') == (kind == 'complete'), 'purpose and expectation mismatch')
        control = data['controls_property']; require(control == expected if purpose == 'unsafe' else control is None, 'invalid control')
        inv = string_list(data['invariants'], 'invariants', True); prop = string_list(data['properties'], 'properties', True)
        const, actual_inv, actual_prop, specification = parse_cfg(path.read_text())
        require(inv == actual_inv and prop == actual_prop, f'{cid}: metadata checks differ from cfg')
        if kind == 'invariant_violation': require(expected in inv, f'{cid}: expected invariant missing')
        if kind == 'temporal_violation': require(prop == (expected,), f'{cid}: temporal control needs exactly one property')
        bounds = data['bounds']; require(isinstance(bounds, dict) and 'Fault' not in bounds, 'invalid bounds')
        require(set(bounds) == {k for k, v in const.items() if k != 'Fault' and (type(v) in (int, bool) or isinstance(v, tuple))}, f'{cid}: bounds must list every numeric, boolean and set constant')
        for k, v in bounds.items():
            require(isinstance(k, str) and k in const and (type(v) in (int, bool)), f'{cid}: invalid bound {k}')
            actual = const[k]; actual = len(actual[1]) if isinstance(actual, tuple) else actual
            require(type(v) is type(actual) and v == actual, f'{cid}: bound {k} differs from cfg')
        require(isinstance(data['scenario'], str) and data['scenario'], 'invalid scenario')
        timeout = data['timeout_seconds']; require(type(timeout) is int and 0 < timeout <= 86400, 'invalid timeout')
        cases.append(Case(family, model, cid, config, matrix, purpose, kind, expected, tuple(bounds.items()), inv, prop, control, data['scenario'], timeout, tuple(const.items()), specification))
    require(len({c.id for c in cases}) == len(cases), f'{family}: duplicate case ids')
    return cases


def validate_coverage(cases, inventory, family, matrix='quick'):
    require(isinstance(inventory, dict) and set(inventory) <= {'scenarios','properties','witnesses','safe_cases'} and {'scenarios','properties','safe_cases'} <= set(inventory), f'{family}: invalid inventory')
    scenarios = string_list(inventory['scenarios'], 'required scenarios')
    properties = string_list(inventory['properties'], 'required properties', True)
    witnesses = string_list(inventory.get('witnesses', []), 'required witnesses')
    require(set(scenarios) <= {c.scenario for c in cases}, f'{family}: missing required scenarios {set(scenarios) - {c.scenario for c in cases}}')
    require(set(witnesses) <= {c.scenario for c in cases if c.purpose == 'witness'}, f'{family}: missing required witnesses')
    safe = [c for c in cases if c.purpose == 'safe']
    require(safe, f'{family}: no safe case')
    profiles = inventory['safe_cases']
    require(isinstance(profiles, dict) and profiles, f'{family}: missing frozen safe profiles')
    selected = {c.id: c for c in cases}
    for cid, profile in profiles.items():
        require(isinstance(profile, dict) and set(profile) == {'matrix','constants','specification','invariants','properties'}, f'{family}: invalid safe profile {cid}')
        matrices = string_list(profile['matrix'], 'frozen profile matrix')
        require(matrices in (('quick','qualification'), ('qualification',)), 'invalid frozen matrix')
        if matrix not in matrices:
            continue
        require(cid in selected and selected[cid].purpose == 'safe', f'{family}: missing required safe case {cid}')
        case = selected[cid]
        require(case.matrix == matrices, f'{family}/{cid}: matrix differs from frozen profile')
        require(case.specification == profile['specification'], f'{family}/{cid}: specification differs from frozen profile')
        require(case.invariants == string_list(profile['invariants'], 'frozen invariants', True), f'{family}/{cid}: invariants differ from frozen profile')
        require(case.properties == string_list(profile['properties'], 'frozen properties', True), f'{family}/{cid}: temporal properties differ from frozen profile')
        require(json.dumps(dict(case.constants), sort_keys=True) == json.dumps(profile['constants'], sort_keys=True),
                f'{family}/{cid}: constants differ from frozen profile')
    checks = {p for c in safe for p in c.invariants + c.properties if p != 'TypeOK'}
    require(set(properties) <= checks, f'{family}: missing required properties {set(properties) - checks}')
    controls = {(c.kind, c.controls_property) for c in cases if c.purpose == 'unsafe'}
    for c in safe:
        for p in c.invariants:
            if p != 'TypeOK': require(('invariant_violation', p) in controls, f'{family}: missing unsafe control for {p}')
        for p in c.properties: require(('temporal_violation', p) in controls, f'{family}: missing temporal control for {p}')


def state_counts(output):
    matches = re.findall(r'([\d,]+) states generated, ([\d,]+) distinct states found, ([\d,]+) states left on queue', output)
    return [int(n.replace(',', '')) for n in matches[-1]] if matches else None


def classify(case, code, output, timed_out=False):
    if timed_out: return Verdict.ERROR, 'timeout'
    if re.search(r'Error: Deadlock reached|Error: Parsing|Parse Error|Semantic errors|Error: TLC', output, re.I):
        return Verdict.ERROR, 'deadlock or TLC error'
    errors = re.findall(r'^Error: .*$', output, re.MULTILINE)
    allowed = (r'Error: Invariant \w+ is violated\.', r'Error: Temporal properties were violated\.', r'Error: The behavior up to this point is:', r'Error: The following behavior constitutes a counter-example:')
    if any(not any(re.fullmatch(pattern, line) for pattern in allowed) for line in errors):
        return Verdict.ERROR, 'additional TLC error'
    trace = bool(re.search(r'State 1:', output))
    inv = re.search(r'Error: Invariant (\w+) is violated\.', output)
    temporal = 'Error: Temporal properties were violated.' in output
    if inv or temporal:
        if (inv and code != 12) or (temporal and code != 13): return Verdict.ERROR, 'unexpected TLC exit code'
        if not trace: return Verdict.ERROR, 'violation without trace'
        if 'Finished in ' not in output: return Verdict.ERROR, 'violation without finished checker'
        matches = (case.kind == 'invariant_violation' and inv and inv.group(1) == case.expected_name) or (case.kind == 'temporal_violation' and temporal and not inv)
        return (Verdict.MATCH, 'expected ' + case.purpose) if matches else (Verdict.MISMATCH, 'unexpected violation')
    counts = state_counts(output)
    nonempty = re.search(r'Finished computing initial states: [1-9][\d,]* distinct states? generated', output)
    if code == 0 and not errors and 'Model checking completed. No error has been found.' in output and nonempty and counts and counts[0] > 0 and counts[1] > 0 and counts[2] == 0 and 'Finished in ' in output:
        return (Verdict.MATCH, 'complete') if case.kind == 'complete' else (Verdict.MISMATCH, 'expected violation did not occur')
    return Verdict.ERROR, 'incomplete search or unrecognized TLC failure'


def run_case(case, directory, jar, java):
    log = directory / 'tlc.log'
    java_tmp = directory / 'java-tmp'
    java_tmp.mkdir()
    command = [java, '-Djava.io.tmpdir=' + str(java_tmp), '-cp', str(jar), 'tlc2.TLC', '-workers', '1', '-fp', '0', '-config', case.config, case.model + '.tla']
    try:
        proc = subprocess.Popen(command, cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, start_new_session=True)
        try: output, _ = proc.communicate(timeout=case.timeout_seconds); timed_out = False
        except subprocess.TimeoutExpired:
            import signal
            os.killpg(proc.pid, signal.SIGKILL); output, _ = proc.communicate(); timed_out = True
        code = proc.returncode
    except OSError as e:
        output = str(e); code = None; timed_out = False
    log.write_text(output)
    verdict, reason = classify(case, code, output, timed_out)
    return {'case': asdict(case), 'command': command, 'exit_code': code, 'verdict': verdict.value, 'reason': reason, 'state_counts': state_counts(output), 'log': str(log)}


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--matrix', choices=('quick','qualification'), default='quick'); parser.add_argument('--model', choices=FAMILIES)
    args = parser.parse_args()
    out = Path(tempfile.mkdtemp(prefix='sidecar-tlc-')); print(f'Output: {out}', flush=True)
    report = {'matrix': args.matrix, 'scope': args.model or 'all', 'cases': [], 'inputs': {}, 'error': None, 'python_version': sys.version}
    status = 2
    try:
        frozen_runner = out / 'check.py'
        frozen_runner.write_bytes(Path(__file__).read_bytes())
        report['runner_sha256'] = digest(frozen_runner)
        jar = Path(os.environ.get('TLA2TOOLS_JAR', str(Path.home()/'.cache/tlaplus/tla2tools-1.7.4.jar'))).expanduser().resolve()
        require(jar.is_file() and digest(jar) == JAR_HASH, 'TLC jar missing or SHA-256 mismatch')
        frozen_jar = out / 'tla2tools-1.7.4.jar'; shutil.copyfile(jar, frozen_jar)
        require(digest(frozen_jar) == JAR_HASH, 'copied TLC jar SHA-256 mismatch'); jar = frozen_jar
        java = shutil.which(os.environ.get('JAVA', 'java')); require(java is not None, 'Java executable not found')
        inventory_path = ROOT / 'references' / 'requirements.json'
        require(inventory_path.is_file(), 'missing required references/requirements.json')
        inventory_bytes = inventory_path.read_bytes(); inventory = json.loads(inventory_bytes)
        require(set(inventory) == {'schema_version','families'} and type(inventory['schema_version']) is int and inventory['schema_version'] == 1 and isinstance(inventory['families'], dict), 'invalid requirements inventory')
        families = (args.model,) if args.model else FAMILIES
        require(set(inventory['families']) == set(FAMILIES), 'inventory must name all families')
        (out / 'requirements.json').write_bytes(inventory_bytes); report['inputs']['requirements.json'] = digest(out / 'requirements.json'); report['jar_sha256'] = digest(jar); report['java_sha256'] = digest(Path(java).resolve())
        all_cases = []
        for family in families:
            source = ROOT / 'models' / family; frozen = out / 'inputs' / family; frozen.mkdir(parents=True)
            require(source.is_dir(), f'missing family {family}')
            for path in source.iterdir():
                if path.suffix in ('.tla', '.cfg') or path.name == 'cases.json':
                    require(path.is_file() and not path.is_symlink(), f'unsafe input {path}')
                    shutil.copyfile(path, frozen / path.name); report['inputs'][f'{family}/{path.name}'] = digest(frozen / path.name)
            selected = [c for c in load_family(family, frozen) if args.matrix in c.matrix]
            validate_coverage(selected, inventory['families'][family], family, args.matrix); all_cases.extend(selected)
        if 'boundary' in families:
            dependency = out / 'inputs' / 'history' / 'SidecarHistory.tla'
            if not dependency.exists():
                source = ROOT / 'models' / 'history' / dependency.name
                require(source.is_file() and not source.is_symlink(), 'missing boundary history predicate dependency')
                dependency.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, dependency)
            report['inputs']['history/' + dependency.name] = digest(dependency)
        for case in all_cases:
            case_dir = out / 'runs' / case.family / case.id; shutil.copytree(out / 'inputs' / case.family, case_dir)
            if case.family == 'boundary':
                shutil.copyfile(out / 'inputs' / 'history' / 'SidecarHistory.tla', case_dir / 'SidecarHistory.tla')
            print(f'{case.family}/{case.id} ...', flush=True)
            result = run_case(case, case_dir, jar, java); report['cases'].append(result)
            print(f"  {result['verdict']}: {result['reason']}", flush=True)
        verdicts = {r['verdict'] for r in report['cases']}
        status = 2 if Verdict.ERROR.value in verdicts else 1 if Verdict.MISMATCH.value in verdicts else 0
    except (InputError, OSError, ValueError, KeyError, TypeError) as e:
        report['error'] = str(e); print(f'Input/tool error: {e}', flush=True)
    report['exit_code'] = status
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Report: {out / "report.json"}; exit {status}', flush=True)
    return status

if __name__ == '__main__':
    raise SystemExit(main())
