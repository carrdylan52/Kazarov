#!/usr/bin/env python3
"""List the game functions that touch PS2 hardware: the port's hardware-function inventory.

    hw_inventory.py scan --src ps2/src --include ps2/include --map build/pal/GAME.xMAP
                         --objects build/pal/main_o_files [--ledger docs/port/ASM_LEDGER.tsv]
                         [--out docs/port/HARDWARE_INVENTORY.md] [--json FILE]
    hw_inventory.py calibrate <scan options> --elf ELF [--overlay NAME=FILE ...] --overrides FILE

`scan` reads every function defined in the game's units, including the bodies only the PS2 build
compiles (#ifndef PORT), and gives a row to each one that:

- calls or takes the address of a symbol of an SDK unit (--objects paths under lib/sce), unless the
  ledger says the port replaces that symbol natively (adapter, sce-core+adapter, host-libc,
  kazarov-runtime, clang-rt);
- uses an EE, GS or VU register address, the scratchpad, an uncached or physical address, or writes
  a DMA, VIF or GIF tag, as a literal or through a macro or constant defined as one;
- holds inline assembly;
- reads the VU microprograms or their data (the map's .vutext and .vudata symbols);
- is static or inline and calls such a function, so only its callers can be overridden.

The rows keep the hand-written columns (trace_position, replacement_wp, state) of an existing --out,
and rows whose evidence starts with "review" are kept as they are.

`calibrate` checks the scan against a port that already exists: it reads the retail executable
(--elf, its relocations and the overlays' images) for the same hardware use in the machine code,
takes the port's PORT_OVERRIDE names (port_override.py list), and reports how many overridden
functions with hardware use the scan finds (recall) and which functions it flags that the port does
not override.
"""

import argparse
import ast
import collections
import json
import operator
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import port_override as po  # noqa: E402

KINDS = ("register", "scratchpad/uncached", "asm", "sce-call", "iop-rpc", "vu-data", "dma", "thread", "timer")
NATIVE = {"adapter", "sce-core+adapter", "host-libc", "kazarov-runtime", "clang-rt"}
HAND_COLUMNS = ("trace_position", "replacement_wp", "state")

THREAD_CALLS = re.compile(
    r"^i?(Create|Start|Exit|ExitDelete|Terminate|Delete|Sleep|Wakeup|CancelWakeup|Suspend|Resume|"
    r"RotateThreadReadyQueue|ReleaseWait|GetThreadId|ReferThreadStatus|ChangeThreadPriority|InitThread|"
    r"CreateSema|DeleteSema|SignalSema|WaitSema|PollSema|ReferSemaStatus)(Thread)?$")
TIMER_CALLS = re.compile(r"^(i?SetAlarm|i?ReleaseAlarm|sceGsSyncV\w*)$")
RPC_CALLS = re.compile(r"^(sceSif\w+|sceSd\w+|sceCd\w+)$")
DMA_CALLS = re.compile(r"^(sceDma\w+|sceVif\d\w+|sceGif\w+)$")
REGISTER_CALLS = re.compile(r"^(sceGs\w+|i?(Enable|Disable)(Intc|Dmac)|Add(Intc|Dmac)Handler2?|"
                            r"Remove(Intc|Dmac)Handler|i?FlushCache|i?SyncDCache|i?InvalidDCache)$")
ASM = re.compile(r"\b(?:asm|__asm__|__asm)\b")
HEX = re.compile(r"\b0[xX]([0-9A-Fa-f]+)[uUlL]*\b")
IDENT = re.compile(r"[A-Za-z_]\w*")
SDK_MACROS = (("SCE_VIF", "dma"), ("SCE_GIF", "dma"), ("SCE_DMA", "dma"), ("SCE_GS_", "register"))
POINTER = re.compile(r"\bvolatile\b|\*\s*\)\s*\(?\s*$")


def kind_of_call(name):
    if THREAD_CALLS.match(name):
        return "thread"
    if TIMER_CALLS.match(name):
        return "timer"
    if RPC_CALLS.match(name):
        return "iop-rpc"
    if DMA_CALLS.match(name):
        return "dma"
    if REGISTER_CALLS.match(name):
        return "register"
    return "sce-call"


def kind_of_value(value, pointer, uncached_context=False):
    """The kind of hardware a 32-bit value names, or None. `pointer`: it is used as an address."""
    top, middle = value >> 28, (value >> 16) & 0xFFF
    if 0x10000000 <= value <= 0x1000FFFF:
        if not pointer:
            return "dma"  # a DMA or GIF tag word
        offset = value & 0xFFFF
        return "timer" if offset < 0x2000 else "dma" if 0x8000 <= offset < 0xF000 else "register"
    if 0x11000000 <= value <= 0x1100FFFF:
        return "register" if pointer else "dma"
    if 0x12000000 <= value <= 0x1200FFFF:
        return "register"
    if 0x70000000 <= value <= 0x70003FFF:
        return "scratchpad/uncached"
    if value in (0x0FFFFFFF,):
        return "scratchpad/uncached" if uncached_context else "dma"
    if 2 <= top <= 3 and middle == 0:
        return "scratchpad/uncached" if pointer or uncached_context else "dma"
    if top in (1, 5, 6, 7) and middle == 0:
        return "dma"  # a DMA tag's id and count
    command = value >> 24
    if 0x60 <= command <= 0x6F or command == 0x4A:
        return "dma"  # VIF UNPACK, MPG
    if command in (0x14, 0x15, 0x17, 0x50, 0x51) and (value >> 16) & 0xFF == 0:
        return "dma"  # VIF MSCAL, MSCALF, MSCNT, DIRECT, DIRECTHL
    return None


# ---- the link map --------------------------------------------------------------------------------

MapRow = collections.namedtuple("MapRow", "group address size section name object")


def read_map(path):
    """Rows of an MWLD xMAP, with the overlay (section group) each sits in, and the groups' load
    addresses."""
    rows, loads, group = [], {}, None
    line_re = re.compile(r"^  ([0-9A-F]{8}) ([0-9A-F]{8}) (\S+)\s+(.*)\t\((.*)\)$")
    load_re = re.compile(r"^#>([0-9A-F]{8})\s+_(\w+)_load\b")
    with open(path, errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("# ."):
                group = line[3:].strip()
                continue
            load = load_re.match(line)
            if load:
                loads[load[2]] = int(load[1], 16)
                continue
            match = line_re.match(line)
            if match:
                rows.append(MapRow(group, int(match[1], 16), int(match[2], 16), match[3], match[4], match[5]))
    return rows, loads


def read_objects(paths):
    objects = []
    for path in paths:
        with open(path) as f:
            objects += f.read().split()
    return objects


def spelled(name):
    """A demangled or declared function name without its parameters."""
    depth = 0
    for i, c in enumerate(name):
        if c == "(" and depth == 0 and not name[:i].endswith("operator"):
            return name[:i].replace(" ", "")
        depth += (c == "<") - (c == ">")
    return name.replace(" ", "")


class LinkMap:
    def __init__(self, map_path, object_lists):
        self.rows, self.loads = read_map(map_path)
        objects = read_objects(object_lists)
        self.sdk_objects = {os.path.basename(p) for p in objects if "/lib/sce/" in p}
        self.game_objects = {os.path.basename(p) for p in objects
                             if "/lib/" not in p and not os.path.basename(p).startswith("crt0")}
        self.sdk = {r.name for r in self.rows if r.object in self.sdk_objects and r.section.endswith("text") and
                    IDENT.fullmatch(r.name)}
        self.vu = {r.name for r in self.rows if r.section in (".vutext", ".vudata") and IDENT.fullmatch(r.name)}
        self.linked = {spelled(r.name) for r in self.rows if r.object in self.game_objects}


def native_symbols(ledger):
    native = set()
    if not ledger:
        return native
    with open(ledger) as f:
        header = None
        for line in f:
            if line.startswith("#"):
                continue
            fields = line.rstrip("\n").split("\t")
            if header is None:
                header = fields
                continue
            row = dict(zip(header, fields))
            if row.get("replacement") in NATIVE:
                native.add(row["symbol"])
    return native


# ---- the source ----------------------------------------------------------------------------------

Function = collections.namedtuple("Function", "unit name spelled local line start end")


def definitions(text):
    """Every function defined at file scope, inside namespaces and extern "C" blocks too, with whether
    another unit can override it (local: static, inline or in an anonymous namespace)."""
    code = po.mask(text)
    flat, _, _ = po.flatten(code)
    found, scopes, i, n = [], [], 0, len(flat)
    while i < n:
        space = re.compile(r"\s+").match(flat, i)
        if space:
            i = space.end()
            continue
        if flat[i] == "}":
            if scopes:
                scopes.pop()
            i += 1
            continue
        if flat[i] == ";":
            i += 1
            continue
        j, paren = i, 0
        while j < n:
            c = flat[j]
            paren += (c == "(") - (c == ")")
            if paren == 0 and c in "{;":
                break
            j += 1
        if j >= n:
            break
        head = flat[i:j]
        scope = re.fullmatch(r"\s*(?:inline\s+)?namespace\b\s*(\w*)\s*|\s*extern\s*", head)
        if flat[j] == "{" and scope:
            scopes.append("anonymous" if head.strip() == "namespace" else "named")
            i = j + 1
            continue
        try:
            definition = po.Definition(text[i:], flat[i:])
        except po.Error:
            definition = None
        if definition is not None and "(" in head and flat[i + definition.length - 1] == "}":
            local = (definition.static or "anonymous" in scopes or
                     re.search(r"\b(?:__)?inline(?:__)?\b", head) is not None)
            name, spelled_name = definition.name, definition.spelled
            # A function returning a pointer to an array or a function: T (*name(params))[n].
            returning = re.search(r"\(\s*\*\s*((?:\w+\s*::\s*)*~?\w+)\s*\(([^()]*)\)\s*\)", head)
            if returning:
                spelled_name = re.sub(r"\s+", "", returning[1])
                name = spelled_name + "(" + ",".join(po.parameter_types(returning[2])) + ")"
            found.append(Function(None, name, spelled_name, local,
                                  text.count("\n", 0, i) + 1, i, i + definition.length))
            i += definition.length
            continue
        i = j + 1 if flat[j] == ";" else po.closing(flat, j) + 1
    return code, found


def statement_around(code, position):
    start = max(code.rfind(";", 0, position), code.rfind("{", 0, position), code.rfind("}", 0, position)) + 1
    end = min((e for e in (code.find(";", position), code.find("{", position)) if e >= 0), default=len(code))
    return code[start:end]


def evaluate(body):
    """The value of a macro body made of numbers, shifts, bit operators and casts, or None."""
    body = re.sub(r"\(\s*(?:volatile\s+)?(?:unsigned|u_int|u_long|int|long|u32|s32|uint32_t)\s*\**\s*\)", "", body)
    body = re.sub(r"\b(0[xX][0-9A-Fa-f]+|\d+)[uUlL]+\b", r"\1", body)
    if not re.fullmatch(r"[\s0-9A-Fa-fxX()<>|&+\-~*]*", body) or not re.search(r"\d", body):
        return None
    try:
        tree = ast.parse(body.strip(), mode="eval")
    except SyntaxError:
        return None
    ops = {ast.LShift: operator.lshift, ast.RShift: operator.rshift, ast.BitOr: operator.or_,
           ast.BitAnd: operator.and_, ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul}

    def value(node):
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return node.value
        if isinstance(node, ast.BinOp) and type(node.op) in ops:
            return ops[type(node.op)](value(node.left), value(node.right)) & 0xFFFFFFFF
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Invert):
            return ~value(node.operand) & 0xFFFFFFFF
        raise ValueError
    try:
        return value(tree.body)
    except (ValueError, TypeError):
        return None


class Tokens:
    """Macros and constants whose value is a hardware address or tag, by name."""

    def __init__(self):
        self.kinds = {}
        self.values = {}

    def kind(self, name, pointer):
        value, defined_as_pointer = self.values.get(name, (None, False))
        if value is None:
            return self.kinds[name]
        return kind_of_value(value, pointer or defined_as_pointer) or self.kinds[name]

    def learn(self, paths, function_spans, vu):
        defines = {}
        for path in paths:
            text = read(path)
            code = po.mask(text)
            for match in re.finditer(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)(\([^)]*\))?((?:\\\n|[^\n])*)",
                                     code, re.MULTILINE):
                defines[match[1]] = match[3].replace("\\\n", " ")
            spans = function_spans.get(path, [])
            outside = code
            for start, end in spans:
                outside = outside[:start] + " " * (end - start) + outside[end:]
            for match in re.finditer(r"\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*=\s*0[xX]([0-9A-Fa-f]+)[uUlL]*\b",
                                     outside):
                kind = kind_of_value(int(match[2], 16), False)
                if kind and match[1] not in self.kinds:
                    self.kinds[match[1]] = kind
                    self.values[match[1]] = (int(match[2], 16), False)
            # A table of the microprograms: reading it is reading them.
            for match in re.finditer(r"\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*=\s*\{([^;]*)\}\s*;", outside):
                if set(IDENT.findall(match[2])) & vu:
                    self.kinds.setdefault(match[1], "vu-data")
        for _ in range(4):
            for name, body in defines.items():
                if name in self.kinds:
                    continue
                pointer = POINTER.search(body) is not None or re.search(r"\*\s*\)", body) is not None
                value = evaluate(body)
                kind = kind_of_value(value, pointer) if value is not None else None
                if kind:
                    self.values[name] = (value, pointer)
                if kind is None:
                    for literal in HEX.finditer(body):
                        kind = kind_of_value(int(literal[1], 16), pointer)
                        if kind:
                            break
                if kind is None:
                    used = [self.kinds[t] for t in IDENT.findall(body) if t in self.kinds]
                    kind = used[0] if used else None
                if kind:
                    self.kinds[name] = kind
        for name in list(defines):
            for prefix, kind in SDK_MACROS:
                if name.startswith(prefix):
                    self.kinds.setdefault(name, kind)
        self.kinds.setdefault("MG_UNCACHED", "scratchpad/uncached")


def read(path):
    with open(path, encoding="utf-8", errors="surrogateescape") as f:
        return f.read()


def scan_function(function, code, tokens, sdk, native, vu):
    """(kind, line, what) for each piece of hardware use in a function's body."""
    body = code[function.start:function.end]
    first = function.line
    hits = []

    def line_of(offset):
        return first + body.count("\n", 0, offset)
    for match in ASM.finditer(body):
        hits.append(("asm", line_of(match.start()), "inline assembly"))
    for match in HEX.finditer(body):
        value = int(match[1], 16)
        if value > 0xFFFFFFFF:
            continue
        before = body[max(0, match.start() - 40):match.start()]
        statement = statement_around(body, match.start())
        pointer = POINTER.search(before) is not None
        kind = kind_of_value(value, pointer, re.search(r"0[xX]0?[Ff]{7}\b", statement) is not None or
                             "MG_UNCACHED" in statement)
        if kind:
            hits.append((kind, line_of(match.start()), match[0]))
    for match in IDENT.finditer(body):
        name = match[0]
        if name in tokens.kinds:
            before = body[max(0, match.start() - 40):match.start()]
            hits.append((tokens.kind(name, POINTER.search(before) is not None), line_of(match.start()), name))
        elif name in sdk and name not in native and not body[match.end():match.end() + 2].startswith("::"):
            hits.append((kind_of_call(name), line_of(match.start()), name))
        elif name in vu:
            hits.append(("vu-data", line_of(match.start()), name))
    return hits


def declared_names(directories):
    names = set()
    for directory in directories:
        for root, _, files in os.walk(directory):
            for name in files:
                if name.endswith((".h", ".hpp")):
                    names |= set(IDENT.findall(po.mask(read(os.path.join(root, name)))))
    return names


def scan(options):
    link = LinkMap(options.map, options.objects)
    # An SDK unit's own static helpers share the map with its API; only what the SDK's headers name
    # can be what the game calls.
    link.sdk &= declared_names(options.sdk_headers)
    native = native_symbols(options.ledger)
    units = []
    for src in options.src:
        for root, dirs, files in os.walk(src):
            dirs[:] = sorted(d for d in dirs if os.path.join(root, d) not in options.exclude)
            units += [os.path.join(root, f) for f in sorted(files) if f.endswith((".cpp", ".c"))]
    headers = []
    for include in options.include:
        for root, dirs, files in os.walk(include):
            dirs.sort()
            headers += [os.path.join(root, f) for f in sorted(files) if f.endswith((".h", ".hpp"))]
    parsed, spans = {}, {}
    for path in units:
        code, functions = definitions(read(path))
        unit = os.path.relpath(path, options.root)
        parsed[path] = (code, [f._replace(unit=unit) for f in functions])
        spans[path] = [(f.start, f.end) for f in functions]
    tokens = Tokens()
    tokens.learn(headers + units, spans, link.vu)

    hits = {}
    functions = []
    for path in units:
        code, found = parsed[path]
        for function in found:
            functions.append(function)
            got = scan_function(function, code, tokens, link.sdk, native, link.vu)
            if got:
                hits[(function.unit, function.name)] = got
    # Callers of a function no other unit can replace inherit its hardware use: they are what the
    # port overrides instead.
    by_unit = collections.defaultdict(list)
    for function in functions:
        by_unit[function.unit].append(function)
    changed = True
    while changed:
        changed = False
        for unit, found in by_unit.items():
            local = {f.spelled.split("::")[-1]: f for f in found if f.local and (unit, f.name) in hits}
            if not local:
                continue
            code = parsed[os.path.join(options.root, unit)][0]
            for function in found:
                body = code[function.start:function.end]
                for match in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", body):
                    callee = local.get(match[1])
                    if callee is None or callee is function:
                        continue
                    key = (unit, function.name)
                    inherited = {(k, "via " + callee.name) for k, _, _ in hits[(unit, callee.name)]}
                    known = {(k, w) for k, _, w in hits.get(key, [])}
                    new = [(k, function.line + body.count("\n", 0, match.start()), w)
                           for k, w in sorted(inherited - known)]
                    if new:
                        hits.setdefault(key, []).extend(new)
                        changed = True
    return functions, hits, link


def rows_of(functions, hits, link):
    rows = []
    for function in functions:
        got = hits.get((function.unit, function.name))
        if not got:
            continue
        kinds = [k for k in KINDS if any(h[0] == k for h in got)]
        evidence = []
        for kind in kinds:
            seen = []
            for k, line, what in got:
                if k == kind and (line, what) not in seen:
                    seen.append((line, what))
            shown = ", ".join("%d %s" % (line, what) for line, what in seen[:3])
            evidence.append("%s: %s%s" % (kind, shown, " +%d" % (len(seen) - 3) if len(seen) > 3 else ""))
        linked = function.spelled in link.linked
        rows.append({
            "unit": function.unit,
            "function": function.name,
            "kind": ", ".join(kinds),
            "evidence": "%s:%d; %s" % (function.unit, function.line, "; ".join(evidence)),
            "trace_position": "" if linked else "dead",
            "replacement_wp": "",
            "state": "" if linked else "dead(not in the link map)",
        })
    return rows


# ---- the inventory file --------------------------------------------------------------------------

COLUMNS = ("unit", "function", "kind", "evidence") + HAND_COLUMNS


def esc(value):
    return value.replace("|", "\\|")


def unesc(value):
    return value.replace("\\|", "|")


def read_inventory(path):
    rows = []
    try:
        with open(path) as f:
            lines = f.read().splitlines()
    except OSError:
        return rows
    for line in lines:
        if not line.startswith("| ") or line.startswith("| unit |"):
            continue
        cells = [unesc(c.strip()) for c in re.split(r"(?<!\\)\|", line.strip())[1:-1]]
        if len(cells) == len(COLUMNS):
            rows.append(dict(zip(COLUMNS, cells)))
    return rows


def merge(rows, previous):
    known = {(r["unit"], r["function"]): r for r in previous}
    for row in rows:
        old = known.get((row["unit"], row["function"]))
        if old:
            for column in HAND_COLUMNS:
                if old[column]:
                    row[column] = old[column]
    generated = {(r["unit"], r["function"]) for r in rows}
    rows += [r for r in previous if r["evidence"].startswith("review") and (r["unit"], r["function"]) not in generated]
    return sorted(rows, key=lambda r: (r["unit"], r["function"]))


def write_inventory(rows, path):
    counts = collections.Counter()
    for row in rows:
        for kind in row["kind"].split(", "):
            counts[kind] += 1
    lines = [
        "# Hardware inventory",
        "",
        "Generated by `%s scan`; the trace_position, replacement_wp and state columns and the rows whose" %
        os.path.basename(__file__),
        "evidence starts with \"review\" are written by hand and survive a rerun.",
        "",
        "%d functions. By kind (a function counts once per kind): %s." % (
            len(rows), ", ".join("%s %d" % (k, counts[k]) for k in KINDS if counts[k])),
        "",
        "| " + " | ".join(COLUMNS) + " |",
        "|" + "---|" * len(COLUMNS),
    ]
    for row in rows:
        lines.append("| " + " | ".join(esc(row[c]) for c in COLUMNS) + " |")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


# ---- calibration against the machine code --------------------------------------------------------

def readelf(args):
    return subprocess.run(["readelf", "-W"] + args, capture_output=True, text=True, check=True).stdout


def machine_code(elf, link, overlays):
    """address -> bytes reader per section group, and relocations per group: address -> (type, symbol)."""
    images = {}
    for line in readelf(["-S", elf]).splitlines():
        match = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+PROGBITS\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)", line)
        if match and int(match[4], 16):
            images[match[1]] = (elf, int(match[2], 16), int(match[3], 16), int(match[4], 16))
    for name, path in overlays.items():
        size = os.path.getsize(path)
        images[name] = (path, link.loads[name], 0, size)
    relocations = collections.defaultdict(dict)
    group = None
    for line in readelf(["-r", elf]).splitlines():
        section = re.match(r"Relocation section '\.rel(\w+)'", line)
        if section:
            group = section[1]
            continue
        match = re.match(r"([0-9a-f]{8})\s+[0-9a-f]+\s+(R_MIPS_\w+)\s+[0-9a-f]+\s+(.+)$", line)
        if match and group:
            relocations[group][int(match[1], 16)] = (match[2], match[3].strip())
    data = {}
    for name, (path, address, offset, size) in images.items():
        with open(path, "rb") as f:
            f.seek(offset)
            data[name] = (address, f.read(size))
    return data, relocations


def upper_use(following, register):
    """How the value a lui puts in `register` is used next: "float" (mtc1), "address" (the base of a
    load or store, possibly after an ori or addiu into the same register) or "value"."""
    for word in following:
        opcode, rs, rt = word >> 26, (word >> 21) & 31, (word >> 16) & 31
        if opcode == 0x11 and rs == 4 and rt == register:
            return "float"
        if 0x20 <= opcode <= 0x3F and opcode != 0x2F and rs == register:
            return "address"
        if opcode in (0x09, 0x0D) and rs == register and rt == register:
            continue
        if opcode == 0 and (word >> 11) & 31 == register:
            return "value"
        if 0x08 <= opcode <= 0x0F and rt == register:
            return "value"
        if 0x20 <= opcode <= 0x27 and rt == register:
            return "value"
    return "value"


def machine_hits(row, image, relocations, counted, vu):
    address, data = image
    hits = set()
    words = []
    for at in range(row.address, row.address + row.size, 4):
        offset = at - address
        if 0 <= offset <= len(data) - 4:
            words.append((at, int.from_bytes(data[offset:offset + 4], "little")))
    for index, (at, word) in enumerate(words):
        relocation = relocations.get(at)
        if relocation:
            _, symbol = relocation
            if symbol in counted:
                hits.add(kind_of_call(symbol))
            elif symbol in vu:
                hits.add("vu-data")
            continue
        opcode, rs, rt = word >> 26, (word >> 21) & 31, (word >> 16) & 31
        if opcode in (0x12, 0x32, 0x36, 0x3A, 0x3E):
            hits.add("asm")  # COP2 (VU0 macro mode), lqc2, sqc2
        elif opcode == 0x10 or opcode == 0x2F:
            hits.add("register")  # COP0, cache
        elif opcode == 0 and (word & 0x3F) in (0x0C, 0x0F):
            hits.add("asm")  # syscall, sync
        elif opcode == 0x0F and rt:
            use = upper_use([later for _, later in words[index + 1:index + 9]], rt)
            if use != "float":
                kind = kind_of_value((word & 0xFFFF) << 16, use == "address")
                if kind:
                    hits.add(kind)
    return hits


def calibrate(options):
    functions, hits, link = scan(options)
    native = native_symbols(options.ledger)
    overlays = dict(o.split("=", 1) for o in options.overlay)
    images, relocations = machine_code(options.elf, link, overlays)
    group_image = {"main": "main"}
    group_image.update({name: name for name in overlays})
    counted = {s for s in link.sdk if s not in native}
    machine = collections.defaultdict(set)
    for row in link.rows:
        if row.object not in link.game_objects or row.size == 0 or not row.section.endswith("text"):
            continue
        if row.section in (".vutext",) or row.group not in group_image:
            continue
        found = machine_hits(row, images[group_image[row.group]], relocations[row.group], counted, link.vu)
        if found:
            machine[spelled(row.name)] |= found
    with open(options.overrides) as f:
        overrides = {line.strip() for line in f if line.strip()}
    overridden = {spelled(name) for name in overrides}
    flagged = {}
    for function in functions:
        if (function.unit, function.name) in hits:
            flagged.setdefault(function.spelled, set()).update(k for k, _, _ in hits[(function.unit, function.name)])
    hardware_overrides = sorted(n for n in overridden if n in machine)
    found = [n for n in hardware_overrides if n in flagged]
    missed = [n for n in hardware_overrides if n not in flagged]
    beyond = sorted(n for n in flagged if n not in overridden and n in link.linked)
    beyond_dead = sorted(n for n in flagged if n not in overridden and n not in link.linked)
    report = {
        "overrides": len(overridden),
        "overrides_with_hardware_in_machine_code": len(hardware_overrides),
        "found": len(found),
        "missed": [(n, sorted(machine[n])) for n in missed],
        "flagged_functions": len(flagged),
        "flagged_not_overridden_linked": [(n, sorted(flagged[n])) for n in beyond],
        "flagged_not_overridden_dead": len(beyond_dead),
        "machine_not_flagged_not_overridden": sorted(n for n in machine if n not in flagged and n not in overridden),
    }
    print("recall: %d of %d overridden functions whose machine code uses hardware" % (len(found),
                                                                                    len(hardware_overrides)))
    for name, kinds in report["missed"]:
        print("  missed: %s (%s)" % (name, ", ".join(kinds)))
    print("flagged and linked but not overridden: %d (dead: %d)" % (len(beyond), len(beyond_dead)))
    if options.json:
        with open(options.json, "w") as f:
            json.dump(report, f, indent=1)
    return 0 if not missed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("scan", "calibrate"):
        command = sub.add_parser(name)
        command.add_argument("--root", default=os.getcwd())
        command.add_argument("--src", action="append", default=[], help="the game's units (default ps2/src)")
        command.add_argument("--exclude", action="append", default=[], help="a directory under --src to skip")
        command.add_argument("--include", action="append", default=[], help="headers (default ps2/include)")
        command.add_argument("--map", required=True, help="the PS2 link's xMAP")
        command.add_argument("--objects", action="append", required=True, help="the link's *_o_files lists")
        command.add_argument("--sdk-headers", action="append", default=[],
                             help="the SDK's headers (default ps2/include/sce)")
        command.add_argument("--ledger", help="ASM_LEDGER.tsv: symbols replaced natively are not hardware")
        command.add_argument("--json")
    sub.choices["scan"].add_argument("--out", default="docs/port/HARDWARE_INVENTORY.md")
    sub.choices["calibrate"].add_argument("--elf", required=True)
    sub.choices["calibrate"].add_argument("--overlay", action="append", default=[], help="NAME=FILE")
    sub.choices["calibrate"].add_argument("--overrides", required=True, help="port_override.py list output")
    options = parser.parse_args()
    options.root = os.path.realpath(options.root)
    resolve = lambda p: p if os.path.isabs(p) else os.path.join(options.root, p)  # noqa: E731
    options.src = [resolve(p) for p in options.src or ["ps2/src"]]
    options.include = [resolve(p) for p in options.include or ["ps2/include"]]
    options.exclude = [resolve(p) for p in options.exclude]
    options.sdk_headers = [resolve(p) for p in options.sdk_headers or ["ps2/include/sce"]]
    options.map = resolve(options.map)
    options.objects = [resolve(p) for p in options.objects]
    if options.command == "calibrate":
        return calibrate(options)
    functions, hits, link = scan(options)
    rows = merge(rows_of(functions, hits, link), read_inventory(resolve(options.out)))
    os.makedirs(os.path.dirname(resolve(options.out)), exist_ok=True)
    write_inventory(rows, resolve(options.out))
    if options.json:
        with open(options.json, "w") as f:
            json.dump(rows, f, indent=1)
    print("%d functions in %s" % (len(rows), os.path.relpath(resolve(options.out), options.root)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
