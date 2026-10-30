"""Build a self-contained Windows folder from release PE imports.

Only the executable, transitive DLL imports, selected Qt plug-ins, examples and
dependency notices are copied. Development tools and the conda environment are
never included. Run with the Python interpreter from the build environment.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import shutil
import struct
import os


def imports(path: pathlib.Path) -> list[str]:
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError(f'Not a PE image: {path}')
    sections, optional_size = struct.unpack_from('<H12xH', data, pe + 6)
    optional = pe + 24
    magic = struct.unpack_from('<H', data, optional)[0]
    if magic != 0x20b:
        raise ValueError(f'Expected 64-bit PE image: {path}')
    dirs = optional + 112
    section_table = optional + optional_size
    ranges = []
    for i in range(sections):
        virtual_size, virtual_addr, raw_size, raw = struct.unpack_from('<IIII', data, section_table + i * 40 + 8)
        ranges.append((virtual_addr, virtual_addr + max(virtual_size, raw_size), raw))

    def offset(rva: int) -> int:
        for a, b, raw in ranges:
            if a <= rva < b:
                return raw + rva - a
        raise ValueError(f'Unmapped PE address {rva:x} in {path}')

    def text_at(rva: int) -> str:
        at = offset(rva)
        return data[at:data.index(b'\0', at)].decode('ascii')

    result = []
    address, _ = struct.unpack_from('<II', data, dirs + 8)
    if address:
        at = offset(address)
        while any(data[at:at + 20]):
            result.append(text_at(struct.unpack_from('<I', data, at + 12)[0]))
            at += 20
    # Delay-loaded imports also need to be present at runtime.
    address, _ = struct.unpack_from('<II', data, dirs + 13 * 8)
    if address:
        at = offset(address)
        while any(data[at:at + 32]):
            flags, name = struct.unpack_from('<II', data, at)
            if not flags & 1:
                raise ValueError(f'Unsupported VA delay import in {path}')
            result.append(text_at(name))
            at += 32
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--viewer', required=True, type=pathlib.Path)
    parser.add_argument('--environment', required=True, type=pathlib.Path)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    parser.add_argument('--repository', required=True, type=pathlib.Path)
    parser.add_argument('--example-project', type=pathlib.Path)
    parser.add_argument('--example-terrain', type=pathlib.Path)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if any(out.iterdir()):
        raise ValueError(f'Use a new empty staging directory: {out}')
    env = args.environment.resolve()
    lib = env / 'Library' / 'bin'
    dlls = {p.name.lower(): p for p in lib.glob('*.dll')}
    system = pathlib.Path(os.environ['SystemRoot']) / 'System32'
    plugin_root = env / 'Library' / 'lib' / 'qt6' / 'plugins'
    sources = {}

    def copy(source: pathlib.Path, relative: str) -> None:
        destination = out / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        sources[relative] = source

    copy(args.viewer.resolve(), 'RouteTec.exe')
    plugins = ['platforms/qwindows.dll', 'styles/qmodernwindowsstyle.dll',
               'imageformats/qjpeg.dll', 'imageformats/qico.dll']
    for plugin in plugins:
        source = plugin_root / plugin
        if source.exists():
            copy(source, plugin)
        elif plugin.startswith('platforms/'):
            raise FileNotFoundError(source)
    queue = list(sources)
    done = set()
    windows_imports = set()
    while queue:
        name = queue.pop()
        if name.lower() in done:
            continue
        done.add(name.lower())
        for dep in imports(out / name):
            key = dep.lower()
            if key in dlls:
                if key not in {s.lower() for s in sources}:
                    copy(dlls[key], dlls[key].name)
                    queue.append(dlls[key].name)
            elif key.startswith(('api-ms-', 'ext-ms-')) or (system / dep).exists():
                windows_imports.add(dep)
            else:
                raise FileNotFoundError(f'Unresolved import {dep} from {name}')
    # Use application-relative Qt plug-ins, independent of installed Qt/conda.
    (out / 'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=.\nTranslations=translations\n', encoding='utf-8')
    translation = env / 'Library' / 'translations' / 'qt6' / 'qtbase_es.qm'
    if translation.exists():
        copy(translation, 'translations/qtbase_es.qm')
    for name in ['optimization-flat.obj', 'proposal-hill.obj', 'hill-15.obj']:
        copy(args.repository / 'fixtures' / name, f'Ejemplos/{name}')
    if args.example_project or args.example_terrain:
        if not args.example_project or not args.example_terrain:
            raise ValueError('Both example project and example terrain are required.')
        example = json.loads(args.example_project.read_text(encoding='utf-8'))
        terrain_name = args.example_terrain.name
        example['configuration'] = re.sub(r'(?m)^terrain_path=.*$', f'terrain_path="{terrain_name}"', example['configuration'])
        example['source_dxf'] = terrain_name
        copy(args.example_terrain, f'Ejemplos/{terrain_name}')
        (out / 'Ejemplos' / 'Carretera_en_espiral.json').write_text(json.dumps(example, ensure_ascii=False, indent=2), encoding='utf-8')
    copy(args.repository / 'packaging' / 'LEEME.txt', 'LEEME.txt')
    # Attribute every copied package binary to its exact installed package.
    wanted = {p.relative_to(env).as_posix().lower() for p in sources.values() if p.is_relative_to(env)}
    packages = []
    for metadata in sorted((env / 'conda-meta').glob('*.json')):
        package = json.loads(metadata.read_text(encoding='utf-8'))
        owned = wanted.intersection(x.lower() for x in package.get('files', []))
        if not owned:
            continue
        pkgid = f"{package['name']}-{package['version']}-{package['build']}"
        notice = out / 'Licencias' / pkgid
        notice.mkdir(parents=True, exist_ok=True)
        extracted = pathlib.Path(package.get('extracted_package_dir', ''))
        about_path = extracted / 'info' / 'about.json'
        about = json.loads(about_path.read_text(encoding='utf-8')) if about_path.exists() else {}
        licenses = extracted / 'info' / 'licenses'
        if not licenses.is_dir():
            # Conda split outputs sometimes keep shared license notices only
            # in another output of the exact same source recipe/version.
            parent = about.get('extra', {}).get('parent_recipe')
            for sibling in extracted.parent.glob('*'):
                sibling_about = sibling / 'info' / 'about.json'
                if not parent or not sibling_about.exists() or not (sibling / 'info' / 'licenses').is_dir():
                    continue
                sibling_metadata = json.loads(sibling_about.read_text(encoding='utf-8'))
                if sibling_metadata.get('extra', {}).get('parent_recipe') == parent:
                    licenses = sibling / 'info' / 'licenses'
                    break
        if not licenses.is_dir():
            raise FileNotFoundError(f'Missing license notices for {pkgid}')
        shutil.copytree(licenses, notice, dirs_exist_ok=True)
        record = {k: package.get(k) for k in ['name', 'version', 'build', 'license', 'url', 'sha256']}
        record['binaries'] = sorted(owned)
        record['home'] = about.get('home')
        record['source_url'] = about.get('dev_url') or about.get('doc_url')
        recipe = extracted / 'info' / 'recipe'
        if recipe.exists():
            shutil.copytree(recipe, notice / 'conda-recipe', dirs_exist_ok=True)
        (notice / 'package.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
        packages.append(record)
    (out / 'Licencias' / 'paquetes.json').write_text(json.dumps(packages, indent=2), encoding='utf-8')
    copy(args.repository / 'packaging' / 'TERCEROS.txt', 'Licencias/TERCEROS.txt')
    # The uninstall script only removes files installed by this package. User
    # projects and unknown files in the installation directory remain intact.
    manifest = []
    for path in sorted(out.rglob('*')):
        if path.is_file():
            manifest.append({'path': path.relative_to(out).as_posix(), 'bytes': path.stat().st_size,
                             'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    (out / 'manifest.json').write_text(json.dumps({'product': 'RouteTec', 'version': '1.0.0', 'files': manifest,
                                                'windows_imports': sorted(windows_imports)}, indent=2), encoding='utf-8')
    files = [p.relative_to(out).as_posix() for p in out.rglob('*') if p.is_file()]
    folders = sorted({p.relative_to(out).as_posix() for p in out.rglob('*') if p.is_dir()}, key=lambda s: (s.count('/'), s), reverse=True)
    uninstall = ['Delete "$INSTDIR\\' + f.replace('/', '\\') + '"' for f in sorted(files)]
    uninstall += ['RMDir "$INSTDIR\\' + f.replace('/', '\\') + '"' for f in folders]
    (out.parent / 'uninstall-files.nsh').write_text('\n'.join(uninstall) + '\n', encoding='utf-8-sig')
    print(json.dumps({'stage': str(out), 'files': len(files), 'bytes': sum(p.stat().st_size for p in out.rglob('*') if p.is_file()),
                      'packages': len(packages)}, indent=2))


if __name__ == '__main__':
    main()
