#!/usr/bin/env python3
"""Restart the reference client and walk it back into the world.

The reference's day/night clock runs slow and never re-syncs while it stays logged in, so a session
left up for an hour drifts tens of minutes away from the server. Every light value is a function of
time of day, which makes every colour comparison against a drifted reference meaningless -- and a
comparison that silently proves nothing is worse than no comparison. Re-log before any run whose
numbers matter.

Doing it by hand is five steps that are easy to skip, which is exactly why they got skipped
repeatedly. This is those five steps.

    python tools/relog-reference.py [--account TEST] [--password TEST]
"""

import argparse
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import memcompare

REFERENCE_DIR = os.path.dirname(memcompare.REFERENCE_EXE)
CONFIG = os.path.join(REFERENCE_DIR, "WTF", "Config.wtf")


def ensure_windowed():
    """Re-add gxWindow. The reference rewrites Config.wtf on exit and drops it every single time,
    and without it the client comes up fullscreen over whatever the user is doing."""
    try:
        with open(CONFIG, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError:
        return False

    if "gxWindow" in text:
        return True

    with open(CONFIG, "a", encoding="utf-8") as f:
        f.write('SET gxWindow "1"\n')

    print("re-added gxWindow to Config.wtf")

    return True


def drive(*args):
    subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), "drive.py"),
                    "--exe", memcompare.REFERENCE_EXE] + list(args),
                   capture_output=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--account", default="TEST")
    ap.add_argument("--password", default="TEST")
    ap.add_argument("--timeout", type=int, default=180)
    args = ap.parse_args()

    # By full path, never by name: the user's own game is also a "WoW.exe", and
    # Stop-Process -Name WoW would end their session along with the reference.
    target = os.path.normcase(os.path.abspath(memcompare.REFERENCE_EXE))
    listing = subprocess.run(
        ["powershell", "-c",
         "Get-CimInstance Win32_Process -Filter \"Name='WoW.exe'\" | "
         "Select-Object ProcessId, ExecutablePath | ConvertTo-Csv -NoTypeInformation"],
        capture_output=True, text=True).stdout

    for line in listing.splitlines()[1:]:
        parts = line.strip().strip('"').split('","')

        if len(parts) == 2 and os.path.normcase(os.path.abspath(parts[1])) == target:
            subprocess.run(["powershell", "-c", "Stop-Process -Id %s -Force" % parts[0]],
                           capture_output=True)

    time.sleep(6)

    if not ensure_windowed():
        sys.exit("could not read %s" % CONFIG)

    subprocess.Popen([memcompare.REFERENCE_EXE], cwd=REFERENCE_DIR,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("launched; waiting for the login screen ...")
    time.sleep(35)

    drive("--click", "520", "400")
    drive("--type", args.account)
    drive("--key", "tab")
    drive("--type", args.password)
    drive("--key", "enter")
    print("logging in as %s ..." % args.account)
    time.sleep(25)

    drive("--key", "enter")
    print("entering the world ...")

    # Confirmed by the clock rather than by a fixed sleep: the reference only publishes a sensible
    # game time once it is actually in the world.
    deadline = time.time() + args.timeout

    while time.time() < deadline:
        time.sleep(5)
        ref = memcompare.attach("reference", memcompare.REFERENCE_EXE)

        if not ref:
            continue

        held = ref.read(ref.base + 0x00d38b00 - 0x400000, 4)

        if not held:
            continue

        minutes = int.from_bytes(held, "little")

        if 0 < minutes < 1440:
            print("in world; reference clock reads %d minutes" % minutes)
            return

    sys.exit("the reference did not reach the world within %ds" % args.timeout)


if __name__ == "__main__":
    main()
