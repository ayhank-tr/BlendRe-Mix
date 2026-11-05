"""BGE-Remix: a Blender run under Windows' console debugger (cdb), and its
crash stack in plain function names (from the build's blender.map).

Used by the crash check (run_smoke.py) and the launcher for testing by hand
(blender_remix.py). Plain Python 3, no extra packages.
"""

import bisect
import os
import re
import subprocess

CDB = r"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe"
HERE = os.path.dirname(os.path.abspath(__file__))
# what the debugger does: on a crash (access violation, heap corruption, a
# stack buffer overrun) it writes where, the registers, the stack and the
# modules, then ends the program
CDB_SCRIPT = ('sxe -c ".echo BGE-Remix crash; .lastevent; r; kn 80; lm; q" av\n'
              'sxe -c ".echo BGE-Remix crash; .lastevent; r; kn 80; lm; q" c0000374\n'
              'sxe -c ".echo BGE-Remix crash; .lastevent; r; kn 80; lm; q" c0000409\n'
              'g\n')
PREFERRED_BASE = 0x140000000


def debugger():
    return CDB if os.path.isfile(CDB) else None


def command(exe, args, debugger_log, script_path):
    """the command line: under the debugger when it is installed"""
    cdb = debugger()
    if not cdb:
        return [exe] + list(args)
    with open(script_path, "w") as f:
        f.write(CDB_SCRIPT)
    return [cdb, "-G", "-sins", "-logo", debugger_log, "-cf", script_path, exe] + list(args)


def run(cmd, env, log, timeout):
    full = dict(os.environ)
    full.update({k: v for k, v in env.items() if v is not None})
    try:
        p = subprocess.run(cmd, env=full, timeout=timeout, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        code, out = p.returncode, p.stdout
    except subprocess.TimeoutExpired as ex:
        code, out = "timeout", ex.stdout or b""
    with open(log, "wb") as f:
        f.write(out or b"")
    return code, (out or b"").decode("utf-8", "replace")


class MapFile:
    """blender.map's functions (the linker's map: address -> name)"""

    def __init__(self, path):
        self.addrs, self.syms = [], []
        rows = []
        with open(path, errors="replace") as f:
            for line in f:
                p = line.split()
                if len(p) >= 4 and re.match(r"^[0-9a-f]{4}:[0-9a-f]{8}$", p[0]) and \
                        re.match(r"^[0-9a-f]{16}$", p[2]):
                    rows.append((int(p[2], 16), p[1], p[-1]))
        rows.sort()
        self.addrs = [r[0] for r in rows]
        self.syms = rows

    def name(self, rva):
        i = bisect.bisect_right(self.addrs, PREFERRED_BASE + rva) - 1
        if i < 0:
            return "?"
        a, n, obj = self.syms[i]
        n = demangle(n)
        return "%s+0x%x (%s)" % (n, PREFERRED_BASE + rva - a, obj.split(":")[-1])


def demangle(n):
    """enough of MSVC's C++ names to read them: ?name@scope@...@@..."""
    if not n.startswith("?"):
        return n
    m = re.match(r"^\?+([^@]+)@([^@]*@)*?@", n)
    if not m:
        return n
    parts = re.match(r"^\?+(.*?)@@", n)
    if not parts:
        return n
    bits = [b for b in parts.group(1).split("@") if b and not b.startswith("?A0x")]
    return "::".join(reversed(bits))


def crash_report(debugger_log, map_path, frames=25):
    """{"crashed", "event", "fault", "stack": [...]} from the debugger's log"""
    r = {"crashed": False}
    try:
        text = open(debugger_log, errors="replace").read()
    except OSError:
        return r
    i = text.find("BGE-Remix crash")
    if i < 0:
        return r
    r["crashed"] = True
    tail = text[i:]
    m = re.search(r"Last event: .*?: (.*)", tail)
    r["event"] = m.group(1).strip() if m else None
    base = None
    m = re.search(r"^([0-9a-f`]{17}) ([0-9a-f`]{17})\s+blender\s", tail, re.M)
    if m:
        base = int(m.group(1).replace("`", ""), 16)
    mapf = MapFile(map_path) if (base and map_path and os.path.isfile(map_path)) else None

    def where(addr):
        if mapf and base <= addr < base + 0x10000000:
            return mapf.name(addr - base)
        return None
    m = re.search(r"^rip=([0-9a-f]{16})", tail, re.M)
    if m:
        rip = int(m.group(1), 16)
        r["fault"] = where(rip) or hex(rip)
    stack = []
    for fm in re.finditer(r"^([0-9a-f]{2}) ([0-9a-f`]{17}) ([0-9a-f`]{17})\s+(\S+)", tail, re.M):
        ret = int(fm.group(3).replace("`", ""), 16)
        stack.append(where(ret) or fm.group(4))
        if len(stack) >= frames:
            break
    r["stack"] = stack
    return r


def report_lines(rep):
    if not rep.get("crashed"):
        return []
    lines = ["CRASH: %s" % rep.get("event"), "  in   %s" % rep.get("fault")]
    lines += ["  from %s" % s for s in rep.get("stack") or []]
    return lines
