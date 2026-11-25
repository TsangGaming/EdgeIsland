"""Build the unsigned Store-submission MSIX, without altering the EXE workflow."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

import build
import resources

ROOT = resources.ROOT
NS = {'f': 'http://schemas.microsoft.com/appx/manifest/foundation/windows10',
      'uap': 'http://schemas.microsoft.com/appx/manifest/uap/windows10',
      'desktop': 'http://schemas.microsoft.com/appx/manifest/desktop/windows10'}
IDENTITY = 'TsangGaming.EdgeIsland'
PUBLISHER = 'CN=519D7D56-0B43-4900-B8E9-3537D891A7FF'
LOGOS = {'StoreLogo.png': 50, 'Square150x150Logo.png': 150, 'Square44x44Logo.png': 44}


def checked(cmd, env=None, cwd=None):
    subprocess.run([str(x) for x in cmd], env=env, cwd=cwd, check=True)


def sdk_tool(name, env):
    sdk = Path(env['WINDOWSSDKDIR'])
    preferred = sdk / 'bin' / env['WINDOWSSDKVERSION'].strip('\\/') / 'x64' / name
    if preferred.is_file():
        return preferred
    for path in sorted((sdk / 'bin').glob('*/x64/' + name), reverse=True):
        if path.is_file():
            return path
    raise RuntimeError(f'Windows SDK tool not found: {name}')


def validate_manifest(data):
    root = ET.fromstring(data)
    identity = root.find('f:Identity', NS)
    assert identity.attrib == {'Name': IDENTITY, 'Publisher': PUBLISHER,
                                'Version': resources.VERSION, 'ProcessorArchitecture': 'x64'}, identity.attrib
    assert resources.VERSION.endswith('.0'), 'Store reserves the fourth version component'
    assert root.findtext('f:Properties/f:PublisherDisplayName', namespaces=NS) == 'TsangGaming'
    task = root.find('.//desktop:StartupTask', NS)
    assert task.get('TaskId') == 'EdgeIslandStartup' and task.get('Enabled') == 'false'
    app = root.find('f:Applications/f:Application', NS)
    assert app.get('Executable') == 'EdgeIsland.exe'


def png_size(data):
    assert data[:8] == b'\x89PNG\r\n\x1a\n' and data[12:16] == b'IHDR'
    return struct.unpack('>II', data[16:24])


def verify_package(package, exe):
    with zipfile.ZipFile(package) as z:
        assert z.testzip() is None, 'Corrupted MSIX ZIP member'
        names = set(z.namelist())
        validate_manifest(z.read('AppxManifest.xml'))
        assert z.read('EdgeIsland.exe') == exe.read_bytes(), 'Packaged executable differs from build'
        assert {'AppxManifest.xml', 'AppxBlockMap.xml', 'EdgeIsland.exe', 'LICENSE', 'PRIVACY.md'} <= names
        assert 'AppxSignature.p7x' not in names, 'Submission artifact expected to be unsigned'
        assert not any(n.lower().endswith(('setup.exe', 'uninstall.exe')) for n in names)
        for logo, size in LOGOS.items():
            assert png_size(z.read('Assets/' + logo)) == (size, size)
    print('Verified MSIX identity, version, startup default, assets, payload and unsigned status.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, default=ROOT / 'build/store')
    args = p.parse_args()
    out = args.out.resolve()
    env = build.msvc_environment()
    compiler = shutil.which('cl.exe', path=env.get('PATH'))
    if not compiler:
        raise RuntimeError('MSVC compiler not found')
    cppwinrt = Path(env['WINDOWSSDKDIR']) / 'Include' / env['WINDOWSSDKVERSION'].strip('\\/') / 'cppwinrt'
    if not (cppwinrt / 'winrt/Windows.ApplicationModel.h').is_file():
        raise RuntimeError('Install the Windows SDK C++/WinRT headers')
    source = ROOT / 'packaging/msix'
    stage = out / 'package'
    obj = out / 'objects'
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    obj.mkdir(parents=True, exist_ok=True)
    common = ['/nologo', '/utf-8', '/O2', '/W3', '/MT', '/GS', '/guard:cf',
              '/D_CRT_SECURE_NO_WARNINGS', '/DUNICODE', '/D_UNICODE', '/DEDGE_STORE_MSIX=1']
    objects = []
    # Reuse the current production application source list, excluding the Run-key implementation.
    for path in build.SOURCES['app']:
        if path == 'src/startup.c':
            continue
        target = obj / (Path(path).stem + '.obj')
        checked([compiler, *common, '/TC', '/std:c11', '/c', ROOT / path, '/Fo' + str(target)], env, obj)
        objects.append(target)
    target = obj / 'startup_store.obj'
    checked([compiler, *common, '/TP', '/std:c++17', '/EHsc', '/bigobj', '/I' + str(cppwinrt), '/c',
             source / 'startup_store.cpp', '/Fo' + str(target)], env, obj)
    objects.append(target)
    res = obj / 'resources.res'
    res.write_bytes(resources.res(resources.items('app', None)))
    exe = stage / 'EdgeIsland.exe'
    checked([compiler, '/nologo', *objects, '/link', res, '/SUBSYSTEM:WINDOWS,10.00', '/MACHINE:X64',
             '/NXCOMPAT', '/DYNAMICBASE', '/HIGHENTROPYVA', '/GUARD:CF', '/OPT:REF', '/OPT:ICF', '/Brepro',
             '/INCREMENTAL:NO', '/MANIFEST:NO', '/OUT:' + str(exe),
             *[x + '.lib' for x in build.SDK_LIBS], 'windowsapp.lib'], env, obj)
    manifest = (source / 'AppxManifest.xml').read_text(encoding='utf-8')
    # Sync future Store versions to the same version header used by the EXE build.
    manifest = manifest.replace('Version="0.3.0.0"', f'Version="{resources.VERSION}"', 1)
    validate_manifest(manifest)
    (stage / 'AppxManifest.xml').write_text(manifest, encoding='utf-8')
    shutil.copytree(source / 'Assets', stage / 'Assets')
    shutil.copyfile(ROOT / 'LICENSE', stage / 'LICENSE')
    shutil.copyfile(ROOT / 'docs/PRIVACY-STORE.md', stage / 'PRIVACY.md')
    package = out / f'EdgeIsland-{resources.VERSION}-x64-STORE.msix'
    checked([sdk_tool('MakeAppx.exe', env), 'pack', '/d', stage, '/p', package, '/o'], env)
    verify_package(package, exe)
    digest = hashlib.sha256(package.read_bytes()).hexdigest()
    (out / 'SHA256SUMS.txt').write_text(f'{digest}  {package.name}\n', encoding='ascii')
    print(f'Store submission package: {package}\nSHA-256: {digest}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError, AssertionError, KeyError) as error:
        sys.exit(f'MSIX build failed: {error}')
