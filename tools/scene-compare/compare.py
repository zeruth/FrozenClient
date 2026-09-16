#!/usr/bin/env python
"""Scene comparison harness: reference WoW.exe vs Whoa.exe, same viewpoint, pixel diff.

For each viewpoint the script teleports the harness character in the AzerothCore database, launches
a client from the reference data directory, drives the login (password + Enter, then Enter at the
character select), waits for the world to load, captures the window's client area, and kills the
client. It does this for both clients and then diffs the two captures.

SAFETY. The user games on this machine. The harness therefore:
  * refuses to run while RunicWorldGame.exe / RunicWorldLauncher.exe is running (override: --force),
  * never calls SetForegroundWindow or otherwise takes focus; keystrokes are posted to the window,
  * kills a client immediately if it comes up without a normal window frame (fullscreen exclusive),
  * is meant to be run by hand when the desktop is free -- never from an unattended loop.

Only pure-python + Pillow + ctypes are needed.

Typical use (from the repo root, servers already running):

    python tools/scene-compare/compare.py                  # every viewpoint, both clients
    python tools/scene-compare/compare.py --only whoa      # just re-capture whoa, reuse ref
    python tools/scene-compare/compare.py --view goldshire # one viewpoint

Output goes to build/scene-compare/<viewpoint>/{ref,whoa,diff,side-by-side}.png plus report.md.
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import json
import os
import subprocess
import time

from PIL import Image, ImageChops, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

DEFAULTS = {
    "ref_dir": os.path.join(REPO, ".reference", "WOTLK 3.3.5a - Windows", "WoW_WOTLK_3.3.5a"),
    "ref_exe": "WoW.exe",
    "whoa_exe": os.path.join(REPO, "build", "dist", "bin", "Whoa.exe"),
    "mysql": r"C:\Program Files\MySQL\MySQL Server 9.6\bin\mysql.exe",
    "mysql_user": "acore",
    "mysql_pass": "acore",
    "account": "SCENE",   # owns exactly one character, so both clients enter with the same one
    "password": "SCENE",
    "viewpoints": os.path.join(HERE, "viewpoints.json"),
    "out": os.path.join(REPO, "build", "scene-compare"),
}

# Processes that mean the user is playing; the harness must not put windows up over them.
USER_GAME_PROCESSES = ("RunicWorldGame.exe", "RunicWorldLauncher.exe")

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_CHAR = 0x0102
VK_RETURN = 0x0D
PW_RENDERFULLCONTENT = 0x00000002
GWL_STYLE = -16
WS_CAPTION = 0x00C00000


# ---------------------------------------------------------------------------------------------
# Win32 helpers
# ---------------------------------------------------------------------------------------------

def running_processes():
    out = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
    return {line.split('","')[0].strip('"').lower() for line in out.splitlines() if line.startswith('"')}


def find_window(pid, title="World of Warcraft", timeout=60):
    """Top-level visible window owned by pid whose title matches."""
    EnumWindowsProc = ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    deadline = time.time() + timeout

    while time.time() < deadline:
        found = []

        def cb(hwnd, _):
            if not user32.IsWindowVisible(hwnd):
                return True
            wpid = wt.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
            if wpid.value != pid:
                return True
            buf = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(hwnd, buf, 256)
            if title in buf.value:
                found.append(hwnd)
                return False
            return True

        user32.EnumWindows(EnumWindowsProc(cb), 0)
        if found:
            return found[0]
        time.sleep(0.5)

    return None


def client_rect(hwnd):
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    return r.right - r.left, r.bottom - r.top


def is_framed_window(hwnd):
    """A normal windowed client has a caption; a fullscreen-exclusive one does not."""
    style = user32.GetWindowLongW(hwnd, GWL_STYLE)
    return (style & WS_CAPTION) == WS_CAPTION


def capture(hwnd):
    """PrintWindow the client area into a PIL image. No focus change, ever."""
    w, h = client_rect(hwnd)
    if w <= 0 or h <= 0:
        return None

    hdc = user32.GetDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    ok = user32.PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT | 1)  # 1 = PW_CLIENTONLY

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
                    ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                    ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG),
                    ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD)]

    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.biWidth = w
    bi.biHeight = -h  # top-down
    bi.biPlanes = 1
    bi.biBitCount = 32
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bi), 0)

    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)

    img = Image.frombuffer("RGBA", (w, h), buf.raw, "raw", "BGRA", 0, 1).convert("RGB")

    if not ok or max(hi for _, hi in img.getextrema()) < 8:
        # A black frame from PrintWindow means the client is not drawing into a composited window
        # (fullscreen exclusive, or a device the DWM cannot read). We deliberately do NOT fall back
        # to a screen grab: that needs the window in front, and taking focus is off limits here.
        return None

    return img


def post_text(hwnd, text):
    for ch in text:
        user32.PostMessageW(hwnd, WM_CHAR, ord(ch), 0)
        time.sleep(0.03)


def post_enter(hwnd):
    user32.PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0x001C0001)
    user32.PostMessageW(hwnd, WM_CHAR, 0x0D, 0x001C0001)
    user32.PostMessageW(hwnd, WM_KEYUP, VK_RETURN, 0xC01C0001)


# ---------------------------------------------------------------------------------------------
# Server side
# ---------------------------------------------------------------------------------------------

def mysql(opts, sql):
    cmd = [opts.mysql, "-u" + opts.mysql_user, "-p" + opts.mysql_pass, "-N", "-e", sql]
    r = subprocess.run(cmd, capture_output=True, text=True)
    err = "\n".join(l for l in r.stderr.splitlines() if "Warning" not in l)
    if r.returncode != 0:
        raise SystemExit("mysql failed: " + err)
    return r.stdout.strip()


def account_id(opts):
    aid = mysql(opts, "SELECT id FROM acore_auth.account WHERE username='%s'" % opts.account)
    if not aid:
        raise SystemExit("account %s does not exist in acore_auth" % opts.account)
    return aid


def wait_offline(opts, timeout=90):
    """The server writes the character (position included) when it finishes logging it out, a few
    seconds after the socket drops. A teleport written before that save lands is overwritten by it."""
    aid = account_id(opts)
    deadline = time.time() + timeout

    while time.time() < deadline:
        online = mysql(opts, "SELECT COUNT(*) FROM acore_characters.characters WHERE account=%s AND online=1" % aid)
        if online == "0":
            return aid
        time.sleep(2)

    raise RuntimeError("character still online after %ds; is a client still logged in?" % timeout)


def teleport(opts, vp):
    aid = wait_offline(opts)
    mysql(opts, (
        "UPDATE acore_characters.characters SET map={map}, instance_id=0, position_x={x}, position_y={y}, "
        "position_z={z}, orientation={o}, taxi_path='', trans_x=0, trans_y=0, trans_z=0, trans_o=0, transguid=0 "
        "WHERE account={account};"
    ).format(map=vp["map"], x=vp["x"], y=vp["y"], z=vp["z"], o=vp.get("o", 0.0), account=aid))


# ---------------------------------------------------------------------------------------------
# Config.wtf
# ---------------------------------------------------------------------------------------------

def prepare_config(config_path, account):
    """Point both clients at the harness account and force windowed mode for the run."""
    with open(config_path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()

    keep = ("SET accountName ", "SET gxWindow ", "SET gxMaximize ")
    lines = [l for l in lines if not l.startswith(keep)]
    lines.append('SET accountName "%s"' % account)
    lines.append('SET gxWindow "1"')
    lines.append('SET gxMaximize "0"')

    with open(config_path, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")


# ---------------------------------------------------------------------------------------------
# Client run
# ---------------------------------------------------------------------------------------------

def run_client(opts, exe, cwd, label, out_png):
    log = open(os.path.join(os.path.dirname(out_png), label + ".log"), "w")
    print("  launching %s" % exe)
    proc = subprocess.Popen([exe], cwd=cwd, stdout=log, stderr=subprocess.STDOUT)

    try:
        hwnd = find_window(proc.pid, timeout=opts.wait_window)
        if not hwnd:
            raise RuntimeError("no game window appeared for " + label)

        # Never leave a fullscreen-exclusive client up: it takes over the whole display.
        if not is_framed_window(hwnd):
            raise RuntimeError("%s came up without a window frame (fullscreen?); killed it. Check gxWindow in Config.wtf" % label)

        w, h = client_rect(hwnd)
        print("  window %dx%d, waiting %ds for the login screen" % (w, h, opts.wait_login))
        time.sleep(opts.wait_login)

        # Account name is prefilled from Config.wtf, so the password box has focus.
        post_text(hwnd, opts.password)
        time.sleep(0.2)
        post_enter(hwnd)

        print("  waiting %ds for the character select" % opts.wait_charselect)
        time.sleep(opts.wait_charselect)
        post_enter(hwnd)  # Enter World with the only character on the account

        print("  waiting %ds for the world" % opts.wait_world)
        time.sleep(opts.wait_world)

        code = proc.poll()
        if code is not None:
            raise RuntimeError("%s exited with code %d before the capture (crash?)" % (label, code))

        # The client may have recreated its window on the way into the world (device reset), so
        # re-resolve the handle by pid instead of trusting the one from the login screen.
        if not user32.IsWindow(hwnd) or client_rect(hwnd) == (0, 0):
            hwnd = find_window(proc.pid, timeout=10) or hwnd

        if not is_framed_window(hwnd):
            raise RuntimeError("%s switched to a frameless (fullscreen?) window on world entry; killed it" % label)

        w, h = client_rect(hwnd)
        if w <= 0 or h <= 0:
            raise RuntimeError("%s window has an empty client area (minimised?)" % label)

        img = capture(hwnd)
        if img is None:
            raise RuntimeError("%s gave a black frame to PrintWindow (not a composited window)" % label)
        img.save(out_png)
        print("  saved %s (%dx%d)" % (out_png, img.width, img.height))
        return img
    finally:
        proc.kill()
        proc.wait()
        log.close()


# ---------------------------------------------------------------------------------------------
# Diff
# ---------------------------------------------------------------------------------------------

def diff_images(ref, whoa, out_dir, threshold):
    note = ""
    if ref.size != whoa.size:
        note = "size mismatch ref %s whoa %s (whoa resized for the diff)" % (ref.size, whoa.size)
        whoa = whoa.resize(ref.size, Image.BILINEAR)

    d = ImageChops.difference(ref, whoa).convert("L")
    hist = d.histogram()
    total = sum(hist)
    bad = sum(hist[threshold + 1:])
    match = 100.0 * (total - bad) / total

    heat = Image.merge("RGB", (d.point(lambda v: min(255, v * 3)), d.point(lambda v: 0), d.point(lambda v: 0)))
    heat.save(os.path.join(out_dir, "diff.png"))

    side = Image.new("RGB", (ref.width * 3 + 20, ref.height + 24), (30, 30, 30))
    side.paste(ref, (0, 24))
    side.paste(whoa, (ref.width + 10, 24))
    side.paste(heat, (ref.width * 2 + 20, 24))
    dr = ImageDraw.Draw(side)
    dr.text((4, 4), "reference", fill=(255, 255, 255))
    dr.text((ref.width + 14, 4), "whoa", fill=(255, 255, 255))
    dr.text((ref.width * 2 + 24, 4), "diff  match %.1f%%" % match, fill=(255, 255, 255))
    side.save(os.path.join(out_dir, "side-by-side.png"))

    return match, note


# ---------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref-dir", default=DEFAULTS["ref_dir"], help="reference client install (cwd for both clients)")
    ap.add_argument("--ref-exe", default=DEFAULTS["ref_exe"], help="reference exe, relative to --ref-dir")
    ap.add_argument("--whoa-exe", default=DEFAULTS["whoa_exe"])
    ap.add_argument("--mysql", default=DEFAULTS["mysql"])
    ap.add_argument("--mysql-user", default=DEFAULTS["mysql_user"])
    ap.add_argument("--mysql-pass", default=DEFAULTS["mysql_pass"])
    ap.add_argument("--account", default=DEFAULTS["account"], help="account NAME to log in with; it should own exactly one character")
    ap.add_argument("--password", default=DEFAULTS["password"])
    ap.add_argument("--viewpoints", default=DEFAULTS["viewpoints"])
    ap.add_argument("--out", default=DEFAULTS["out"])
    ap.add_argument("--view", action="append", help="only this viewpoint name (repeatable)")
    ap.add_argument("--only", choices=["ref", "whoa"], help="capture just one client; the other side's last capture is reused for the diff")
    ap.add_argument("--no-teleport", action="store_true")
    ap.add_argument("--threshold", type=int, default=24, help="per-pixel grey difference counted as a mismatch (0-255)")
    ap.add_argument("--wait-window", type=int, default=60)
    ap.add_argument("--wait-login", type=int, default=12, help="seconds from window to login screen ready")
    ap.add_argument("--wait-charselect", type=int, default=10)
    ap.add_argument("--wait-world", type=int, default=30)
    ap.add_argument("--force", action="store_true", help="run even though the user's game is up (it will lose the screen)")
    opts = ap.parse_args()

    busy = [p for p in USER_GAME_PROCESSES if p.lower() in running_processes()]
    if busy and not opts.force:
        raise SystemExit("refusing to run: %s is running and the clients would take the screen (use --force)" % ", ".join(busy))

    with open(opts.viewpoints) as f:
        viewpoints = json.load(f)
    if opts.view:
        viewpoints = [v for v in viewpoints if v["name"] in opts.view]
        if not viewpoints:
            raise SystemExit("no viewpoint matched " + ", ".join(opts.view))

    os.makedirs(opts.out, exist_ok=True)
    rows = []

    # The reference client rewrites Config.wtf on world entry, so the original is restored byte for
    # byte at the end whatever happened in between.
    config_path = os.path.join(opts.ref_dir, "WTF", "Config.wtf")
    with open(config_path, "rb") as f:
        config_orig = f.read()

    try:
        for vp in viewpoints:
            print("== %s (map %d @ %.1f %.1f %.1f o=%.2f)" % (vp["name"], vp["map"], vp["x"], vp["y"], vp["z"], vp.get("o", 0.0)))
            vdir = os.path.join(opts.out, vp["name"])
            os.makedirs(vdir, exist_ok=True)
            ref_png = os.path.join(vdir, "ref.png")
            whoa_png = os.path.join(vdir, "whoa.png")

            failure = None
            try:
                for label, exe in (("ref", os.path.join(opts.ref_dir, opts.ref_exe)), ("whoa", opts.whoa_exe)):
                    if opts.only and opts.only != label:
                        continue
                    if not opts.no_teleport:
                        teleport(opts, vp)
                    prepare_config(config_path, opts.account)
                    run_client(opts, exe, opts.ref_dir, label, ref_png if label == "ref" else whoa_png)
            except RuntimeError as e:
                failure = str(e)
                print("  FAILED: " + failure)

            if failure:
                rows.append((vp["name"], None, failure))
            elif os.path.exists(ref_png) and os.path.exists(whoa_png):
                match, note = diff_images(Image.open(ref_png).convert("RGB"), Image.open(whoa_png).convert("RGB"), vdir, opts.threshold)
                print("  match %.1f%% %s" % (match, note))
                rows.append((vp["name"], match, note))
            else:
                rows.append((vp["name"], None, "missing capture"))
    finally:
        try:
            wait_offline(opts, timeout=60)  # let the last logout save land before the config goes back
        except (RuntimeError, SystemExit) as e:
            print("  warning: " + str(e))
        with open(config_path, "wb") as f:
            f.write(config_orig)

    report = os.path.join(opts.out, "report.md")
    with open(report, "w") as f:
        f.write("# Scene comparison\n\n")
        f.write("Generated %s. Threshold %d. Match = share of pixels whose grey difference is within the threshold.\n\n" % (time.strftime("%Y-%m-%d %H:%M"), opts.threshold))
        f.write("| Viewpoint | Match | Notes |\n|---|---|---|\n")
        for name, match, note in rows:
            f.write("| %s | %s | %s |\n" % (name, ("%.1f%%" % match) if match is not None else "-", note))
    print("report: " + report)


if __name__ == "__main__":
    main()
