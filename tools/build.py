"""Build Edge Island without downloading dependencies or publishing anything."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import resources

ROOT = resources.ROOT
SOURCES = {
    "app": ["src/model.c", "src/draw.c", "src/audio.c", "src/media.c", "src/startup.c", "src/language.c", "src/main.c"],
    "uninstall": ["packaging/install.c", "src/startup.c", "src/language.c", "packaging/uninstall.c"],
    "setup": ["packaging/install.c", "packaging/setup.c", "packaging/wizard.c", "src/language.c"],
}
SDK_LIBS = ["user32", "gdi32", "msimg32", "kernel32", "gdiplus", "advapi32", "shell32", "shcore",
            "ole32", "runtimeobject", "comctl32"]

def msvc_environment():
    if os.name != "nt":
        raise RuntimeError("Use Windows with Visual Studio C++ Build Tools, or specify --tcc and --tcc-root on Linux.")
    if shutil.which("cl") and os.environ.get("VSCMD_ARG_TGT_ARCH", "").lower() == "x64":
        return dict(os.environ)
    finder = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if not finder.exists():
        raise RuntimeError("Install Visual Studio Build Tools with 'Desktop development with C++' and a Windows SDK.")
    path = subprocess.check_output([str(finder), "-latest", "-products", "*", "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath", "-utf8"], encoding="utf-8-sig").strip()
    if not path:
        raise RuntimeError("The x64 MSVC toolchain could not be found.")
    vcvars = Path(path) / "VC/Auxiliary/Build/vcvars64.bat"
    # cmd /u makes the captured environment lossless on non-English Windows.
    command = 'call "' + str(vcvars) + '" >nul && set'
    result = subprocess.run(command_line_for_cmd(command), stdout=subprocess.PIPE, check=True)
    env = dict(os.environ)
    for entry in result.stdout.decode("utf-16-le").splitlines():
        name, sep, value = entry.partition("=")
        if name and sep:
            env[name.upper()] = value
    return env

def command_line_for_cmd(command):
    shell = os.environ["COMSPEC"]
    return f'"{shell}" /d /u /s /c "{command}"'

def tcc_imports(directory):
    exports = json.loads((ROOT / "tools/tcc-imports.json").read_text())
    directory.mkdir(parents=True, exist_ok=True)
    definitions = []
    for dll, symbols in exports.items():
        path = directory / (dll.split(".")[0] + ".def")
        path.write_text("LIBRARY " + dll + "\nEXPORTS\n" + "\n".join(symbols) + "\n", encoding="ascii")
        definitions.append(str(path))
    return definitions

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", choices=["all", "payload", "setup"], default="all")
    parser.add_argument("--out", type=Path, default=ROOT / "build/bin")
    parser.add_argument("--payload-dir", type=Path, help="Exact app and uninstaller to embed in setup")
    parser.add_argument("--release-candidate", action="store_true", help="Use neutral setup text in the protected signing workflow")
    parser.add_argument("--tcc", type=Path, help="Path to an x86_64-win32-tcc cross compiler")
    parser.add_argument("--tcc-root", type=Path, help="Matching TinyCC source distribution, including win32/include")
    args = parser.parse_args()
    if bool(args.tcc) != bool(args.tcc_root):
        parser.error("Specify both TinyCC options together.")
    out = args.out.resolve()
    payload = (args.payload_dir or out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    intermediate = out.parent / (out.name + "-objects")
    env = dict(os.environ) if args.tcc else msvc_environment()
    kinds = ["app", "uninstall", "setup"] if args.stage == "all" else ["app", "uninstall"] if args.stage == "payload" else ["setup"]
    defs = tcc_imports(intermediate / "imports") if args.tcc else []
    for kind in kinds:
        obj = intermediate / kind
        obj.mkdir(parents=True, exist_ok=True)
        target = out / resources.NAMES[kind]
        files = [str(ROOT / name) for name in SOURCES[kind]]
        contents = resources.items(kind, payload)
        if args.tcc:
            r = obj / "resources.o"
            r.write_bytes(resources.coff(contents))
            tc = args.tcc_root.resolve()
            cmd = [str(args.tcc.resolve()), "-B" + str(tc), "-nostdinc", "-I" + str(tc/"include"),
                "-I" + str(tc/"win32/include"), "-I" + str(tc/"win32/include/winapi"), "-DEDGE_FREESTANDING=1",
                "-nostdlib", "-Wl,-subsystem=gui,-dynamicbase,-high-entropy-va,-nxcompat,-large-address-aware,-e=edge_entry"]
            if args.release_candidate:
                cmd.append("-DEDGE_SIGNING_CANDIDATE=1")
            cmd += ["-o", str(target), *files, str(r), *defs]
        else:
            r = obj / "resources.res"
            r.write_bytes(resources.res(contents))
            compiler = shutil.which("cl.exe", path=env.get("PATH"))
            if not compiler:
                raise RuntimeError("vcvars64 did not provide cl.exe.")
            cmd = [compiler, "/nologo", "/TC", "/std:c11", "/utf-8", "/O2", "/W3", "/MT", "/GS", "/guard:cf",
                   "/D_CRT_SECURE_NO_WARNINGS", "/DUNICODE", "/D_UNICODE"]
            if args.release_candidate:
                cmd.append("/DEDGE_SIGNING_CANDIDATE=1")
            cmd += [*files, "/link", str(r), "/SUBSYSTEM:WINDOWS,10.00", "/MACHINE:X64", "/NXCOMPAT",
                    "/DYNAMICBASE", "/HIGHENTROPYVA", "/GUARD:CF", "/OPT:REF", "/OPT:ICF", "/Brepro",
                    "/INCREMENTAL:NO", "/MANIFEST:NO", "/OUT:" + str(target), *[x + ".lib" for x in SDK_LIBS]]
        subprocess.run(cmd, cwd=obj, env=env, check=True)
        print(f"{target.name}: {target.stat().st_size:,} bytes")

if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
