#!/usr/bin/env python3
r"""One command for a full two-client comparison run, with the safety rules built in.

Assembling this sequence by hand each time is how a run gets wasted: the reference comes up
fullscreen because Config.wtf lost gxWindow again, or a stale Frozen is still holding the port, or the
screenshot never gets converted and there is nothing to look at afterwards. The desktop is rarely
free, so when it is, the run has to work the first time.

    python tools/scene-run.py                       # both clients, compare, screenshot, clean up
    python tools/scene-run.py --keep                # leave both running afterwards
    python tools/scene-run.py --character Scenedk   # which character our client logs in as

What it does, in order:

 1. refuses to start while the user's own game is running;
 2. re-adds SET gxWindow "1" to the reference's Config.wtf (it drops it on every exit, and BOTH
    clients read that file);
 3. kills leftover clients -- by full path, never by process name;
 4. brings the reference to the world and waits on its own clock rather than a fixed sleep;
 5. launches ours with auto-login and a one-shot screenshot;
 6. runs memcompare against both at the same instant;
 7. converts the screenshot to PNG so it can actually be viewed;
 8. shuts both down unless --keep.
"""

import argparse
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import memcompare

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
REFERENCE_DIR = os.path.dirname(memcompare.REFERENCE_EXE)
CONFIG = os.path.join(REFERENCE_DIR, "WTF", "Config.wtf")

# The user plays on this machine. These two being up is the documented reason to refuse.
USER_GAME = ("RunicWorldGame.exe", "RunicWorldLauncher.exe")


def running_processes():
    out = subprocess.run(["powershell", "-c",
                          "Get-Process | Select-Object -ExpandProperty ProcessName"],
                         capture_output=True, text=True).stdout

    return set(line.strip().lower() for line in out.splitlines() if line.strip())


def user_is_playing():
    names = running_processes()

    return [g for g in USER_GAME if os.path.splitext(g)[0].lower() in names]


def kill_by_path(*paths):
    """Kill only processes whose ExecutablePath matches, never by name.

    The user's own game and the reference are both plausible "WoW.exe" matches, so
    Stop-Process -Name WoW would end their session.
    """
    wanted = set(os.path.normcase(os.path.abspath(p)) for p in paths)
    script = ("Get-CimInstance Win32_Process | "
              "Where-Object { $_.ExecutablePath } | "
              "Select-Object ProcessId, ExecutablePath | ConvertTo-Csv -NoTypeInformation")
    out = subprocess.run(["powershell", "-c", script], capture_output=True, text=True).stdout

    killed = []

    for line in out.splitlines()[1:]:
        parts = line.strip().strip('"').split('","')

        if len(parts) != 2:
            continue

        pid, path = parts

        if os.path.normcase(os.path.abspath(path)) in wanted:
            subprocess.run(["powershell", "-c", "Stop-Process -Id %s -Force" % pid],
                           capture_output=True)
            killed.append((pid, path))

    return killed


def ensure_windowed():
    """The reference rewrites Config.wtf on exit and drops gxWindow every single time."""
    try:
        with open(CONFIG, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return False

    if "gxWindow" in text:
        return True

    with open(CONFIG, "a", encoding="utf-8") as fh:
        fh.write('SET gxWindow "1"\n')

    print("  re-added gxWindow to Config.wtf")

    return True


def wait_for_world(label, exe, symbol_reader, timeout):
    """Poll until the client reports it is in the world, rather than sleeping a fixed time."""
    deadline = time.time() + timeout

    while time.time() < deadline:
        time.sleep(5)
        target = memcompare.attach(label, exe)

        if not target:
            continue

        value = symbol_reader(target)

        if value is not None:
            print("  %s is in the world (%s)" % (label, value))
            return True

    return False


def reference_in_world(target):
    held = target.read(target.base + 0x00d38b00 - 0x400000, 4)

    if not held:
        return None

    minutes = int.from_bytes(held, "little")

    return "clock %d" % minutes if 0 < minutes < 1440 else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--account", default="SCENE")
    ap.add_argument("--password", default="SCENE")
    ap.add_argument("--character", default="Scenedk")
    ap.add_argument("--ref-account", default="TEST")
    ap.add_argument("--ref-password", default="TEST")
    ap.add_argument("--timeout", type=int, default=180)
    ap.add_argument("--keep", action="store_true", help="leave both clients running")
    ap.add_argument("--shot", default=os.path.join(ROOT, "build", "scene-run.tga"))
    args = ap.parse_args()

    playing = user_is_playing()

    if playing:
        sys.exit("refusing to launch: %s is running. The reference takes the screen; wait until "
                 "the desktop is free." % ", ".join(playing))

    print("[1/7] desktop is free")

    if not ensure_windowed():
        sys.exit("could not read %s" % CONFIG)

    print("[2/7] gxWindow present")

    killed = kill_by_path(memcompare.REFERENCE_EXE, memcompare.FROZEN_EXE)
    print("[3/7] cleared %d leftover client(s)" % len(killed))

    if killed:
        time.sleep(6)

    print("[4/7] bringing the reference to the world ...")
    relog = subprocess.run([sys.executable, os.path.join(TOOLS, "relog-reference.py"),
                            "--account", args.ref_account, "--password", args.ref_password,
                            "--timeout", str(args.timeout)],
                           capture_output=True, text=True)

    if relog.returncode != 0:
        sys.exit("the reference never reached the world:\n%s" % (relog.stdout + relog.stderr))

    print("  %s" % relog.stdout.strip().splitlines()[-1])

    shot = os.path.abspath(args.shot)
    os.makedirs(os.path.dirname(shot), exist_ok=True)

    if os.path.exists(shot):
        os.remove(shot)

    env = dict(os.environ)
    env["FROZEN_AUTO_LOGIN"] = "%s:%s" % (args.account, args.password)
    env["FROZEN_AUTO_CHARACTER"] = args.character
    env["FROZEN_SCREENSHOT"] = shot

    print("[5/7] launching ours as %s ..." % args.character)
    log = open(os.path.join(ROOT, "build", "scene-run.log"), "wb")
    subprocess.Popen([memcompare.FROZEN_EXE], cwd=REFERENCE_DIR, env=env, stdout=log, stderr=log)

    symbols = memcompare.load_frozen_symbols()

    def ours_in_world(target):
        info = symbols.get("CGGameUI::s_inWorld")

        if not info:
            return None

        held = target.read(target.base + info[0], 1)

        return "in world" if held and held[0] else None

    if not wait_for_world("frozen", memcompare.FROZEN_EXE, ours_in_world, args.timeout):
        print("  WARNING: ours never reported in-world; comparing anyway")

    # The screenshot fires a few hundred frames in; give it room to land.
    time.sleep(20)

    print("[6/7] comparing ...")
    compare = subprocess.run([sys.executable, os.path.join(TOOLS, "memcompare.py")],
                             capture_output=True, text=True)
    report = compare.stdout
    print("\n".join(line for line in report.splitlines()
                    if "clock:" in line or line.strip().startswith("ok:")))

    ok = sum(1 for l in report.splitlines() if l.rstrip().endswith("ok"))
    diff = sum(1 for l in report.splitlines() if l.rstrip().endswith("DIFF"))
    skew = sum(1 for l in report.splitlines() if l.rstrip().endswith("SKEW"))
    print("  ok: %d   DIFF: %d   SKEW: %d" % (ok, diff, skew))

    with open(os.path.join(ROOT, "build", "scene-run-compare.txt"), "w", encoding="utf-8") as fh:
        fh.write(report)

    print("[7/7] screenshot ...")

    if os.path.exists(shot):
        png = os.path.splitext(shot)[0] + ".png"
        convert = subprocess.run([sys.executable, os.path.join(TOOLS, "tga2png.py"), shot, png],
                                 capture_output=True, text=True)
        print("  %s" % (convert.stdout.strip() or convert.stderr.strip()))
    else:
        print("  NO SCREENSHOT at %s -- the capture did not fire" % shot)

    stubs = set()

    try:
        with open(os.path.join(ROOT, "build", "scene-run.log"), "r",
                  encoding="utf-8", errors="replace") as fh:
            for line in fh:
                if "Function not yet implemented" in line:
                    stubs.add(line.split(":", 1)[1].strip())
    except OSError:
        pass

    print("  stubs hit this run: %d unique" % len(stubs))

    if not args.keep:
        kill_by_path(memcompare.REFERENCE_EXE, memcompare.FROZEN_EXE)
        print("both clients shut down")
    else:
        print("both clients left running (--keep)")


if __name__ == "__main__":
    main()
