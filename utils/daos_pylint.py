#!/usr/bin/env python3
"""Wrapper script for calling pylint"""

from pylint.lint import Run
from pylint.reporters.collecting_reporter import CollectingReporter
from pylint.lint import pylinter
from collections import Counter
import subprocess
import sys

# Test against python 3.6 as this is what CentOS 7.9 is using.
MIN_PY_VER = '3.6'

# atom.io will call
# /Library/Developer/CommandLineTools/Library/Frameworks/Python3.framework/Versions/3.8/Resources/Python.app/Contents/MacOS/Python /Users/ampittma/coral/daos/venv/bin/pylint --msg-template='{line},{column},{category},{msg_id}:{msg_id} {symbol} {msg}' --reports=n --output-format=text --rcfile=/Users/ampittma/coral/daos/pylintrc /Users/ampittma/coral/daos/utils/node_local_test.py

def parse_file(target_file):
    """Main program"""

    rep = CollectingReporter()

    if isinstance(target_file, list):
        target = list(target_file)
        target.extend(['--jobs', '100'])
    else:
        target = [target_file]

    target.extend(['--py-version', MIN_PY_VER])
    target.extend(['--persistent', 'n'])

    results = Run(target, reporter=rep, do_exit=False)

    types = Counter()
    symbols = Counter()

    for msg in results.linter.reporter.messages:
        vals = {}

        # Spelling mistake, do not complain about message tags.
        if msg.msg_id in ('C0401', 'C0402'):
            if ":avocado:" in msg.msg:
                continue

        vals['path'] = msg.path
        vals['line'] = msg.line
        vals['column'] = msg.column
        vals['message-id'] = msg.msg_id
        vals['message'] = msg.msg
        vals['symbol'] = msg.symbol

        print('AMP:{path}:{line}:{column}: {message-id}: {message} ({symbol})'.format(**vals))
        types[msg.category] += 1
        symbols[msg.symbol] += 1

    if not types:
        return
    for (mtype, count) in types.most_common():
        print(f'{mtype}:{count}')

    for (mtype, count) in symbols.most_common():
        print(f'{mtype}:{count}')

def run_git_files():
    """Run pylint on contents of 'git ls-files'"""

    ret = subprocess.run(['git', 'ls-files'], check=True, capture_output=True)
    stdout = ret.stdout.decode('utf-8')
    py_files = []
    for file in stdout.splitlines():
        if not file.endswith('.py'):
            continue
        py_files.append(file)
    parse_file(py_files)

def run_input_file():
    """Run from a input file"""

#    py_files = []
    with open('utils/to-check') as fd:
        for file in fd.readlines():
            file = file.strip()
            if not file.endswith('.py'):
                continue
            if file.startswith('src/control/vendor/'):
                continue
#            print(file)
            parse_file(file)
            continue
#            py_files.append(file)
#            if len(py_files) == 10:
#                parse_file(py_files)
#                py_files = []
#    parse_file(py_files)

def main():
    """Main program"""

    pylinter.MANAGER.clear_cache()

    if len(sys.argv) == 2:
        if sys.argv[1] == 'git':
            run_git_files()
            return
        if sys.argv[1] == 'from-file':
            run_input_file()
            return
        parse_file(sys.argv[1])
    else:
        parse_file('utils/node_local_test.py')
        parse_file('ci/gha_helper.py')
        parse_file('SConstruct')


if __name__ == "__main__":
    main()
