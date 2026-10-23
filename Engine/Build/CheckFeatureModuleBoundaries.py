# Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
"""Check feature module ownership and build dependency boundaries (stdlib only)."""
from pathlib import Path
import xml.etree.ElementTree as ET

source = Path(__file__).resolve().parents[1] / 'Source'
ns = {'m': 'http://schemas.microsoft.com/developer/msbuild/2003'}
projects = {p.stem: p for p in source.rglob('*.vcxproj')}
features = ('Landscape', 'Foliage', 'LandscapeEditorUtilities', 'LandscapeEditor', 'FoliageEditor')
graph = {}
errors = []
for name, project in projects.items():
    tree = ET.parse(project)
    refs = tree.findall('.//m:ProjectReference', ns)
    graph[name] = [Path(r.attrib['Include'].replace('\\', '/')).stem for r in refs]
    if project.is_relative_to(source / 'Runtime'):
        for dep in graph[name]:
            if dep in projects and projects[dep].is_relative_to(source / 'Editor'):
                errors.append(f'{name} imports editor module {dep}')
    if name in features:
        for tag in ('ClCompile', 'ClInclude'):
            for item in tree.findall('.//m:' + tag, ns):
                if 'Include' not in item.attrib:
                    continue
                owned = (project.parent / item.attrib['Include'].replace('\\', '/')).resolve()
                if not owned.is_relative_to(project.parent.resolve()) or not owned.is_file():
                    errors.append(f'{name}: invalid source ownership {owned}')
        for file in project.parent.rglob('*'):
            if file.suffix in ('.h', '.cpp'):
                code = file.read_text()
                if 'MapEditorPrivate.h' in code or 'MapEditorShell' in code or 'GenesisEditor/' in code:
                    errors.append(f'{name}: imports host/private implementation in {file}')
for name in ('Landscape', 'Foliage'):
    if set(graph[name]) - {'Core', 'Geometry'}:
        errors.append(f'{name}: runtime data depends on higher-level modules')
for name, forbidden in {'MapEditor': {'LandscapeEditor', 'FoliageEditor'},
                        'LandscapeEditor': {'FoliageEditor'},
                        'FoliageEditor': {'LandscapeEditor'},
                        'LandscapeEditorUtilities': {'MapEditor', 'MapCore', 'LandscapeEditor', 'FoliageEditor'}}.items():
    if set(graph[name]) & forbidden:
        errors.append(f'{name}: reversed feature dependency')
active, done = set(), set()
def visit(name):
    if name in active:
        errors.append(f'dependency cycle through {name}')
        return
    if name in done:
        return
    active.add(name)
    for dep in graph.get(name, []):
        visit(dep)
    active.remove(name)
    done.add(name)
for name in graph:
    visit(name)
if errors:
    raise SystemExit('\n'.join(errors))
print(f'Feature boundaries passed: {len(features)} feature modules; {len(graph)} project graphs checked.')
