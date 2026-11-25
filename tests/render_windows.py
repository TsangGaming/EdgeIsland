"""Run the renderer and system-media regression cases with the application's Windows SDK."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build import msvc_environment

def main():
    env = msvc_environment()
    compiler = shutil.which("cl.exe", path=env["PATH"])
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "render_test.exe"
        subprocess.run([compiler, "/nologo", "/std:c11", "/TC", "/utf-8", "/O2", "/MT",
                        "/D_CRT_SECURE_NO_WARNINGS", str(ROOT / "tests/render_test.c"),
                        str(ROOT / "src/draw.c"), str(ROOT / "src/model.c"), "/link", "/OUT:" + str(exe),
                        "user32.lib", "gdi32.lib", "msimg32.lib", "gdiplus.lib"], env=env, cwd=tmp, check=True)
        subprocess.run([str(exe)], cwd=tmp, check=True)
        media = Path(tmp) / "media_test.exe"
        subprocess.run([compiler, "/nologo", "/std:c11", "/TC", "/utf-8", "/O2", "/MT",
                        "/D_CRT_SECURE_NO_WARNINGS", str(ROOT / "tests/media_test.c"),
                        str(ROOT / "src/model.c"), "/link", "/OUT:" + str(media), "user32.lib", "gdi32.lib",
                        "gdiplus.lib", "shcore.lib", "runtimeobject.lib"], env=env, cwd=tmp, check=True)
        subprocess.run([str(media)], cwd=tmp, check=True)

if __name__ == "__main__":
    main()
