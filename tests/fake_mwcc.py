#!/usr/bin/env python3
"""Stand-in for mwcc, used as the EXE of kz-mwcc in tests/test_kz_mwcc.py.

It is run as `RUNNER fake_mwcc.py ARGS...`, RUNNER being `sys.executable` in the tests.
It needs each source and each -i/-I directory's common.h to resolve from its cwd, and fails
otherwise. The sources are the arguments after `-o OUT`; with several, or when OUT is a
directory, it writes OUT/<stem>.o and OUT/<stem>.d for each, as MWCC does. Controls, all from
the environment:

  FAKE_MWCC_LOG     JSON record of the run (cwd, listing, arguments) is written here.
  FAKE_MWCC_EXIT    nonzero: exit with this code and write nothing.
  FAKE_MWCC_OUTPUT  ok (default), empty, or none (no -o file); for several sources, it
                    applies to the last one only.
  FAKE_MWCC_DEP     ok (default; <source stem>.d), object (<object stem>.d), cwd (<source
                    stem>.d in the cwd even beside a directory output), or none.

The map is written the way MWCC's is, with CRLF line ends: a relative path, a path under the
run's cwd (temporary directory), a Z:\\ path under the project root and a temporary target.
"""

import json
import os
import sys


def write(source, obj, header, cwd, map_dir, output):
    """The object and the dependency map for one source."""
    if output == "ok":
        with open(obj, "w", encoding="utf-8") as f:
            f.write("object for %s\n" % source)
    elif output == "empty":
        open(obj, "w").close()

    dep = os.environ.get("FAKE_MWCC_DEP", "ok")
    if dep != "none":
        stem = os.path.splitext(os.path.basename(obj if dep == "object" else source))[0]
        root = os.environ.get("KZ_PROJECT_ROOT", cwd)
        windows_root = "Z:" + root.replace("/", "\\")
        lines = [
            "%s: %s \\" % (os.path.basename(obj), source),
            "  %s \\" % header,
            "  %s/own.h \\" % cwd,
            "  tmpQ7x.o: \\",
            "  %s\\private\\win.h" % windows_root,
        ]
        with open(os.path.join(map_dir, stem + ".d"), "w", encoding="utf-8", newline="") as f:
            f.write("\r\n".join(lines) + "\r\n")


def main(argv):
    args = argv[1:]
    obj = None
    sources = []
    includes = []
    i = 0
    while i < len(args) - 1:
        arg = args[i]
        if arg == "-o":
            obj = args[i + 1]
            sources = args[i + 2:]
            break
        if arg in ("-i", "-I"):
            includes.append(args[i + 1])
            i += 2
            continue
        if arg[:2] in ("-i", "-I") and len(arg) > 2:
            includes.append(arg[2:])
        i += 1

    cwd = os.getcwd()
    code = int(os.environ.get("FAKE_MWCC_EXIT", "0"))
    with open(os.environ["FAKE_MWCC_LOG"], "w", encoding="utf-8") as f:
        json.dump({"cwd": cwd, "env_cwd": os.environ.get("KZ_MWCC_CWD"),
                   "listing": sorted(os.listdir(".")), "argv": args}, f)
    if code:
        return code

    for source in sources:
        if not os.path.isfile(source):
            print("fake_mwcc: cannot open %s" % source, file=sys.stderr)
            return 1
    header = None
    for directory in includes:
        candidate = os.path.join(directory, "common.h")
        if os.path.isfile(candidate):
            header = candidate
            break
    if header is None:
        print("fake_mwcc: common.h not found in %s" % includes, file=sys.stderr)
        return 1

    several = len(sources) > 1 or os.path.isdir(obj)
    for index, source in enumerate(sources):
        stem = os.path.splitext(os.path.basename(source))[0]
        target = os.path.join(obj, stem + ".o") if several else obj
        last = index == len(sources) - 1
        beside = several and os.environ.get("FAKE_MWCC_DEP") != "cwd"
        write(source, target, header, cwd, os.path.dirname(target) if beside else cwd,
              os.environ.get("FAKE_MWCC_OUTPUT", "ok") if last else "ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
