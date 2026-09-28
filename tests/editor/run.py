#!/usr/bin/env python3
"""Editor interaction tests: the real editor, driven by recorded mouse and keyboard input.

    python3 tests/editor/run.py build/bin/aven-editor [name-filter]

Each test copies a template to a temporary folder, runs the editor headless (--screenshot mode,
under xvfb-run on Linux when there's no display) with an --input file, and checks the result:
the Hierarchy printed by the `dump` command, or the scene file after `savescene`.

Input lines are "<frame> <command>": move X Y [frames], down [right], up [right],
key <Name> down|up, type <text>. Coordinates are for the default layout at 1600x900, the size
--screenshot mode uses. If a layout change moves things, update the numbers here.
"""

import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def run_editor(editor, template, frames, inputs="", panels="", extra=()):
    """Runs the editor on a copy of a template; returns (project folder, log, exit code)."""
    work = tempfile.mkdtemp(prefix="aven-editor-test-")
    project = os.path.join(work, "game")
    shutil.copytree(os.path.join(ROOT, "templates", template), project)
    if os.path.exists(os.path.join(project, "template.json")):
        os.remove(os.path.join(project, "template.json"))
    cmd = [editor, project, "--screenshot", os.path.join(work, "shot.png"), "--frames", str(frames)]
    if inputs:
        path = os.path.join(work, "input.txt")
        with open(path, "w") as f:
            f.write(inputs.strip() + "\n")
        cmd += ["--input", path]
    if panels:
        cmd += ["--panel", panels]
    cmd += list(extra)
    if sys.platform.startswith("linux") and not os.environ.get("DISPLAY") and shutil.which("xvfb-run"):
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1920x1400x24"] + cmd
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    return project, proc.stdout + proc.stderr, proc.returncode


def hierarchy(log):
    """The last `dump`: a list of (depth, selected, name)."""
    rows, current = [], []
    for line in log.splitlines():
        m = re.search(r"dump: ( *)(\*?)(.*?) \(-?[\d.]+, -?[\d.]+, -?[\d.]+\)", line)
        if m:
            current.append((len(m.group(1)) // 2, m.group(2) == "*", m.group(3)))
        elif current:
            rows, current = current, []
    return current or rows


def entities(project, scene="scenes/main.scene"):
    with open(os.path.join(project, scene)) as f:
        return json.load(f)["entities"]


def children_of(rows, parent):
    """Names directly inside the first row called `parent`."""
    out, inside, depth = [], False, 0
    for d, _, name in rows:
        if inside:
            if d <= depth:
                break
            if d == depth + 1:
                out.append(name)
        elif name == parent:
            inside, depth = True, d
    return out


# --- tests: each returns None when it passes, or what went wrong

def test_hierarchy_shift_click_drag_into_folder(editor):
    """Shift-click two objects, drag them onto a folder: both move in (only one used to)."""
    project, log, code = run_editor(editor, "platformer", 34, """
8 move 64 297
9 down
10 up
14 key Shift down
15 move 64 325
16 down
17 up
18 key Shift up
22 move 64 325
23 down
24 move 64 300
25 move 64 250
26 move 64 215
27 move 66 213
28 up
""", "@31:dump")
    rows = hierarchy(log)
    inside = children_of(rows, "Level")
    if "Flag" not in inside or "Player" not in inside:
        return "Flag and Player should both be inside Level; Level holds %s" % inside
    if sorted(n for _, sel, n in rows if sel) != ["Flag", "Player"]:
        return "both should stay selected"


def test_hierarchy_arrow_keys(editor):
    """Right opens a folder, Down steps in, Shift+Down extends; Left goes up to the parent."""
    _, log, _ = run_editor(editor, "platformer", 44, """
10 move 80 185
11 down
12 up
16 key Right down
17 key Right up
20 key Down down
21 key Down up
24 key Shift down
25 key Down down
26 key Down up
28 key Down down
29 key Down up
30 key Shift up
34 key Left down
35 key Left up
""", "@31:dump,@38:dump")
    first = [r for r in log.split("dump: Camera")[1:]]
    if len(first) < 2:
        return "expected two dumps"
    selected_then = re.findall(r"dump: +\*(\w+)", first[0])
    if selected_then != ["Hill", "Hill", "Hill"]:
        return "Shift+Down should select three hills, got %s" % selected_then
    selected_after = re.findall(r"dump: *\*(\w+)", first[1])
    if selected_after != ["Background"]:
        return "Left should go up to Background, got %s" % selected_after


def test_assets_drag_several_into_scene(editor):
    """Ctrl-click three pictures in Assets and drag them into the scene: three sprites, one undo step."""
    _, log, _ = run_editor(editor, "platformer", 82, """
8 move 345 750
9 down
10 up
11 down
12 up
15 move 345 750
16 down
17 up
19 key Ctrl down
30 move 545 750
31 down
32 up
45 move 645 750
46 down
47 up
48 key Ctrl up
70 down
71 move 660 740
72 move 700 600
73 move 700 400
74 move 700 300
75 move 702 300
76 up
""", "@79:dump")
    selected = [n for _, sel, n in hierarchy(log) if sel]
    if selected != ["coin", "hero", "slime"]:
        return "expected coin, hero and slime placed and selected, got %s" % selected


def test_add_component_to_two_objects(editor):
    """+ Add Component with two objects selected adds it to both."""
    project, log, _ = run_editor(editor, "platformer", 26, """
8 move 1416 458
9 down
10 up
14 type Rigidbody
17 move 1340 530
18 down
19 up
""", "@3:select:Background|Level,@4:inspector,@23:savescene")
    have = {e["name"]: "RigidBody2D" in e.get("components", {}) for e in entities(project) if e["name"] in ("Background", "Level")}
    if have != {"Background": True, "Level": True}:
        return "both should have a Rigid Body 2D: %s" % have


def test_script_variable_on_two_objects(editor):
    """Changing a script variable with two objects selected changes it on both."""
    project, log, _ = run_editor(editor, "obby-3d", 28, """
8 move 1468 883
9 key Ctrl down
10 down
11 up
12 key Ctrl up
14 key Ctrl down
15 key A down
16 key A up
17 key Ctrl up
18 type 45
20 key Enter down
21 key Enter up
""", "@3:select:Spinning Lava|Trophy,@4:inspector,@25:savescene", ["--size", "1600x1200"])
    speeds = {e["name"]: e["components"]["Script"].get("overrides", {}).get("speed")
              for e in entities(project) if e["name"] in ("Spinning Lava", "Trophy")}
    if speeds != {"Spinning Lava": 45, "Trophy": 45}:
        return "both spinners should have speed 45: %s" % speeds


def test_export_desktop_app(editor):
    """Build & Share > Desktop apps makes this system's app: a zip whose program runs the game."""
    import zipfile
    project, log, code = run_editor(editor, "platformer", 8, "", "@3:exportapps")
    exports = os.path.join(project, "exports")
    zips = [f for f in os.listdir(exports) if f.endswith(".zip")] if os.path.isdir(exports) else []
    if not zips:
        return "no app zip was made:\n" + log[-1500:]
    if not sys.platform.startswith("linux"):
        return None
    z = zipfile.ZipFile(os.path.join(exports, zips[0]))
    if z.testzip() is not None:
        return "the zip is damaged"
    out = tempfile.mkdtemp(prefix="aven-app-")
    program = None
    for info in z.infolist():
        path = z.extract(info, out)
        mode = info.external_attr >> 16
        if mode:
            os.chmod(path, mode & 0o777)
        if mode & 0o111:
            program = path
    if not program:
        return "the program in the zip isn't marked runnable"
    cmd = [program, "--screenshot", os.path.join(out, "shot.png"), "--frames", "10", "--size", "320x180", "--hidden"]
    if not os.environ.get("DISPLAY") and shutil.which("xvfb-run"):
        cmd = ["xvfb-run", "-a"] + cmd
    run = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    if run.returncode != 0 or not os.path.exists(os.path.join(out, "shot.png")):
        return "the exported game didn't run:\n" + (run.stdout + run.stderr)[-1500:]


def test_update_downloads_and_installs(editor):
    """The updater: a newer, signed release is found, downloaded, checked and swapped in when Aven
    closes, and "Go back" restores the old one. Refused, changing nothing: a download that doesn't
    match its SHA-256, one with no signature, one signed by another key, and an old version's signed
    download passed off as a new one. (Linux: a copy of the editor stands in for a download.)"""
    import hashlib
    import zipfile
    sys.path.insert(0, os.path.join(ROOT, "tools", "release"))
    import update_key
    if not sys.platform.startswith("linux"):
        return None
    work = tempfile.mkdtemp(prefix="aven-update-test-")
    game = os.path.join(work, "game")
    shutil.copytree(os.path.join(ROOT, "templates", "platformer"), game)
    marker = b"AVEN-NEW-BUILD"
    key, other_key = os.urandom(32), os.urandom(32)

    def make_package(version):
        path = os.path.join(work, "aven-%s-linux-x64.zip" % version)
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
            for name, data, mode in (("aven-editor", open(editor, "rb").read() + marker, 0o755),
                                     ("START HERE.txt", b"new", 0o644), ("templates/new.txt", b"hello", 0o644)):
                info = zipfile.ZipInfo("aven-%s-linux-x64/" % version + name)
                info.create_system, info.external_attr, info.compress_type = 3, (0o100000 | mode) << 16, zipfile.ZIP_DEFLATED
                z.writestr(info, data)
        return path

    def sign(path, seed, name=None):
        text = update_key.message(path)
        if name: # sign as if the file had another name
            text = text.replace(os.path.basename(path).encode(), name.encode())
        with open(path + ".sig", "w") as f:
            f.write(update_key.sign(seed, text).hex())
        return path + ".sig"

    package = make_package("9.9.9")
    data = open(package, "rb").read()
    good_sig = sign(package, key)
    count = [0]

    def run_editor_copy(install, extra_env, panel):
        cmd = [os.path.join(install, "aven-editor"), game, "--screenshot", os.path.join(work, "shot.png"),
               "--frames", "6", "--panel", panel]
        if not os.environ.get("DISPLAY") and shutil.which("xvfb-run"):
            cmd = ["xvfb-run", "-a", "-s", "-screen 0 1920x1080x24"] + cmd
        env = dict(os.environ, AVEN_UPDATE_PUBLIC_KEY=update_key.public_key(key).hex(), **extra_env)
        run = subprocess.run(cmd, capture_output=True, text=True, timeout=300, env=env)
        return run.stdout + run.stderr

    def attempt(digest=None, sig=good_sig, package_path=package):
        count[0] += 1
        install = os.path.join(work, "Aven-%d" % count[0])
        os.makedirs(os.path.join(install, "templates"))
        shutil.copy2(editor, install)
        for name, text in (("START HERE.txt", "old"), ("templates/old.txt", "old"), ("My notes.txt", "mine")):
            with open(os.path.join(install, name), "w") as f:
                f.write(text)
        releases = os.path.join(work, "releases-%d.json" % count[0])
        assets = [{"name": "aven-9.9.9-linux-x64.zip", "size": os.path.getsize(package_path),
                   "digest": "sha256:" + (digest or hashlib.sha256(open(package_path, "rb").read()).hexdigest()),
                   "browser_download_url": "file://" + package_path}]
        if sig:
            assets.append({"name": "aven-9.9.9-linux-x64.zip.sig", "size": 128, "browser_download_url": "file://" + sig})
        with open(releases, "w") as f:
            json.dump([{"tag_name": "v9.9.9", "prerelease": False, "draft": False, "body": "## New\n- things",
                        "html_url": "https://example.com", "assets": assets}], f)
        return install, run_editor_copy(install, {"AVEN_UPDATE_URL": "file://" + releases}, "@3:update")

    def unchanged(install):
        return (not open(os.path.join(install, "aven-editor"), "rb").read().endswith(marker)
                and open(os.path.join(install, "START HERE.txt")).read() == "old")

    install, log = attempt()
    if "update: 9.9.9 ready" not in log:
        return "the update wasn't downloaded:\n" + log[-1500:]
    if not open(os.path.join(install, "aven-editor"), "rb").read().endswith(marker):
        return "the new editor wasn't put in place:\n" + log[-1500:]
    if not os.access(os.path.join(install, "aven-editor"), os.X_OK):
        return "the new editor isn't runnable"
    if open(os.path.join(install, "START HERE.txt")).read() != "new" or not os.path.exists(os.path.join(install, "templates/new.txt")):
        return "the new files weren't put in place"
    if os.path.exists(os.path.join(install, "templates/old.txt")):
        return "the old templates folder should have been replaced"
    if open(os.path.join(install, "My notes.txt")).read() != "mine":
        return "a file of the user's was touched"
    # The new version starts and keeps the old one, to go back to...
    log = run_editor_copy(install, {}, "@3:dump")
    if "Aven Editor" not in log or not os.path.exists(os.path.join(install, ".aven-update", "old", "aven-editor")):
        return "the updated editor should start, keeping the old version in .aven-update/old:\n" + log[-1500:]
    # ...which "Go back" puts back.
    log = run_editor_copy(install, {}, "@3:rollback")
    if open(os.path.join(install, "aven-editor"), "rb").read().endswith(marker) or open(os.path.join(install, "START HERE.txt")).read() != "old":
        return "going back should restore the old version:\n" + log[-1500:]
    if not os.path.exists(os.path.join(install, "templates/old.txt")) or open(os.path.join(install, "My notes.txt")).read() != "mine":
        return "going back should restore the old templates and leave the user's files alone"

    cases = [
        ("a download that doesn't match its SHA-256", dict(digest="0" * 64), "SHA-256"),
        ("an unsigned release", dict(sig=None), "isn't signed"),
        ("a release signed by another key", dict(sig=sign(make_package("9.9.9-other"), other_key, "aven-9.9.9-linux-x64.zip")), "signed by Aven's release key"),
    ]
    # An old version's genuine download, renamed to look like 9.9.9: its signature names the old version.
    old = make_package("0.0.1")
    old_sig = sign(old, key)
    fake = os.path.join(work, "fake", "aven-9.9.9-linux-x64.zip")
    os.makedirs(os.path.dirname(fake))
    shutil.copy2(old, fake)
    cases.append(("an old version's signed download posing as 9.9.9", dict(sig=old_sig, package_path=fake), "signed by Aven's release key"))
    for what, kwargs, reason in cases:
        install, log = attempt(**kwargs)
        if "update: 9.9.9 failed" not in log or reason not in log:
            return what + " should be refused (" + reason + "):\n" + log[-1500:]
        if not unchanged(install):
            return what + " must not change anything"


def test_monkey(editor):
    """Random clicks, drags, keys and typing for a while, editing and playing: no crash."""
    for seed, template, play in ((1, "platformer", False), (2, "obby-3d", True)):
        r = random.Random(seed)
        lines, f = [], 5
        keys = ["Delete", "Escape", "Enter", "Tab", "F2", "Up", "Down", "Left", "Right", "A", "D", "Z", "C", "V", "G"]
        while f < 390:
            x, y = r.randint(0, 1599), r.randint(0, 899)
            kind = r.random()
            if kind < 0.5:
                lines += ["%d move %d %d" % (f, x, y), "%d down" % (f + 1), "%d up" % (f + 2)]
                f += 3
            elif kind < 0.7:
                x2, y2 = r.randint(0, 1599), r.randint(0, 899)
                lines += ["%d move %d %d" % (f, x, y), "%d down" % (f + 1), "%d move %d %d 5" % (f + 2, x2, y2), "%d up" % (f + 8)]
                f += 9
            elif kind < 0.85:
                k = r.choice(keys)
                lines += ["%d key %s down" % (f, k), "%d key %s up" % (f + 1, k)]
                f += 2
            else:
                k = r.choice(keys)
                lines += ["%d key Ctrl down" % f, "%d key %s down" % (f + 1, k), "%d key %s up" % (f + 2, k), "%d key Ctrl up" % (f + 3)]
                f += 4
            f += r.randint(0, 3)
        _, log, code = run_editor(editor, template, 400, "\n".join(lines), "", ["--play"] if play else [])
        if code != 0:
            return "seed %d (%s) exited with %d:\n%s" % (seed, template, code, log[-2000:])


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    editor = os.path.abspath(sys.argv[1])
    only = sys.argv[2] if len(sys.argv) > 2 else ""
    tests = [(n, f) for n, f in sorted(globals().items()) if n.startswith("test_") and only in n]
    failed = 0
    for name, fn in tests:
        problem = fn(editor)
        print(("ok   " if problem is None else "FAIL ") + name[5:] + ("" if problem is None else ": " + problem), flush=True)
        failed += problem is not None
    print("%d/%d editor tests passed" % (len(tests) - failed, len(tests)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
