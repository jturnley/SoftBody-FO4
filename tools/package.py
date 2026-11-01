"""Pack the built mod/ into an MO2-installable zip: dist/SoftBodyFO4-<date>-<commit>.zip with Materials, meshes,
Textures and Tools at the top. Run after build_prototype.py and a commit; an uncommitted mod/ gets "-dirty".

  python -I tools/package.py                    the zip only
  python -I tools/package.py --release v0.1.0   the zip, published as a GitHub pre-release of HEAD (tag v0.1.0)

A release's notes are the commits since the previous release, plus the install steps.
"""
import datetime
import os
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(ROOT, "mod")
DIST = os.path.join(ROOT, "dist")
INSTALL = """### Install
1. Install the zip with MO2. It must win conflicts with CBBE and any other SMP body; its `FemaleHands.nif` overrides other hands mods.
2. In BodySlide, build **CBBE Body SoftBody** (group CBBE) with your preset (tick Build Morphs for LooksMenu).

Needs F4SE, CBBE + BodySlide, FO4 Faster HDT-SMP and a female skeleton with SMP support. Prototype: see the README for what is tested."""


def git(*args):
    return subprocess.run(["git", "-C", ROOT, *args], capture_output=True, text=True, check=True).stdout.strip()


def package():
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
    return out, bool(dirty)


def release(tag, zip_path):
    if git("status", "--porcelain"):
        raise SystemExit("commit and push first: a release must match a pushed commit")
    previous = subprocess.run(["git", "-C", ROOT, "describe", "--tags", "--abbrev=0"], capture_output=True, text=True)
    since = previous.stdout.strip() + "..HEAD" if previous.returncode == 0 else "HEAD"
    changes = git("log", "--format=- %s", since)
    notes = "### Changes\n%s\n\n%s\n" % (changes, INSTALL)
    with tempfile.NamedTemporaryFile("w", suffix=".md", delete=False, encoding="utf-8") as f:
        f.write(notes)
    subprocess.run(["gh", "release", "create", tag, zip_path, "--repo", "jturnley/SoftBody-FO4", "--target",
                    git("rev-parse", "HEAD"), "--title", "SoftBody FO4 %s" % tag, "--notes-file", f.name,
                    "--prerelease"], cwd=ROOT, check=True)
    os.unlink(f.name)


def main(argv):
    zip_path, dirty = package()
    if "--release" in argv:
        if dirty:
            raise SystemExit("mod/ has uncommitted changes; not releasing")
        release(argv[argv.index("--release") + 1], zip_path)


if __name__ == "__main__":
    main(sys.argv)
