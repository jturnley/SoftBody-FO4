"""Pack the built mod/ into an MO2-installable zip: dist/SoftBodyFO4-<date>-<commit>.zip with Materials, meshes,
Textures and Tools at the top. Run after build_prototype.py and a commit; an uncommitted mod/ gets "-dirty".

  python -I tools/package.py
"""
import datetime
import os
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(ROOT, "mod")
DIST = os.path.join(ROOT, "dist")


def git(*args):
    return subprocess.run(["git", "-C", ROOT, *args], capture_output=True, text=True, check=True).stdout.strip()


def main():
    commit = git("rev-parse", "--short", "HEAD")
    dirty = git("status", "--porcelain", "--", "mod")
    name = "SoftBodyFO4-%s-%s%s.zip" % (datetime.date.today().strftime("%Y%m%d"), commit, "-dirty" if dirty else "")
    os.makedirs(DIST, exist_ok=True)
    out = os.path.join(DIST, name)
    count = 0
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for folder, _, files in os.walk(MOD):
            for f in sorted(files):
                path = os.path.join(folder, f)
                z.write(path, os.path.relpath(path, MOD))
                count += 1
    print("%s: %d files, %.1f MB%s" % (out, count, os.path.getsize(out) / 2 ** 20,
                                       " (mod/ has uncommitted changes)" if dirty else ""))
    return out


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
