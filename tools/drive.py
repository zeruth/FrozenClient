#!/usr/bin/env python3
"""Drive a client's window from the outside: find it, describe it, type into it, click it.

Both clients have to be walked through the same sequence (login, realm, character select, enter
world) before any comparison of in-world state is possible, and doing that by hand every run makes
the comparison something that happens once rather than continuously.

Input goes through PostMessage rather than SendInput wherever it can, because SendInput requires the
target window to hold focus, and stealing focus from the user is the one thing this project is not
allowed to do.

Usage:
    python tools/drive.py --list                       # every top-level window, with its process
    python tools/drive.py --exe <path> --info          # the window belonging to that image
    python tools/drive.py --exe <path> --type TEST     # type into it
    python tools/drive.py --exe <path> --key enter
    python tools/drive.py --exe <path> --click 512 400 # client-relative click
    python tools/drive.py --exe <path> --shot out.png  # capture the window
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import os
import sys
import time

user32 = ctypes.WinDLL("user32", use_last_error=True)
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

PROCESS_QUERY_LIMITED_INFORMATION = 0x1000

WM_CHAR = 0x0102
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_MOUSEMOVE = 0x0200
WM_SETFOCUS = 0x0007

VK = {
    "enter": 0x0D,
    "tab": 0x09,
    "escape": 0x1B,
    "esc": 0x1B,
    "space": 0x20,
    "backspace": 0x08,
    "up": 0x26,
    "down": 0x28,
    "left": 0x25,
    "right": 0x27,
    "f1": 0x70,
}


def process_path(pid):
    handle = k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)

    if not handle:
        return None

    try:
        size = wt.DWORD(32768)
        buf = ctypes.create_unicode_buffer(size.value)

        if not k32.QueryFullProcessImageNameW(wt.HANDLE(handle), 0, buf, ctypes.byref(size)):
            return None

        return buf.value
    finally:
        k32.CloseHandle(wt.HANDLE(handle))


def enum_windows():
    """Every visible top-level window as (hwnd, pid, title, path)."""
    out = []
    proto = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    def callback(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True

        length = user32.GetWindowTextLengthW(hwnd)
        buf = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buf, length + 1)

        pid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        out.append((hwnd, pid.value, buf.value, process_path(pid.value)))
        return True

    user32.EnumWindows(proto(callback), 0)
    return out


def window_for(exe_path):
    """The largest visible window owned by this image, which is the render window.

    Largest rather than first: these clients also create small helper windows, and picking by
    z-order or creation order got the wrong one.
    """
    want = os.path.normcase(os.path.abspath(exe_path))
    best = None
    best_area = -1

    for hwnd, pid, title, path in enum_windows():
        if not path or os.path.normcase(path) != want:
            continue

        rect = wt.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(rect))
        area = (rect.right - rect.left) * (rect.bottom - rect.top)

        if area > best_area:
            best, best_area = (hwnd, pid, title, rect), area

    return best


def type_text(hwnd, text, delay=0.03):
    for ch in text:
        user32.PostMessageW(hwnd, WM_CHAR, ord(ch), 0)
        time.sleep(delay)


def press(hwnd, name, delay=0.05):
    code = VK.get(name.lower())

    if code is None:
        sys.exit("unknown key: %s" % name)

    user32.PostMessageW(hwnd, WM_KEYDOWN, code, 0)
    time.sleep(delay)
    user32.PostMessageW(hwnd, WM_KEYUP, code, 0)

    # The glue screens act on WM_CHAR for Enter in some paths and on the key event in others, so
    # send both rather than guess which screen is up.
    if code in (0x0D, 0x09, 0x20, 0x08):
        user32.PostMessageW(hwnd, WM_CHAR, code, 0)


def click(hwnd, x, y, delay=0.05):
    lparam = (y << 16) | (x & 0xFFFF)
    user32.PostMessageW(hwnd, WM_MOUSEMOVE, 0, lparam)
    time.sleep(delay)
    user32.PostMessageW(hwnd, WM_LBUTTONDOWN, 1, lparam)
    time.sleep(delay)
    user32.PostMessageW(hwnd, WM_LBUTTONUP, 0, lparam)


def screenshot(hwnd, path):
    """PrintWindow the target into a PNG, without raising or focusing it."""
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    width = rect.right - rect.left
    height = rect.bottom - rect.top

    if width <= 0 or height <= 0:
        return False

    hdc = user32.GetDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bitmap = gdi32.CreateCompatibleBitmap(hdc, width, height)
    gdi32.SelectObject(mem, bitmap)

    # 2 = PW_RENDERFULLCONTENT, which is what makes this work on a hardware-accelerated window.
    user32.PrintWindow(hwnd, mem, 2)

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", ctypes.c_long), ("biHeight", ctypes.c_long),
                    ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                    ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                    ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wt.DWORD),
                    ("biClrImportant", wt.DWORD)]

    header = BITMAPINFOHEADER()
    header.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    header.biWidth = width
    header.biHeight = -height  # top-down
    header.biPlanes = 1
    header.biBitCount = 32
    header.biCompression = 0

    buf = (ctypes.c_char * (width * height * 4))()
    gdi32.GetDIBits(mem, bitmap, 0, height, buf, ctypes.byref(header), 0)

    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)

    write_png(path, width, height, bytes(buf))
    return True


def write_png(path, width, height, bgra):
    """Minimal PNG writer, so this does not need Pillow installed."""
    import struct
    import zlib

    rows = bytearray()

    for y in range(height):
        rows.append(0)
        row = bgra[y * width * 4:(y + 1) * width * 4]

        for x in range(width):
            b, g, r = row[x * 4], row[x * 4 + 1], row[x * 4 + 2]
            rows += bytes((r, g, b))

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 6))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--exe")
    ap.add_argument("--info", action="store_true")
    ap.add_argument("--type")
    ap.add_argument("--key")
    ap.add_argument("--click", nargs=2, type=int, metavar=("X", "Y"))
    ap.add_argument("--shot")
    args = ap.parse_args()

    if args.list:
        for hwnd, pid, title, path in enum_windows():
            if not title:
                continue

            print("hwnd 0x%08X pid %-6d %-40s %s" % (hwnd, pid, title[:40], path or "?"))

        return

    if not args.exe:
        sys.exit("need --exe (or --list)")

    found = window_for(args.exe)

    if not found:
        sys.exit("no visible window for %s" % args.exe)

    hwnd, pid, title, rect = found

    if args.info or not (args.type or args.key or args.click or args.shot):
        print("hwnd 0x%08X pid %d  %dx%d at (%d,%d)  title %r"
              % (hwnd, pid, rect.right - rect.left, rect.bottom - rect.top,
                 rect.left, rect.top, title))

    if args.type:
        type_text(hwnd, args.type)

    if args.key:
        press(hwnd, args.key)

    if args.click:
        click(hwnd, args.click[0], args.click[1])

    if args.shot:
        ok = screenshot(hwnd, args.shot)
        print("%s %s" % ("wrote" if ok else "FAILED to write", args.shot))


if __name__ == "__main__":
    main()
