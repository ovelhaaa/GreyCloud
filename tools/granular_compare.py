"""Reproduce the granular before/after test, WAVs, metrics and callback benchmark."""
import argparse
from pathlib import Path
import os
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', default='38085c994e87a09b9e4f30fb87f486e8a65c2aaf')
parser.add_argument('--out', type=Path, default=Path('build/granular-comparison'))
parser.add_argument('--compiler', default='g++')
parser.add_argument('--profile', default='DESKTOP_STUDIO',
                    choices=['DESKTOP_STUDIO', 'H5_LOW_CPU', 'H5_BALANCED', 'H7_HIGH_QUALITY'])
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
out = args.out.resolve()
flags = ['-std=c++17', '-O3', '-DNDEBUG', f'-DCLOUD_GREY_PROFILE_{args.profile}=1']
ext = '.exe' if os.name == 'nt' else ''
for version in ['before', 'after']:
    directory = out / version
    directory.mkdir(parents=True, exist_ok=True)
    for file in ['cloud_grey_verb.cpp', 'cloud_grey_verb.hpp', 'dsp_utils.hpp']:
        data = (subprocess.check_output(['git', 'show', f'{args.baseline}:src/dsp/{file}'], cwd=root)
                if version == 'before' else (root / 'src/dsp' / file).read_bytes())
        (directory / file).write_bytes(data)
    # Co-locate the harness with the matching header: do not mix class layouts
    # between old/new translation units by relying on -I include precedence.
    for test in ['continuity_test', 'bench']:
        file = f'cloud_grey_verb_granular_{test}.cpp'
        (directory / file).write_bytes((root / 'src/dsp' / file).read_bytes())
        binary = directory / (test + ext)
        extra = ['-DCGV_CONTINUITY_BASELINE=1'] if version == 'before' and test == 'continuity_test' else []
        subprocess.run([args.compiler, *flags, *extra, str(directory / 'cloud_grey_verb.cpp'),
                        str(directory / file), '-o', str(binary)], check=True)
        cmd = [str(binary)]
        if test == 'continuity_test':
            cmd.append(str(directory / version))
        with (directory / f'{test}.log').open('w', encoding='utf-8') as log:
            result = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT)
        print(f'{version} {test}: exit={result.returncode}, log={directory / (test + ".log")}')
        if test == 'continuity_test' and version == 'before':
            if result.returncode != 1:
                raise RuntimeError('Baseline must fail the regression checks (exit 1)')
        elif result.returncode:
            raise RuntimeError(f'{version} {test} failed; see its log')
