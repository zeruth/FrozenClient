import re, struct, sys
# Maps each WowClientDB instance in the reference to its DBC filename: the static constructors
# store a per-type vtable into the instance, and one of that vtable's functions names the file.
sys.argv = ["x"]
exec(open(r"C:/Users/tyler/AppData/Local/Temp/claude/C--Users-tyler-runicworld-client/985e7361-7e55-414f-9b09-af64af7b56ec/scratchpad/dumpqtp.py").read().split('print("functions')[0])
ASM = r"C:/Users/tyler/AppData/Local/Temp/claude/C--Users-tyler-runicworld-client/985e7361-7e55-414f-9b09-af64af7b56ec/scratchpad/wow_text.asm"

lines = []
index = {}
for line in open(ASM, encoding="latin1"):
    m = re.match(r"^\s+([0-9a-f]+):\s*(.*)$", line)
    if m:
        index[int(m.group(1), 16)] = len(lines)
        lines.append((int(m.group(1), 16), m.group(2)))

def filename_of(func, depth=0):
    i = index.get(func)
    if i is None:
        return None
    for k in range(i, min(i + 60, len(lines))):
        va, text = lines[k]
        for h in re.findall(r"\$0x([0-9a-f]{6,8})", text):
            s = cstr(int(h, 16))
            if s and "DBFilesClient" in s:
                return s
        if text.startswith("ret"):
            break
        m = re.match(r"calll\s+0x([0-9a-f]+)", text)
        if m and depth < 1:
            r = filename_of(int(m.group(1), 16), depth + 1)
            if r:
                return r
    return None

out = {}
for va, text in lines:
    if not (0x9d0000 <= va < 0x9e0000):
        continue
    m = re.match(r"movl\s+\$0x([0-9a-f]+), 0x(ad[0-9a-f]{4})\b", text)
    if not m:
        continue
    vt, obj = int(m.group(1), 16), int(m.group(2), 16)
    if vt == 0xa94408 or obj in out:
        continue
    name = None
    for slot in range(12):
        if at(vt + slot * 4) is None:
            break
        f = u32(vt + slot * 4)
        if not (0x401000 <= f < 0x9e0000):
            break
        name = filename_of(f)
        if name:
            break
    if name:
        out[obj] = name
for obj in sorted(out):
    print("%x %s" % (obj, out[obj].split("\\")[-1]))
