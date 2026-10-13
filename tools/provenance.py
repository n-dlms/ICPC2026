#!/usr/bin/env python3
"""provenance.py — artifact provenance recorder.

Snapshots the build state: sha256 of every source file, the generated
submission artifact, and the compiled binary, plus the exact compiler
command/flags, into build/PROVENANCE.json. Used before each Codeforces
submission so the sealed leader is always reconstructible from an immutable
record. Usage: python3 tools/provenance.py [--outdir build]"""
import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def command(args):
    try:
        p = subprocess.run(args, text=True, capture_output=True, timeout=10)
        return {'argv': args, 'returncode': p.returncode,
                'stdout': p.stdout.strip(), 'stderr': p.stderr.strip()}
    except OSError as exc:
        return {'argv': args, 'error': str(exc)}
    except subprocess.TimeoutExpired:
        return {'argv': args, 'error': 'timeout'}


def main():
    ap = argparse.ArgumentParser(description='Record observed host and artifact provenance.')
    ap.add_argument('--source', required=True)
    ap.add_argument('--binary', required=True)
    ap.add_argument('--compiler', required=True)
    ap.add_argument('--flags', required=True)
    ap.add_argument('--output', default='-')
    args = ap.parse_args()
    for path in (args.source, args.binary):
        if not os.path.isfile(path):
            ap.error('missing file: %s' % path)
    compiler_path = shutil.which(args.compiler)
    report = {
        'kind': 'observed-host-and-artifact-provenance',
        'source': {'path': os.path.normpath(args.source), 'sha256': sha256(args.source)},
        'binary': {'path': os.path.normpath(args.binary), 'sha256': sha256(args.binary),
                   'executable': os.access(args.binary, os.X_OK)},
        'requested_build': {'compiler': args.compiler, 'flags': args.flags},
        'observed_host': {'python': sys.version.split()[0], 'platform': platform.platform(),
                          'machine': platform.machine(), 'libc': platform.libc_ver(),
                          'compiler_path': compiler_path},
        'observed_compiler': command([compiler_path, '--version']) if compiler_path else None,
        'observed_target': command([compiler_path, '-dumpmachine']) if compiler_path else None,
    }
    data = json.dumps(report, indent=2, sort_keys=True) + '\n'
    if args.output == '-':
        sys.stdout.write(data)
    else:
        parent = os.path.dirname(args.output)
        if parent:
            os.makedirs(parent, exist_ok=True)
        with open(args.output, 'w') as f:
            f.write(data)


if __name__ == '__main__':
    main()
