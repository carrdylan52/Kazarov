#!/usr/bin/env python3
"""Write the asserting definitions of the SDK functions a port has no replacement for.

    gen_sce_stubs.py --ledger docs/port/ASM_LEDGER.tsv --headers ps2/include/sce --out DIR

Every ledger row whose replacement is `stub` becomes a definition of that function, spelled as the
game's own SDK header declares it, whose body is PS2_UNIMPLEMENTED(). The definitions go to
DIR/<header>.cpp, one file per declaring header, in the header's order; other .cpp files in DIR are
removed and an unchanged file is not rewritten. The paths written are printed, one per line.

The ledger is tab-separated with a header line naming its columns; `symbol` and `replacement` are
the ones read here. A stub symbol no header declares, or one two headers declare differently, stops
the script.
"""

import argparse
import csv
import pathlib
import re
import sys

COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)
DIRECTIVE = re.compile(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", re.MULTILINE)
DEFAULT_ARGUMENT = re.compile(r"\s*=[^,()]*(?=[,)])")


def stub_symbols(ledger):
    with open(ledger, newline="", encoding="utf-8") as file:
        rows = csv.DictReader((line for line in file if not line.startswith("#")), delimiter="\t")
        missing = {"symbol", "replacement"} - set(rows.fieldnames or [])
        if missing:
            sys.exit(f"{ledger}: no column {', '.join(sorted(missing))}")
        return sorted({row["symbol"] for row in rows if row["replacement"] == "stub"})


def declarations(header, symbols):
    """The prototype of each wanted symbol the header declares, with its offset in the header."""
    text = DIRECTIVE.sub("", COMMENT.sub(" ", header.read_text(encoding="utf-8")))
    found = {}
    for symbol in symbols:
        pattern = re.compile(r"(?:^|(?<=[;{}]))([^;{}]*?\b" + re.escape(symbol) + r"\s*\([^;{}]*\))\s*;")
        for match in pattern.finditer(text):
            prototype = " ".join(match.group(1).split())
            prototype = re.sub(r"^extern\s+(?:\"C\"\s+)?", "", prototype)
            prototype = prototype.replace("( ", "(").replace(" )", ")")
            prototype = DEFAULT_ARGUMENT.sub("", prototype)
            if symbol in found and found[symbol][1] != prototype:
                sys.exit(f"{header}: {symbol} is declared twice, differently")
            found.setdefault(symbol, (match.start(1), prototype))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--ledger", required=True, type=pathlib.Path)
    parser.add_argument("--headers", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()

    symbols = stub_symbols(args.ledger)
    owners = {}
    files = {}
    for header in sorted(args.headers.glob("*.h")):
        for symbol, (offset, prototype) in declarations(header, symbols).items():
            if symbol in owners and owners[symbol][1] != prototype:
                sys.exit(f"{symbol} is declared differently in {owners[symbol][0].name} and {header.name}")
            if symbol not in owners:
                owners[symbol] = (header, prototype)
                files.setdefault(header, []).append((offset, prototype))
    unknown = [symbol for symbol in symbols if symbol not in owners]
    if unknown:
        sys.exit(f"no header in {args.headers} declares {', '.join(unknown)}")

    args.out.mkdir(parents=True, exist_ok=True)
    written = []
    for header, stubs in files.items():
        lines = [f"#include <{header.name}>"]
        for _, prototype in sorted(stubs):
            lines += ["", prototype + " {", "    PS2_UNIMPLEMENTED();", "}"]
        path = args.out / (header.stem + ".cpp")
        text = "\n".join(lines) + "\n"
        if not path.exists() or path.read_text(encoding="utf-8") != text:
            path.write_text(text, encoding="utf-8")
        written.append(path)
    for stale in args.out.glob("*.cpp"):
        if stale not in written:
            stale.unlink()
    for path in sorted(written):
        print(path.as_posix())


if __name__ == "__main__":
    main()
