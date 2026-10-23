# Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
"""Gather explicit first-party display messages into the source-language catalog."""
from pathlib import Path
import ast
import json
import re

ROOT = Path(__file__).resolve().parents[1]
STRING = r'"(?:[^"\\]|\\.)*"(?:\s*"(?:[^"\\]|\\.)*")*'
PATTERNS = [
    re.compile(r'GENESIS_TEXT\(\s*(' + STRING + ')'),
    re.compile(r'FTextLocalization::Format\(\s*(' + STRING + ')'),
    re.compile(r'GENESIS_LOG\(\s*\w+\s*,\s*\w+\s*,\s*(' + STRING + ')'),
    # Static labels and tooltips passed through shared display helpers.
    re.compile(r'\b(?:Add|AddCommand|AddSubMenu)\(\s*' + STRING + r'\s*,\s*(' + STRING + r')\s*,\s*(' + STRING + ')'),
    re.compile(r'\b(?:SpinRow(?:<[^>]+>)?|PointSpin|SplineSpin|SplineCheck|Number|Check)\(\s*(' + STRING + r')\s*,\s*(' + STRING + ')'),
    re.compile(r'\bPanelHeader\(\s*(' + STRING + ')'),
    re.compile(r'\.Show\(\s*EMessageType::\w+\s*,\s*EMessageButtons::\w+\s*,\s*(' + STRING + r')(?:\s*,\s*(' + STRING + '))?'),

]

def gather():
    messages = set()
    for group in ('Runtime', 'Editor', 'Programs/GenesisEditor', 'Programs/GenesisGridServer'):
        for path in (ROOT / 'Source' / group).rglob('*'):
            if path.suffix not in ('.h', '.cpp'):
                continue
            text = path.read_text(encoding='utf-8', errors='surrogateescape')
            for pattern in PATTERNS:
                for match in pattern.finditer(text):
                    for source in match.groups():
                        if not source:
                            continue
                        parts = re.findall(r'"(?:[^"\\]|\\.)*"', source)
                        message = ''.join(ast.literal_eval(part) for part in parts)
                        if message:
                            messages.add(message)
    return {message: message for message in sorted(messages)}

if __name__ == '__main__':
    output = ROOT / 'Content' / 'Localization' / 'en.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    catalog = gather()
    output.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'Gathered {len(catalog)} source messages: {output}')
