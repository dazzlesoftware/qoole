# Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
"""Audit first-party C++ policy without inspecting vendored/generated source."""
from pathlib import Path
import argparse
import re

SOURCE = Path(__file__).resolve().parents[1] / 'Source'
TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\b[A-Za-z_]\w*\b', re.S)

def inspect():
    findings = []
    for group in ('Runtime', 'Editor', 'Programs'):
        for path in sorted((SOURCE / group).rglob('*')):
            if path.suffix not in ('.h', '.cpp'):
                continue
            text = path.read_text(encoding='utf-8', errors='surrogateescape')
            for token in TOKEN.finditer(text):
                value = token.group()
                if value == 'throw' and group != 'Programs' and ('Scripting' in path.parts or 'ScriptEditorPlugin' in path.parts):
                    line = text.count('\n', 0, token.start()) + 1
                    findings.append(f'{path.relative_to(SOURCE)}:{line}: scripting validation must return errors; preserve foreign exception barriers')
                if value == 'auto' or re.fullmatch(r'(?:UE_\w*|UE|Unreal\w+)', value):
                    line = text.count('\n', 0, token.start()) + 1
                    findings.append(f'{path.relative_to(SOURCE)}:{line}: forbidden token {value}')
            if group != 'Programs':
                if 'Tests' in path.parts or re.search(r'^\s*#\s*include\s*[<"](?:doctest|TestHelpers)', text, re.M):
                    findings.append(f'{path.relative_to(SOURCE)}: tests must belong to Programs')
    return findings

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, help='Write findings without truncation')
    args = parser.parse_args()
    findings = inspect()
    if args.report:
        args.report.write_text('\n'.join(findings) + ('\n' if findings else ''), encoding='utf-8')
    print(f'Coding audit: {len(findings)} policy findings')
    for finding in findings[:20]:
        print(finding)
    return bool(findings)

if __name__ == '__main__':
    raise SystemExit(main())
