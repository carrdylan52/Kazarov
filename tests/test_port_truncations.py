"""scripts/port_truncations.py fails when a translation unit does not load or parse.

libclang is not needed: the units' results are given, as the workers would return them.
"""

import importlib.util
import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def load():
    spec = importlib.util.spec_from_file_location("port_truncations", ROOT / "scripts" / "port_truncations.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture
def truncations(monkeypatch, tmp_path):
    module = load()
    monkeypatch.setattr(module, "load_libclang", lambda explicit: None)
    monkeypatch.setattr(module, "debug_build", lambda build: True)
    monkeypatch.setattr(module, "unit_entries", lambda build: [{"file": "ps2/src/a.cpp"}, {"file": "port/src/b.cpp"}])
    monkeypatch.setattr(sys, "argv", ["port_truncations.py", "--executable", "game", "--root", str(tmp_path),
                                      "--out", str(tmp_path / "truncations.md"), "--check"])
    return module


def unit(path, **fields):
    return {"file": path, "casts": [], "back": [], "sinks": [], "edges": [], "definitions": [], "errors": [],
            **fields}


def test_a_unit_that_does_not_load_fails(truncations, monkeypatch, capsys):
    monkeypatch.setattr(truncations, "run_units", lambda *args: [
        {"file": "ps2/src/a.cpp", "error": "Error parsing translation unit."}, unit("port/src/b.cpp")])
    assert truncations.main() == 1
    assert "1 of 2 units did not load or parse" in capsys.readouterr().err


def test_a_unit_with_errors_fails(truncations, monkeypatch, tmp_path):
    monkeypatch.setattr(truncations, "run_units", lambda *args: [
        unit("ps2/src/a.cpp", errors=["a.cpp:1:1: error: unknown type name 'x'"]), unit("port/src/b.cpp")])
    assert truncations.main() == 1
    assert not (tmp_path / "truncations.md").exists()


# The copy a port compiles drops the bodies it replaces; the audit reads the unit with them, and
# the MW runtime's among them throw.
def test_a_game_unit_is_read_with_exceptions(tmp_path):
    build = tmp_path / "build"
    copy = build / "ps2_src" / "a.cpp"
    copy.parent.mkdir(parents=True)
    copy.write_text("")
    (build / "compile_commands.json").write_text(json.dumps([{
        "directory": str(build), "file": str(copy), "command": "clang++ -fno-exceptions -c %s -o a.o" % copy}]))
    module = load()
    module.ROOT = str(tmp_path)
    [entry] = module.unit_entries(str(build))
    assert entry["file"] == str(tmp_path / "ps2" / "src" / "a.cpp")
    assert "-fexceptions" in entry["arguments"] and "-fno-exceptions" not in entry["arguments"]
