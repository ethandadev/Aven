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

# Every folder a test makes, removed once the run ends (AVEN_KEEP_TEST_FILES=1 keeps them, to look
# at after a failure). The update test alone makes the best part of a gigabyte.
_made = []


def temp_dir(prefix):
    path = tempfile.mkdtemp(prefix=prefix)
    _made.append(path)
    return path


def run_editor(editor, template, frames, inputs="", panels="", extra=()):
    """Runs the editor on a copy of a template; returns (project folder, log, exit code)."""
    work = temp_dir("aven-editor-test-")
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
    out = temp_dir("aven-app-")
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
    work = temp_dir("aven-update-test-")
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


def test_native_build_asks_before_running_someone_elses_cmake(editor):
    """A project's own native/CMakeLists.txt can run any command while building, so Build asks first
    (and runs nothing) unless it's the one Aven makes."""
    work = temp_dir("aven-native-test-")
    project = os.path.join(work, "game")
    shutil.copytree(os.path.join(ROOT, "templates", "platformer"), project)
    os.makedirs(os.path.join(project, "native", "src"))
    ran = os.path.join(work, "cmake-ran")
    with open(os.path.join(project, "native", "CMakeLists.txt"), "w") as f:
        f.write('cmake_minimum_required(VERSION 3.10)\nproject(x C)\nfile(WRITE "%s" "yes")\n' % ran.replace("\\", "/"))
    with open(os.path.join(project, "native", "src", "behaviors.c"), "w") as f:
        f.write("int x;\n")
    cmd = [editor, project, "--screenshot", os.path.join(work, "shot.png"), "--frames", "8", "--panel", "@3:buildnative"]
    if sys.platform.startswith("linux") and not os.environ.get("DISPLAY") and shutil.which("xvfb-run"):
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1920x1080x24"] + cmd
    run = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    log = run.stdout + run.stderr
    if "native build: asks first" not in log:
        return "Build should ask before running this CMakeLists.txt:\n" + log[-1500:]
    if os.path.exists(ran):
        return "the project's CMake script ran without asking"
    # Aven's own CMakeLists.txt builds straight away.
    shutil.copy2(os.path.join(ROOT, "sdk", "template", "CMakeLists.txt"), os.path.join(project, "native", "CMakeLists.txt"))
    run = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    if "native build: started" not in run.stdout + run.stderr:
        return "Aven's own CMakeLists.txt should build without asking:\n" + (run.stdout + run.stderr)[-1500:]


def test_crash_recovery(editor):
    """Unsaved changes survive Aven closing unexpectedly: the next time the project opens, they're
    offered back; a normal close leaves nothing behind."""
    work = temp_dir("aven-recovery-test-")
    project = os.path.join(work, "game")
    shutil.copytree(os.path.join(ROOT, "templates", "platformer"), project)

    def run(panels, frames=10):
        cmd = [editor, project, "--screenshot", os.path.join(work, "shot.png"), "--frames", str(frames), "--panel", panels]
        if sys.platform.startswith("linux") and not os.environ.get("DISPLAY") and shutil.which("xvfb-run"):
            cmd = ["xvfb-run", "-a", "-s", "-screen 0 1920x1080x24"] + cmd
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        return r.returncode, r.stdout + r.stderr

    code, log = run("@3:create:Star,@5:recoverysnapshot,@6:crash")
    if code != 3:
        return "the pretend crash should end the editor (exit 3), got %d:\n%s" % (code, log[-1500:])
    code, log = run("@4:dump") # closed without answering: still offered next time
    code, log = run("@4:recover,@7:dump")
    if "recovery: unsaved changes" not in log:
        return "reopening should offer the unsaved changes:\n" + log[-1500:]
    if "dump: Star" not in log:
        return "recovering should bring back the new Star:\n" + log[-1500:]
    if "Star" in open(os.path.join(project, "scenes", "main.scene")).read():
        return "recovering must not save over the scene by itself"
    code, log = run("@4:dump")
    if "recovery:" in log:
        return "after a normal close, there should be nothing left to recover"


def test_debugger_stops_at_a_breakpoint(editor):
    """A breakpoint stops the game at a line, shows the values there, steps a line at a time, and
    the game runs on once it's removed."""
    panels = ("@3:breakpoint:scripts/slime.es:13,@4:play,@12:debug:values,@13:debug:over,@14:debug:values,"
              "@15:breakpoint:scripts/slime.es:13,@16:debug:continue,@40:dump")
    _, log, code = run_editor(editor, "platformer", 45, "", panels)
    if code != 0:
        return "exit %d:\n%s" % (code, log[-1500:])
    stops = [l for l in log.splitlines() if "debugger: stopped at scripts/slime.es line 13" in l]
    if not stops:
        return "it should stop at the breakpoint:\n" + log[-2000:]
    if "debugger:   dt = " not in log or "debugger:   speed = 1.5 (script)" not in log:
        return "the Debugger should show dt and the script's speed:\n" + log[-2000:]
    if "debugger: in on_update() at scripts/slime.es:14" not in log:
        return "Step Over should go on to line 14:\n" + log[-2000:]
    if len(stops) > 3:
        return "with the breakpoint removed, the game should run on (stopped %d times)" % len(stops)


def read_png(path):
    """(width, height, rows of RGBA bytes) from an 8-bit, non-interlaced PNG like the editor writes."""
    import struct
    import zlib
    data = open(path, "rb").read()
    pos, idat, width, height, kind = 8, b"", 0, 0, 6
    while pos < len(data):
        length, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if tag == b"IHDR":
            width, height, _, kind = struct.unpack(">IIBB", body[:10])
        elif tag == b"IDAT":
            idat += body
        pos += 12 + length
    channels = {2: 3, 6: 4}[kind]
    raw, stride, rows, prev = zlib.decompress(idat), width * channels, [], bytearray(width * channels)
    for y in range(height):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b, c = prev[i], prev[i - channels] if i >= channels else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line) if channels == 4 else bytes(v for x in range(width) for v in (*line[x * 3:x * 3 + 3], 255)))
        prev = line
    return width, height, rows


def test_shadows_follow_moved_objects(editor):
    """Shadow maps are kept between frames while nothing changes. Moving an object has to draw them
    again: a cube moved mid-run looks the same as one that started there (its shadow moved too)."""
    def shot(move_at, start_x):
        work = temp_dir("aven-shadow-test-")
        project = os.path.join(work, "game")
        shutil.copytree(os.path.join(ROOT, "templates", "blank-3d"), project)
        scene_path = os.path.join(project, "scenes", "main.scene")
        scene = json.load(open(scene_path))
        for e in scene["entities"]:
            if e.get("name") == "Cube":
                e["components"]["Transform"]["position"][0] = start_x
        json.dump(scene, open(scene_path, "w"))
        os.makedirs(os.path.join(project, "editor_tools"), exist_ok=True)
        with open(os.path.join(project, "editor_tools", "move_cube.es"), "w") as f:
            f.write("def run():\n    find(\"Cube\").x = 2.5\n")
        png = os.path.join(work, "shot.png")
        cmd = [editor, project, "--screenshot", png, "--frames", "40"]
        if move_at:
            cmd += ["--panel", "@%d:tool:editor_tools/move_cube.es" % move_at]
        if sys.platform.startswith("linux") and not os.environ.get("DISPLAY") and shutil.which("xvfb-run"):
            cmd = ["xvfb-run", "-a", "-s", "-screen 0 1920x1400x24"] + cmd
        run = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        if run.returncode != 0 or not os.path.exists(png):
            return None, (run.stdout + run.stderr)[-1500:]
        return read_png(png), ""

    def differing(a, b): # inside the scene view only (running a tool also shows a message, "unsaved"...)
        (w, h, ra), (_, _, rb) = a, b
        return sum(1 for y in range(h * 16 // 100, h * 65 // 100, 2) for x in range(w * 4 * 20 // 100, w * 4 * 75 // 100, 4)
                   if abs(ra[y][x] - rb[y][x]) > 40)

    moved, log = shot(20, 0)
    if not moved:
        return "the editor didn't run:\n" + log
    started, log = shot(0, 2.5)
    if not started:
        return "the editor didn't run:\n" + log
    unmoved, log = shot(0, 0)
    if not unmoved:
        return "the editor didn't run:\n" + log
    if differing(unmoved, started) < 50:
        return "the test can't tell the cube's two places apart"
    wrong = differing(moved, started)
    if wrong > 20:
        return "after moving the cube, %d sampled pixels differ from a cube that started there: an old shadow left behind?" % wrong


def test_shortcuts(editor):
    """Keymaps have no two commands on the same keys; a new second key for Undo works; Ctrl+W
    closes a script tab; Ctrl+Shift+S saves the scene under a new name."""
    _, log, _ = run_editor(editor, "platformer", 4, "", "keycheck")
    if "keycheck: 0 problems" not in log:
        return "keymaps have clashes:\n" + "\n".join(l for l in log.splitlines() if "keycheck:" in l)

    # Preferences > Shortcuts: click Undo's second keys, press Ctrl+U. Then delete Flag and Ctrl+U it back.
    _, log, _ = run_editor(editor, "platformer", 44, """
10 move 1080 659
11 down
12 up
16 key Ctrl down
17 key U down
18 key U up
19 key Ctrl up
22 move 64 297
23 down
24 up
26 key Delete down
27 key Delete up
32 key Ctrl down
33 key U down
34 key U up
35 key Ctrl up
""", "prefs:Shortcuts,@30:dump,@40:dump,@41:keys")
    if "keys: undo/2 = Ctrl+U" not in log:
        return "Ctrl+U didn't become Undo's second keys"
    dumps = log.split("dump: Camera")
    flag = re.compile(r"dump: \*?Flag ")
    if len(dumps) < 3 or flag.search(dumps[1]) or not flag.search(dumps[2]):
        return "Delete then Ctrl+U should remove Flag and bring it back"

    project, log, _ = run_editor(editor, "platformer", 50, """
14 key Ctrl down
15 key W down
16 key W up
17 key Ctrl up
24 move 800 400
25 key Ctrl down
26 key Shift down
27 key S down
28 key S up
29 key Shift up
30 key Ctrl up
34 key Ctrl down
35 key A down
36 key A up
37 key Ctrl up
38 type level two
40 key Enter down
41 key Enter up
""", "script:scripts/coin.es,@12:tabs,@20:tabs,@46:tabs")
    tabs = [l.split("tabs: ", 1)[1] for l in log.splitlines() if "tabs: " in l]
    if not tabs or tabs[0] != "scripts/coin.es":
        return "the script didn't open: %s" % tabs
    if "scripts/coin.es" in tabs[2:]:
        return "Ctrl+W didn't close the script tab: %s" % tabs
    if tabs[-1] != "scene scenes/level two.scene" or not os.path.exists(os.path.join(project, "scenes", "level two.scene")):
        return "Ctrl+Shift+S didn't save the scene as 'level two': %s" % tabs


def test_welcome_tour(editor):
    """The welcome tour: name, picture and how they found Aven are kept; "never coded" skips the
    engines step and starts at Starter; "some" plus Godot starts at Creator with Godot's keys."""
    def tour(coding_y, extra):
        lines = """
10 type Robin
12 move 661 392
13 down
14 up
16 key Enter down
17 key Enter up
20 move 1134 320
21 down
22 up
24 key Enter down
25 key Enter up
28 move 912 %d
29 down
30 up
32 key Enter down
33 key Enter up
""" % coding_y + extra
        _, log, _ = run_editor(editor, "platformer", 60, lines, "hub,onboarding,@58:profile")
        found = [l.split("profile: ", 1)[1] for l in log.splitlines() if "profile: " in l]
        return found[-1] if found else "(no profile line)"

    new = tour(296, """
36 key Enter down
37 key Enter up
40 key Enter down
41 key Enter up
""")
    for want in ("name=Robin", "avatar=fox", "found=GitHub", "coding=0", "level=1", "keymap=Aven", "onboarded=1", "tour=closed"):
        if want not in new:
            return "new to code: expected %s in: %s" % (want, new)
    pro = tour(418, """
36 move 829 312
37 down
38 up
40 key Enter down
41 key Enter up
44 key Enter down
45 key Enter up
48 key Enter down
49 key Enter up
""")
    for want in ("coding=2", "level=3", "engines=Godot", "keymap=Godot", "onboarded=1", "tour=closed"):
        if want not in pro:
            return "some code, Godot: expected %s in: %s" % (want, pro)


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
    try:
        for name, fn in tests:
            problem = fn(editor)
            print(("ok   " if problem is None else "FAIL ") + name[5:] + ("" if problem is None else ": " + problem), flush=True)
            failed += problem is not None
    finally:
        if not os.environ.get("AVEN_KEEP_TEST_FILES"):
            for path in _made:
                shutil.rmtree(path, ignore_errors=True)
    print("%d/%d editor tests passed" % (len(tests) - failed, len(tests)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
